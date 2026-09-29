#include "machine.h"
#include "toolchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int peek_magic(const char* path, char* out, size_t sz);

// -I <dir> / -Idir: append to the assembler's .include search path. Returns 1
// if argv[*i] was an -I flag (advancing *i past its argument when separate),
// 0 if it was not, -1 on error (message already printed). A dropped -I would
// only surface later as a puzzling "cannot open include", so it is fatal.
static int parse_include_flag(int argc, char** argv, int* i)
{
  const char* dir = NULL;
  if (strcmp(argv[*i], "-I") == 0 && *i + 1 < argc)          dir = argv[++(*i)];
  else if (strncmp(argv[*i], "-I", 2) == 0 && argv[*i][2])   dir = argv[*i] + 2;
  else return 0;

  if (asm_add_include_dir(dir) != 0)
  {
    fprintf(stderr, "error: too many -I directories ('%s')\n", dir);
    return -1;
  }
  return 1;
}

// -D <name> / -Dname: define a name for .ifdef/.ifndef. Same contract as
// parse_include_flag, and fatal for the same reason — a -D that went missing
// would not fail, it would quietly assemble the OTHER branch.
//
// Two refusals on purpose, both of things that would otherwise be silent:
//   -DNAME=value   PRESENCE is the whole feature; a name that also carried a
//                  value would look like a constant and not be one.
//   -Dnot.an.ident a name that no .ifdef can ever spell would simply never
//                  match, and the build would be wrong without a word.
static int parse_define_flag(int argc, char** argv, int* i)
{
  const char* name = NULL;
  if (strcmp(argv[*i], "-D") == 0 && *i + 1 < argc)          name = argv[++(*i)];
  else if (strncmp(argv[*i], "-D", 2) == 0 && argv[*i][2])   name = argv[*i] + 2;
  else return 0;

  if (strchr(name, '='))
  {
    fprintf(stderr, "error: -D takes a name, not a value ('%s'). A name is "
                    "either defined or not; it never has a value.\n", name);
    return -1;
  }
  if (!(isalpha((unsigned char) name[0]) || name[0] == '_'))
  {
    fprintf(stderr, "error: '-D %s' is not a name\n", name);
    return -1;
  }
  for (const char* p = name + 1; *p; ++p)
    if (!(isalnum((unsigned char) *p) || *p == '_'))
    {
      fprintf(stderr, "error: '-D %s' is not a name\n", name);
      return -1;
    }

  if (asm_add_define(name) != 0)
  {
    fprintf(stderr, "error: too many -D names ('%s')\n", name);
    return -1;
  }
  return 1;
}

// The two assembler flags together, in the order a command line writes them.
// Returns 1 consumed, 0 not ours, -1 error (already reported).
static int parse_asm_flag(int argc, char** argv, int* i)
{
  int r = parse_include_flag(argc, argv, i);
  if (r != 0) return r;
  return parse_define_flag(argc, argv, i);
}

// Le statistiche sono PER CPU, una per ogni CPU accesa, col suo prefisso
// (vuoto con una CPU sola). La frequenza e' quella della CPU -- il master
// diviso il suo divisore -- perche' i cicli qui sopra sono cicli di CPU.
static void print_stats(const VMachine* m)
{
  for (int i = 0; i < NUM_CPU; ++i)
  {
    const VCpu* cpu = &m->cpu[i];
    if (!cpu->prog) continue;
    const char* g = cpu->tag;
    printf("%s---- stats ----\n", g);
    printf("%sinstructions executed : %llu\n", g, (unsigned long long) cpu->instr_count);
    printf("%svector element ops     : %llu\n", g, (unsigned long long) cpu->vec_elem_ops);
    printf("%scycles (timing model)  : %llu\n", g, (unsigned long long) cpu->cycles);
    // La frequenza accanto ai cicli, e non altrove: chi legge questi numeri per
    // farne un tempo (tools/scheduler_facts.py) la trova qui, invece di tenerne
    // una copia. E' lo stesso modello dei canali riservati nella registrazione.
    printf("%sclock (Hz)             : %llu\n", g,
           (unsigned long long) (m->clock.hz / m->clock.div_cpu));
  }
}

// --- legacy path: assemble a .vasm and run it in memory --------------------
// La traccia della tastiera, se --kbd e' stato passato. E' file-static perche'
// la ricevono entrambi i percorsi di esecuzione (legacy a file singolo e
// "run" su un .vx) e non cambia niente per chi non la usa.
static const char* g_kbd_spec = NULL;
static const char* g_marche_out = NULL;
// Il segnale dell'ADC (--adc), stesso percorso della traccia: e' il MONDO,
// e sta fuori dalla macchina come i tasti premuti.
static const char* g_adc_spec = NULL;

