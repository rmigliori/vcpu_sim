#include "vcpu.h"
#include "toolchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int peek_magic(const char* path, char* out, size_t sz);

static void print_stats(const VCpu* cpu)
{
    printf("---- stats ----\n");
    printf("instructions executed : %llu\n", (unsigned long long) cpu->instr_count);
    printf("vector element ops     : %llu\n", (unsigned long long) cpu->vec_elem_ops);
    printf("cycles (timing model)  : %llu\n", (unsigned long long) cpu->cycles);
}

// --- legacy path: assemble a .vasm and run it in memory --------------------
static int cmd_legacy(const char* path, RunMode mode)
{
    static VCpu  cpu;
    static Instr prog[MAX_INSTR];
    char err[256] = {0};

    vcpu_init(&cpu);
    int len = assemble(path, &cpu, prog, err, sizeof err);
    if (len < 0) { fprintf(stderr, "assemble error: %s\n", err); return 1; }

    vcpu_run_ex(&cpu, prog, len, mode);
    print_stats(&cpu);
    return 0;
}

// --- asm: .vasm -> .vo -----------------------------------------------------
static int cmd_asm(int argc, char** argv)
{
    const char* in = NULL;
    const char* out = NULL;
    const char* expanded = NULL;
    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(argv[i], "--emit-expanded") == 0 && i + 1 < argc) expanded = argv[++i];
        else in = argv[i];
    }
    if (!in || !out)
    { fprintf(stderr, "usage: %s asm <in.vasm> -o <out.vo> [--emit-expanded <file>]\n", argv[0]); return 2; }

    VObject* obj = calloc(1, sizeof(VObject));
    if (!obj) { fprintf(stderr, "out of memory\n"); return 1; }
    char err[256] = {0};

    int rc = 1;
    if (assemble_object(in, obj, expanded, err, sizeof err) != 0)
        fprintf(stderr, "asm error: %s\n", err);
    else if (vo_write(out, obj, err, sizeof err) != 0)
        fprintf(stderr, "asm error: %s\n", err);
    else
        rc = 0;

    vobject_free(obj);
    free(obj);
    return rc;
}

// --- ld: .vo ... -> .vx ----------------------------------------------------
// --- ld: link .vo objects and .va archives into a .vx ----------------------
#define LD_MAX 64

static int obj_defines(const VObject* o, const char* name)
{
    for (int i = 0; i < o->sym_count; ++i)
        if (o->syms[i].section != RSEC_NONE && o->syms[i].binding != BIND_EXTERN &&
            strcmp(o->syms[i].name, name) == 0)
            return 1;
    return 0;
}

static int obj_refs(const VObject* o, const char* name)
{
    for (int i = 0; i < o->reloc_count; ++i)
        if (strcmp(o->relocs[i].sym, name) == 0)
            return 1;
    return 0;
}

static int cmd_ld(int argc, char** argv)
{
    const char* out   = NULL;
    const char* entry = NULL;
    const char* ins[LD_MAX];
    int nin = 0;
    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) entry = argv[++i];
        else if (nin < LD_MAX) ins[nin++] = argv[i];
    }
    if (nin == 0 || !out) { fprintf(stderr, "usage: %s ld <a.vo|lib.va> ... [-e <sym>] -o <out.vx>\n", argv[0]); return 2; }

    // Explicit objects are always linked; archive members are pulled in on demand.
    VObject* expl = calloc(LD_MAX, sizeof(VObject)); int nexpl = 0;
    VObject* memb = calloc(LD_MAX, sizeof(VObject)); int nmemb = 0;
    char (*mname)[64] = calloc(LD_MAX, 64);
    int*  incl = calloc(LD_MAX, sizeof(int));
    VImage* img = calloc(1, sizeof(VImage));
    if (!expl || !memb || !mname || !incl || !img)
    { fprintf(stderr, "out of memory\n"); goto oom; }

    char err[256] = {0};
    int rc = 1;

    for (int i = 0; i < nin; ++i)
    {
        char magic[8] = {0};
        if (peek_magic(ins[i], magic, sizeof magic) != 0)
        { fprintf(stderr, "ld error: cannot read '%s'\n", ins[i]); goto done; }

        if (strcmp(magic, "VO1") == 0)
        {
            if (nexpl >= LD_MAX) { fprintf(stderr, "ld error: too many objects\n"); goto done; }
            if (vo_read(ins[i], &expl[nexpl], err, sizeof err) != 0)
            { fprintf(stderr, "ld error: %s\n", err); goto done; }
            nexpl += 1;
        }
        else if (strcmp(magic, "VA1") == 0)
        {
            int got = 0;
            if (va_read(ins[i], &memb[nmemb], &mname[nmemb], &got, LD_MAX - nmemb, err, sizeof err) != 0)
            { fprintf(stderr, "ld error: %s\n", err); goto done; }
            nmemb += got;
        }
        else { fprintf(stderr, "ld error: '%s' is not a .vo/.va file\n", ins[i]); goto done; }
    }

    // Selective inclusion: pull an archive member if it defines a symbol that is
    // referenced but not yet defined by the current included set. Repeat to a fixpoint.
    for (int changed = 1; changed; )
    {
        changed = 0;
        for (int k = 0; k < nmemb; ++k)
        {
            if (incl[k]) continue;
            for (int s = 0; s < memb[k].sym_count; ++s)
            {
                const ObjSym* sym = &memb[k].syms[s];
                if (sym->binding != BIND_GLOBAL || sym->section == RSEC_NONE) continue;
                const char* nm = sym->name;

                int referenced = 0, defined = 0;
                for (int e = 0; e < nexpl && !referenced; ++e) referenced = obj_refs(&expl[e], nm);
                for (int j = 0; j < nmemb && !referenced; ++j) if (incl[j]) referenced = obj_refs(&memb[j], nm);
                for (int e = 0; e < nexpl && !defined; ++e) defined = obj_defines(&expl[e], nm);
                for (int j = 0; j < nmemb && !defined; ++j) if (incl[j]) defined = obj_defines(&memb[j], nm);

                if (referenced && !defined) { incl[k] = 1; changed = 1; break; }
            }
        }
    }

    // Build the link list: explicit objects first, then included archive members.
    const VObject* list[LD_MAX * 2]; int nlink = 0;
    for (int e = 0; e < nexpl; ++e) list[nlink++] = &expl[e];
    for (int k = 0; k < nmemb; ++k) if (incl[k]) list[nlink++] = &memb[k];

    if (link_objects(list, nlink, img, entry, err, sizeof err) != 0)
        fprintf(stderr, "ld error: %s\n", err);
    else if (vx_write(out, img, err, sizeof err) != 0)
        fprintf(stderr, "ld error: %s\n", err);
    else
        rc = 0;