// ---------------------------------------------------------------------------
//  Il marcatore: accensione e scarico.
//
//  `current` si risolve dalla TABELLA DEI SIMBOLI del .vx, che il loader ha
//  gia' in mano (e' la stessa che stampa `nm`: "740 D current"). Niente da
//  passare a mano, niente indirizzo cablato -- il difetto che §3.37 aveva gia'
//  segnalato due volte in un giorno. Se il programma quel simbolo non ce l'ha
//  (un test che non linka il kernel), il canale dell'esecuzione resta vuoto e
//  il resto funziona lo stesso.
// ---------------------------------------------------------------------------
static void marks_prepare(VMachine* m, int id, const VImage* img)
{
  if (!g_marche_out) return;
  VCpu* cpu = &m->cpu[id];
  m->marker[id].on = 1;
  for (int i = 0; i < img->sym_count; ++i)
    if (!img->symmap[i].is_code && strcmp(img->symmap[i].name, "current") == 0)
    {
      cpu->mark_current_addr = img->symmap[i].offset;
      return;
    }
  fprintf(stderr, "marche: nessun simbolo 'current': il canale %d resta vuoto\n",
          MARK_EXEC);
}

// La stessa cosa per il percorso a file singolo, che una VImage non ce l'ha: il
// simbolo lo chiede all'assembler, che l'ha appena visto passare.
static void marks_prepare_legacy(VMachine* m)
{
  if (!g_marche_out) return;
  VCpu* cpu = &m->cpu[0];
  m->marker[0].on = 1;
  int64_t addr;
  if (assemble_symbol("current", &addr))
  {
    cpu->mark_current_addr = addr;
    return;
  }
  fprintf(stderr, "marche: nessun simbolo 'current': il canale %d resta vuoto\n",
          MARK_EXEC);
}

// Il nome della registrazione di UNA CPU. Con una CPU sola e' quello dato;
// con piu' CPU una per CPU, ciascuna col suo nome: ".cpuN" prima
// dell'estensione ("rec.txt" -> "rec.cpu0.txt"), o in fondo se non ce n'e'.
static void marks_name(const VMachine* m, int id, char* out, size_t sz)
{
  if (m->ncpu <= 1) { snprintf(out, sz, "%s", g_marche_out); return; }
  const char* slash = strrchr(g_marche_out, '/');
  const char* dot   = strrchr(g_marche_out, '.');
  if (dot && (!slash || dot > slash) && dot != g_marche_out)
    snprintf(out, sz, "%.*s.cpu%d%s", (int) (dot - g_marche_out), g_marche_out, id, dot);
  else
    snprintf(out, sz, "%s.cpu%d", g_marche_out, id);
}

static void marks_dump_one(const VMachine* m, int id)
{
  const VMarker* mk = &m->marker[id];
  char name[1024];
  marks_name(m, id, name, sizeof name);
  FILE* f = fopen(name, "w");
  if (!f) { perror(name); return; }
  // I canali della macchina li dichiara LA REGISTRAZIONE, in una forma che il
  // lettore possa leggere. Se se li scrivesse lui sarebbe una seconda verita' —
  // lo stesso difetto che marks.conf esiste per togliere di mezzo.
  // Le due colonne in fondo sono il PORTO DATI: il valore che il programma
  // aveva armato, e se lo aveva armato davvero. La seconda non e' ridondante --
  // zero e' un valore legittimo, quindi senza di essa "il dato era 0" e
  // "nessuno ha armato" sarebbero la stessa riga.
  // I NOMI REGISTRATI dal programma, prima delle marche: il lettore li vuole
  // avere in mano quando comincia a leggerle.
  for (int i = 0; i < mk->names_len; ++i)
    fprintf(f, "# nome %d %d %d\n", mk->names[i].canale,
            mk->names[i].valore, mk->names[i].id);
  if (mk->names_lost)
    fprintf(stderr, "marche: %d nomi PERSI (oltre il tetto di %d)\n",
            mk->names_lost, MARK_NAMES_MAX);
  fprintf(f, "# ciclo canale valore current dato ha_dato in_trap\n");
  fprintf(f, "# riservato %d esecuzione\n", MARK_EXEC);
  fprintf(f, "# riservato %d tasto\n", MARK_KEY);
  fprintf(f, "# riservato %d timer\n", MARK_TIMER);
  // La FREQUENZA viaggia con la registrazione, per la stessa ragione dei
  // canali: e' la macchina che sa a che velocita' gira, e un lettore che la
  // tenesse per conto suo leggerebbe in microsecondi sbagliati una traccia
  // prodotta da un'altra macchina -- senza accorgersene, perche' i cicli
  // sarebbero comunque giusti.
  fprintf(f, "# frequenza %llu\n", (unsigned long long) m->clock.hz);
  for (int i = 0; i < mk->len; ++i)
  {
    const Marca* e = &mk->marche[i];
    fprintf(f, "%llu %d %d %d %d %d %d\n", (unsigned long long) e->cycle,
            e->canale, e->valore, e->current, e->dato, e->ha_dato, e->in_trap);
  }
  fclose(f);
  printf("%smarche: %d in %s", m->cpu[id].tag, mk->len, name);
  if (mk->lost)
    printf("  (PERSE %llu: oltre il tetto di %d)",
           (unsigned long long) mk->lost, MARCHE_MAX);
  printf("\n");
}

// Una registrazione per ogni CPU accesa (§3.79).
static void marks_dump(const VMachine* m)
{
  if (!g_marche_out) return;
  for (int i = 0; i < NUM_CPU; ++i)
    if (m->cpu[i].prog) marks_dump_one(m, i);
}

// Applica la traccia dopo machine_init, che azzera tutto. Ritorna 0 o 1.
static int apply_kbd_trace(VMachine* m)
{
  char err[256] = {0};
  if (!g_kbd_spec) return 0;
  if (kbd_set_trace(&m->kbd, g_kbd_spec, err, sizeof err) != 0)
  {
    fprintf(stderr, "%s\n", err);
    return 1;
  }
  return 0;
}

// Il segnale dell'ADC, applicato dopo machine_init come la traccia. Ritorna 0 o 1.
static int apply_adc_signal(VMachine* m)
{
  char err[256] = {0};
  if (!g_adc_spec) return 0;
  if (adc_set_signal(&m->adc, g_adc_spec, err, sizeof err) != 0)
  {
    fprintf(stderr, "%s\n", err);
    return 1;
  }
  return 0;
}

static int cmd_legacy(const char* path, RunMode mode)
{
  static VMachine m;
  static Instr prog[MAX_INSTR];
  char err[256] = {0};

  machine_init(&m);
  if (apply_kbd_trace(&m) != 0) return 2;
  if (apply_adc_signal(&m) != 0) return 2;
  int len = assemble(path, m.ram[0], prog, err, sizeof err);
  if (len < 0) { fprintf(stderr, "assemble error: %s\n", err); return 1; }

  // --marks VALE ANCHE QUI (14/09/2026, §3.68). Fino a oggi la registrazione
  // esisteva solo nel percorso `run <prog.vx>`, e su un .vasm l'opzione veniva
  // accettata e non faceva NIENTE: nessun file, nessun messaggio, exit 0. Chi
  // la usava concludeva che il suo programma non emetteva marche e andava a
  // cercare il difetto nel posto sbagliato.
  //
  // Dev'essere DOPO assemble(): il canale 0 vuole l'indirizzo di `current`, e
  // quel simbolo esiste solo a assemblaggio fatto.
  marks_prepare_legacy(&m);
  machine_load(&m, 0, prog, len, 0);
  machine_run(&m, mode);
  marks_dump(&m);
  print_stats(&m);
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
    int f = parse_asm_flag(argc, argv, &i);
    if (f < 0) return 2;
    if (f > 0) continue;
    if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
    else if (strcmp(argv[i], "--emit-expanded") == 0 && i + 1 < argc) expanded = argv[++i];
    else in = argv[i];
  }
  if (!in || !out)
  { fprintf(stderr, "usage: %s asm <in.vasm> -o <out.vo> [-I <dir>]... [-D <name>]... [--emit-expanded <file>]\n", argv[0]); return 2; }

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
  // UN .vx PER CPU, in ordine (§3.79): il primo sulla CPU 0, il secondo sulla
  // CPU 1. Con uno solo la CPU 1 resta ferma, e tutto e' come prima.
  const char* paths[NUM_CPU];
  int npaths = 0;
  RunMode mode = RUN_NORMAL;
  for (int i = 2; i < argc; ++i)
  {
    if (strcmp(argv[i], "--trace") == 0)      mode = RUN_TRACE;
    else if (strcmp(argv[i], "--debug") == 0) mode = RUN_DEBUG;
    else if (strcmp(argv[i], "--kbd") == 0 && i + 1 < argc) g_kbd_spec = argv[++i];
    else if (strcmp(argv[i], "--adc") == 0 && i + 1 < argc) g_adc_spec = argv[++i];
    else if (strcmp(argv[i], "--marks") == 0 && i + 1 < argc) g_marche_out = argv[++i];
    else if (npaths < NUM_CPU) paths[npaths++] = argv[i];
    else
    {
      fprintf(stderr, "run: al piu' %d programmi, uno per CPU ('%s' e' di troppo)\n",
              NUM_CPU, argv[i]);
      return 2;
    }
  }
  if (npaths == 0)
  {
    fprintf(stderr, "usage: %s run <prog.vx> [<prog_cpu1.vx>] [--trace|--debug] [--kbd <ciclo:car,...>]"
                    " [--adc <Hz>,<ampiezza>] [--marks <file>]\n", argv[0]);
    return 2;
  }
  // Il debugger ha UNA tabella delle etichette: con due programmi i nomi si
  // sovrapporrebbero. Limite dichiarato (§3.79).
  if (mode == RUN_DEBUG && npaths > 1)
  {
    fprintf(stderr, "run: --debug vuole un programma solo\n");
    return 2;
  }

  static VMachine m;
  static Instr prog[NUM_CPU][MAX_INSTR];
  VImage* img[NUM_CPU] = { NULL };
  char err[256] = {0};
  int rc = 1;

  for (int i = 0; i < npaths; ++i)
  {
    img[i] = calloc(1, sizeof(VImage));
    if (!img[i]) { fprintf(stderr, "out of memory\n"); goto fine; }
    if (vx_read(paths[i], img[i], err, sizeof err) != 0)
    {
      fprintf(stderr, "run error: %s\n", err);
      goto fine;
    }
  }

  machine_init(&m);
  if (apply_kbd_trace(&m) != 0)  { rc = 2; goto fine; }
  if (apply_adc_signal(&m) != 0) { rc = 2; goto fine; }
  // All'indietro, e la CPU 0 per ultima: vx_load pubblica le etichette del
  // SUO programma per --trace, e cosi' restano quelle della CPU 0.
  for (int i = npaths - 1; i >= 0; --i)
  {
    marks_prepare(&m, i, img[i]);
    int len = 0;
    int64_t entry = vx_load(img[i], m.ram[i], prog[i], &len);
    machine_load(&m, i, prog[i], len, entry);
  }
  machine_run(&m, mode);
  marks_dump(&m);
  print_stats(&m);
  rc = 0;