done:
    for (int i = 0; i < nexpl; ++i) vobject_free(&expl[i]);
    for (int i = 0; i < nmemb; ++i) vobject_free(&memb[i]);
    vimage_free(img);
    free(expl); free(memb); free(mname); free(incl); free(img);
    return rc;

oom:
    free(expl); free(memb); free(mname); free(incl); free(img);
    return 1;
}

// --- ar: bundle .vo objects into a .va archive -----------------------------
static int cmd_ar(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s ar <lib.va> <o1.vo> ...\n", argv[0]); return 2; }
    const char* out = argv[2];
    const char* members[LD_MAX];
    int n = 0;
    for (int i = 3; i < argc && n < LD_MAX; ++i) members[n++] = argv[i];

    char err[256] = {0};
    if (va_write(out, members, n, err, sizeof err) != 0)
    { fprintf(stderr, "ar error: %s\n", err); return 1; }
    return 0;
}

// --- run: load and execute a .vx -------------------------------------------
static int cmd_run(int argc, char** argv)
{
    const char* path = NULL;
    RunMode mode = RUN_NORMAL;
    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "--trace") == 0)      mode = RUN_TRACE;
        else if (strcmp(argv[i], "--debug") == 0) mode = RUN_DEBUG;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: %s run <prog.vx> [--trace|--debug]\n", argv[0]); return 2; }

    static VCpu  cpu;
    static Instr prog[MAX_INSTR];
    VImage* img = calloc(1, sizeof(VImage));
    if (!img) { fprintf(stderr, "out of memory\n"); return 1; }
    char err[256] = {0};

    int rc = 1;
    if (vx_read(path, img, err, sizeof err) != 0)
    {
        fprintf(stderr, "run error: %s\n", err);
    }
    else
    {
        vcpu_init(&cpu);
        int len = 0;
        int64_t entry = vx_load(img, &cpu, prog, &len);
        vcpu_run_from(&cpu, prog, len, mode, entry);
        print_stats(&cpu);
        rc = 0;
    }

    vimage_free(img);
    free(img);
    return rc;
}

// --- nm: list the symbols of a .vo or .vx ----------------------------------
// Read the file's magic tag ("VO1" / "VX1") into 'out'.
static int peek_magic(const char* path, char* out, size_t sz)
{
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;
    int ok = fscanf(fp, "%3s", out) == 1;
    (void) sz;
    fclose(fp);
    return ok ? 0 : -1;
}

// nm-style type letter: uppercase = global. T code, D data, U undefined.
static char nm_type(int section, int binding, int is_code)
{
    if (section == RSEC_NONE || binding == BIND_EXTERN) return 'U';
    char c = is_code ? 't' : 'd';
    return (binding == BIND_GLOBAL) ? (char) toupper((unsigned char) c) : c;
}