fine:
  for (int i = 0; i < npaths; ++i)
    if (img[i]) { vimage_free(img[i]); free(img[i]); }
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
    int f = parse_asm_flag(argc, argv, &i);
    if (f < 0) return 2;
    if (f > 0) continue;
    if (strcmp(argv[i], "--trace") == 0)      mode = RUN_TRACE;
    else if (strcmp(argv[i], "--debug") == 0) mode = RUN_DEBUG;
    else if (strcmp(argv[i], "--kbd") == 0 && i + 1 < argc) g_kbd_spec = argv[++i];
    else if (strcmp(argv[i], "--adc") == 0 && i + 1 < argc) g_adc_spec = argv[++i];
    else if (strcmp(argv[i], "--marks") == 0 && i + 1 < argc) g_marche_out = argv[++i];
    // UN'OPZIONE SCONOSCIUTA E' UN ERRORE, non il nome del programma
    // (14/09/2026, §3.68). Qui c'era un `else path = argv[i]` che prendeva
    // QUALUNQUE argomento come il sorgente: `--marks rec.txt prog.vasm`
    // assegnava path tre volte e vinceva l'ultimo, quindi il programma girava
    // e l'opzione spariva senza un messaggio. Un flag scritto male si comporta
    // allo stesso modo -- e sbagliare a scrivere un flag e' la norma.
    else if (argv[i][0] == '-')
    {
      fprintf(stderr, "opzione sconosciuta: %s\n", argv[i]);
      return 2;
    }
    else                                      path = argv[i];
  }
  if (!path)
  {
    fprintf(stderr,
            "usage: %s [--trace|--debug] [--kbd <ciclo:car,...>] [--adc <Hz>,<ampiezza>] [--marks <file>] [-I <dir>]... [-D <name>]... <program.vasm>\n"
            "       %s asm <in.vasm> -o <out.vo> [-I <dir>]... [-D <name>]...\n"
            "       %s ld  <a.vo|lib.va> ... [-e <sym>] -o <out.vx>\n"
            "       %s run <prog.vx> [<prog_cpu1.vx>] [--trace|--debug] [--kbd <ciclo:car,...>] [--adc <Hz>,<ampiezza>] [--marks <file>]\n"
            "       %s nm  [-n|-p] [-r] <file.vo|file.vx>\n"
            "       %s ar  <lib.va> <o1.vo> ...\n",
            argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 2;
  }
  return cmd_legacy(path, mode);
}