// Symbol ordering for nm output (Unix-style). Set before qsort; the comparator
// takes no user argument so the mode is passed through file-static state.
typedef enum { NM_SORT_NAME, NM_SORT_VALUE, NM_SORT_NONE } NmSort;
static NmSort g_nm_sort    = NM_SORT_NAME;
static int    g_nm_reverse = 0;

static int nm_cmp(const void* pa, const void* pb)
{
    const ObjSym* a = pa;
    const ObjSym* b = pb;
    int r;
    if (g_nm_sort == NM_SORT_VALUE)
    {
        r = (a->offset > b->offset) - (a->offset < b->offset);
        if (r == 0) r = strcmp(a->name, b->name);
    }
    else
        r = strcmp(a->name, b->name);
    return g_nm_reverse ? -r : r;
}

static void nm_sort(ObjSym* syms, int n)
{
    if (g_nm_sort != NM_SORT_NONE)
        qsort(syms, (size_t) n, sizeof(ObjSym), nm_cmp);
}

static int cmd_nm(int argc, char** argv)
{
    const char* path = NULL;
    g_nm_sort    = NM_SORT_NAME;
    g_nm_reverse = 0;
    for (int i = 2; i < argc; ++i)
    {
        if      (strcmp(argv[i], "-n") == 0) g_nm_sort = NM_SORT_VALUE;
        else if (strcmp(argv[i], "-p") == 0) g_nm_sort = NM_SORT_NONE;
        else if (strcmp(argv[i], "-r") == 0) g_nm_reverse = 1;
        else                                 path = argv[i];
    }
    if (!path)
    { fprintf(stderr, "usage: %s nm [-n|-p] [-r] <file.vo|file.vx>\n", argv[0]); return 2; }

    char magic[8] = {0};
    if (peek_magic(path, magic, sizeof magic) != 0)
    { fprintf(stderr, "nm error: cannot read '%s'\n", path); return 1; }

    char err[256] = {0};

    if (strcmp(magic, "VX1") == 0)
    {
        VImage* img = calloc(1, sizeof(VImage));
        if (!img) { fprintf(stderr, "out of memory\n"); return 1; }
        int rc = 1;
        if (vx_read(path, img, err, sizeof err) != 0)
            fprintf(stderr, "nm error: %s\n", err);
        else
        {
            printf("entry: %lld\n", (long long) img->entry);
            nm_sort(img->symmap, img->sym_count);
            for (int i = 0; i < img->sym_count; ++i)
                printf("%8lld %c %s\n", (long long) img->symmap[i].offset,
                       nm_type(img->symmap[i].section, BIND_GLOBAL, img->symmap[i].is_code),
                       img->symmap[i].name);
            rc = 0;
        }
        vimage_free(img);
        free(img);
        return rc;
    }
    else if (strcmp(magic, "VO1") == 0)
    {
        VObject* obj = calloc(1, sizeof(VObject));
        if (!obj) { fprintf(stderr, "out of memory\n"); return 1; }
        int rc = 1;
        if (vo_read(path, obj, err, sizeof err) != 0)
            fprintf(stderr, "nm error: %s\n", err);
        else
        {
            nm_sort(obj->syms, obj->sym_count);
            for (int i = 0; i < obj->sym_count; ++i)
            {
                const ObjSym* s = &obj->syms[i];
                char t = nm_type(s->section, s->binding, s->is_code);
                if (s->section == RSEC_NONE) printf("%8s %c %s\n", "", t, s->name);
                else                         printf("%8lld %c %s\n", (long long) s->offset, t, s->name);
            }
            rc = 0;
        }
        vobject_free(obj);
        free(obj);
        return rc;
    }

    fprintf(stderr, "nm error: '%s' is not a .vo/.vx file (magic '%s')\n", path, magic);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "asm") == 0) return cmd_asm(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "ld") == 0)  return cmd_ld(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "run") == 0) return cmd_run(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "nm") == 0)  return cmd_nm(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "ar") == 0)  return cmd_ar(argc, argv);

    // Legacy: vcpu_sim [--trace|--debug] <program.vasm>
    const char* path = NULL;
    RunMode     mode = RUN_NORMAL;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--trace") == 0)      mode = RUN_TRACE;
        else if (strcmp(argv[i], "--debug") == 0) mode = RUN_DEBUG;
        else                                      path = argv[i];
    }
    if (!path)
    {
        fprintf(stderr,
                "usage: %s [--trace|--debug] <program.vasm>\n"
                "       %s asm <in.vasm> -o <out.vo>\n"
                "       %s ld  <a.vo|lib.va> ... [-e <sym>] -o <out.vx>\n"
                "       %s run <prog.vx> [--trace|--debug]\n"
                "       %s nm  [-n|-p] [-r] <file.vo|file.vx>\n"
                "       %s ar  <lib.va> <o1.vo> ...\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    return cmd_legacy(path, mode);
}
