#include "toolchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ===========================================================================
//  Text I/O helpers
// ===========================================================================

// Read the next meaningful line (skips blank and comment-only ';' lines).
// Returns 1 on success, 0 at EOF. Trailing newline is stripped.
static int next_line(FILE* fp, char* buf, size_t sz)
{
  while (fgets(buf, (int) sz, fp))
  {
    char* p = buf;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == ';') continue;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    return 1;
  }
  return 0;
}

static const char* rsec_name(int s)
{
  return s == RSEC_TEXT ? "text" : s == RSEC_DATA ? "data" : "----";
}

static int rsec_parse(const char* t)
{
  if (strcmp(t, "text") == 0) return RSEC_TEXT;
  if (strcmp(t, "data") == 0) return RSEC_DATA;
  return RSEC_NONE;
}

static const char* bind_name(int b)
{
  return b == BIND_GLOBAL ? "global" : b == BIND_EXTERN ? "extern" : "local";
}

static int bind_parse(const char* t)
{
  if (strcmp(t, "global") == 0) return BIND_GLOBAL;
  if (strcmp(t, "extern") == 0) return BIND_EXTERN;
  return BIND_LOCAL;
}

// Write one instruction as a row of raw fields.
static void write_instr(FILE* fp, int idx, const Instr* in)
{
  fprintf(fp, "%d %d %d %d %d %lld %.17g %d\n",
          idx, (int) in->op, in->a, in->b, in->c,
          (long long) in->imm, in->fimm, in->target);
}

// Parse one instruction row. Returns 1 on success.
static int parse_instr(const char* line, Instr* in)
{
  int idx, op, a, b, c, target;
  long long imm;
  double fimm;
  if (sscanf(line, "%d %d %d %d %d %lld %lg %d",
             &idx, &op, &a, &b, &c, &imm, &fimm, &target) != 8)
    return 0;
  memset(in, 0, sizeof(*in));
  in->op = (OpCode) op;
  in->a = a; in->b = b; in->c = c;
  in->imm = (int64_t) imm;
  in->fimm = fimm;
  in->target = target;
  return 1;
}

// Write a byte image as offset-prefixed hex, 16 bytes per line.
static void write_data(FILE* fp, const uint8_t* data, int64_t count)
{
  for (int64_t off = 0; off < count; off += 16)
  {
    fprintf(fp, "%04llx:", (unsigned long long) off);
    for (int64_t j = off; j < off + 16 && j < count; ++j)
      fprintf(fp, " %02x", data[j]);
    fputc('\n', fp);
  }
}

// Read 'count' data bytes into 'data' (allocated here; NULL if count == 0).
static int read_data(FILE* fp, uint8_t** out, int64_t count, char* err, size_t errsz)
{
  *out = NULL;
  if (count <= 0) return 0;
  uint8_t* data = malloc((size_t) count);
  if (!data) { snprintf(err, errsz, "out of memory"); return -1; }

  int64_t got = 0;
  char line[512];
  while (got < count && next_line(fp, line, sizeof line))
  {
    char* save = NULL;
    for (char* t = strtok_r(line, " \t", &save); t; t = strtok_r(NULL, " \t", &save))
    {
      if (strchr(t, ':')) continue;   // offset label
      if (got >= count) break;
      data[got++] = (uint8_t) strtol(t, NULL, 16);
    }
  }
  if (got != count)
  {
    snprintf(err, errsz, "truncated data section (%lld/%lld bytes)",
             (long long) got, (long long) count);
    free(data);
    return -1;
  }
  *out = data;
  return 0;
}

// ===========================================================================
//  Object (.vo) writer / reader
// ===========================================================================

static int vo_read_stream(FILE* fp, VObject* obj, const char* path, char* err, size_t errsz);

int vo_write(const char* path, const VObject* obj, char* err, size_t errsz)
{
  FILE* fp = fopen(path, "w");
  if (!fp) { snprintf(err, errsz, "cannot write '%s'", path); return -1; }

  fprintf(fp, "VO1\n\n");

  fprintf(fp, ".text %d\n", obj->text_count);
  fprintf(fp, "; idx op a b c imm fimm target\n");
  for (int i = 0; i < obj->text_count; ++i) write_instr(fp, i, &obj->text[i]);

  fprintf(fp, "\n.data %lld\n", (long long) obj->data_count);
  write_data(fp, obj->data, obj->data_count);

  fprintf(fp, "\n.symtab %d\n", obj->sym_count);
  fprintf(fp, "; name section offset binding is_code\n");
  for (int i = 0; i < obj->sym_count; ++i)
  {
    const ObjSym* s = &obj->syms[i];
    if (s->section == RSEC_NONE)
      fprintf(fp, "%s ---- - %s -\n", s->name, bind_name(s->binding));
    else
      fprintf(fp, "%s %s %lld %s %d\n", s->name, rsec_name(s->section),
              (long long) s->offset, bind_name(s->binding), s->is_code);
  }

  fprintf(fp, "\n.reloc %d\n", obj->reloc_count);
  fprintf(fp, "; type site sym addend\n");
  for (int i = 0; i < obj->reloc_count; ++i)
  {
    const ObjReloc* r = &obj->relocs[i];
    const char* tn = r->type == R_CODE ? "R_CODE"
                   : (r->type == R_ADDR ? "R_ADDR" : "R_DATA");
    fprintf(fp, "%s %d %s %lld\n", tn,
            r->site, r->sym, (long long) r->addend);
  }

  fprintf(fp, "\n.end\n");
  fclose(fp);
  return 0;
}

int vo_read(const char* path, VObject* obj, char* err, size_t errsz)
{
  FILE* fp = fopen(path, "r");
  if (!fp) { snprintf(err, errsz, "cannot open '%s'", path); return -1; }
  int rc = vo_read_stream(fp, obj, path, err, errsz);
  fclose(fp);
  return rc;
}

// Read one object from an already-open stream (magic first, stops at .end).
// Does not close 'fp'. Used by vo_read() and the archive reader.
static int vo_read_stream(FILE* fp, VObject* obj, const char* path, char* err, size_t errsz)
{
  memset(obj, 0, sizeof(*obj));

  char line[512];
  if (!next_line(fp, line, sizeof line) || strncmp(line, "VO1", 3) != 0)
  {
    snprintf(err, errsz, "'%s': bad magic (expected VO1)", path);
    return -1;
  }

  while (next_line(fp, line, sizeof line))
  {
    char tag[32]; long long count = 0;
    int got = sscanf(line, "%31s %lld", tag, &count);
    if (got < 1) continue;

    if (strcmp(tag, ".end") == 0) break;

    if (strcmp(tag, ".text") == 0)
    {
      obj->text_count = (int) count;
      for (int i = 0; i < obj->text_count; ++i)
      {
        if (!next_line(fp, line, sizeof line) || !parse_instr(line, &obj->text[i]))
        { snprintf(err, errsz, "'%s': bad .text row %d", path, i); goto fail; }
      }
    }
    else if (strcmp(tag, ".data") == 0)
    {
      if (read_data(fp, &obj->data, count, err, errsz) != 0) goto fail;
      obj->data_count = count;
    }
    else if (strcmp(tag, ".symtab") == 0)
    {
      for (int i = 0; i < (int) count; ++i)
      {
        if (!next_line(fp, line, sizeof line)) { snprintf(err, errsz, "'%s': truncated symtab", path); goto fail; }
        char name[64], sec[16], bind[16], off[32], isc[8];
        if (sscanf(line, "%63s %15s %31s %15s %7s", name, sec, off, bind, isc) != 5)
        { snprintf(err, errsz, "'%s': bad symtab row %d", path, i); goto fail; }
        ObjSym* s = &obj->syms[obj->sym_count++];
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->section = rsec_parse(sec);
        s->offset  = (off[0] == '-') ? 0 : strtoll(off, NULL, 10);
        s->binding = bind_parse(bind);
        s->is_code = (isc[0] == '-') ? -1 : (int) strtol(isc, NULL, 10);
      }
    }
    else if (strcmp(tag, ".reloc") == 0)
    {
      for (int i = 0; i < (int) count; ++i)
      {
        if (!next_line(fp, line, sizeof line)) { snprintf(err, errsz, "'%s': truncated reloc", path); goto fail; }
        char type[16], sym[64]; int site; long long addend;
        if (sscanf(line, "%15s %d %63s %lld", type, &site, sym, &addend) != 4)
        { snprintf(err, errsz, "'%s': bad reloc row %d", path, i); goto fail; }
        ObjReloc* r = &obj->relocs[obj->reloc_count++];
        r->type   = strcmp(type, "R_CODE") == 0 ? R_CODE
             : (strcmp(type, "R_ADDR") == 0 ? R_ADDR : R_DATA);
        r->site   = site;
        r->addend = addend;
        snprintf(r->sym, sizeof(r->sym), "%s", sym);
      }
    }
  }

  return 0;

fail:
  free(obj->data);
  obj->data = NULL;
  return -1;
}

// ===========================================================================
//  Archive (.va) writer / reader
// ===========================================================================

static const char* base_name(const char* path)
{
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

int va_write(const char* path, const char* const* member_paths, int nmemb,
             char* err, size_t errsz)
{
  FILE* out = fopen(path, "w");
  if (!out) { snprintf(err, errsz, "cannot write '%s'", path); return -1; }
  fprintf(out, "VA1\n");

  for (int i = 0; i < nmemb; ++i)
  {
    FILE* in = fopen(member_paths[i], "r");
    if (!in) { snprintf(err, errsz, "cannot open '%s'", member_paths[i]); fclose(out); return -1; }

    char magic[8] = {0};
    if (fscanf(in, "%3s", magic) != 1 || strcmp(magic, "VO1") != 0)
    {
      snprintf(err, errsz, "'%s' is not a .vo object", member_paths[i]);
      fclose(in); fclose(out); return -1;
    }
    rewind(in);

    fprintf(out, ".member %s\n", base_name(member_paths[i]));
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fputc('\n', out);
    fclose(in);
  }

  fclose(out);
  return 0;
}

int va_read(const char* path, VObject* objs, char names[][64], int* count,
            int maxobj, char* err, size_t errsz)
{
  *count = 0;
  FILE* fp = fopen(path, "r");
  if (!fp) { snprintf(err, errsz, "cannot open '%s'", path); return -1; }

  char line[512];
  if (!next_line(fp, line, sizeof line) || strncmp(line, "VA1", 3) != 0)
  {
    snprintf(err, errsz, "'%s': bad magic (expected VA1)", path);
    fclose(fp); return -1;
  }

  while (next_line(fp, line, sizeof line))
  {
    char tag[32], name[64] = {0};
    if (sscanf(line, "%31s %63s", tag, name) < 1) continue;
    if (strcmp(tag, ".member") != 0) continue;

    if (*count >= maxobj) { snprintf(err, errsz, "'%s': too many members", path); goto fail; }
    snprintf(names[*count], 64, "%s", name);
    if (vo_read_stream(fp, &objs[*count], path, err, errsz) != 0) goto fail;
    (*count) += 1;
  }

  fclose(fp);
  return 0;

fail:
  for (int i = 0; i < *count; ++i) { free(objs[i].data); objs[i].data = NULL; }
  fclose(fp);
  return -1;
}

// ===========================================================================
//  Linker
// ===========================================================================

int link_objects(const VObject* const* objs, int nobj, VImage* img,
                 const char* entry_name, char* err, size_t errsz)
{
  memset(img, 0, sizeof(*img));

  // 1. Assign per-module bases (command order).
  int64_t text_base[64], data_base[64];
  if (nobj > 64) { snprintf(err, errsz, "too many objects (max 64)"); return -1; }
  // Data starts above the guard word: address 0 must stay NULL, so that no
  // object can ever be confused with a null pointer (see NULL_GUARD in vcpu.h).
  int64_t tcur = 0, dcur = NULL_GUARD;
  for (int m = 0; m < nobj; ++m)
  {
    text_base[m] = tcur; tcur += objs[m]->text_count;
    data_base[m] = dcur; dcur += objs[m]->data_count;
  }
  if (tcur > MAX_INSTR) { snprintf(err, errsz, "linked code too large (%lld instr)", (long long) tcur); return -1; }

  // 2. Build the global symbol map.
  for (int m = 0; m < nobj; ++m)
  {
    for (int i = 0; i < objs[m]->sym_count; ++i)
    {
      const ObjSym* s = &objs[m]->syms[i];
      if (s->binding != BIND_GLOBAL || s->section == RSEC_NONE) continue;
      for (int j = 0; j < img->sym_count; ++j)
        if (strcmp(img->symmap[j].name, s->name) == 0)
        { snprintf(err, errsz, "duplicate global '%s'", s->name); return -1; }
      ObjSym* g = &img->symmap[img->sym_count++];
      snprintf(g->name, sizeof(g->name), "%s", s->name);
      g->section = s->section;
      g->offset  = (s->section == RSEC_TEXT ? text_base[m] : data_base[m]) + s->offset;
      g->binding = BIND_GLOBAL;
      g->is_code = s->is_code;
    }
  }

  // 3. Concatenate code and data images.
  img->text_count = (int) tcur;
  img->data_count = dcur;
  if (dcur > 0)
  {
    img->data = malloc((size_t) dcur);
    if (!img->data) { snprintf(err, errsz, "out of memory"); return -1; }
    // Zero first: nothing is copied over the guard word, and a datum that lands
    // there by accident would be indistinguishable from a null pointer.
    memset(img->data, 0, (size_t) dcur);
  }
  for (int m = 0; m < nobj; ++m)
  {
    memcpy(&img->text[text_base[m]], objs[m]->text, objs[m]->text_count * sizeof(Instr));
    if (objs[m]->data_count > 0)
      memcpy(&img->data[data_base[m]], objs[m]->data, (size_t) objs[m]->data_count);
  }

  // 4. Apply relocations.
  for (int m = 0; m < nobj; ++m)
  {
    for (int i = 0; i < objs[m]->reloc_count; ++i)
    {
      const ObjReloc* r = &objs[m]->relocs[i];
      int64_t value; int is_code;

      // Prefer a local definition (LOCAL or GLOBAL defined here).
      const ObjSym* d = NULL;
      for (int j = 0; j < objs[m]->sym_count; ++j)
      {
        const ObjSym* s = &objs[m]->syms[j];
        if (s->section != RSEC_NONE && s->binding != BIND_EXTERN &&
            strcmp(s->name, r->sym) == 0) { d = s; break; }
      }
      if (d)
      {
        value = (d->section == RSEC_TEXT ? text_base[m] : data_base[m]) + d->offset;
        is_code = d->is_code;
      }
      else
      {
        const ObjSym* g = NULL;
        for (int j = 0; j < img->sym_count; ++j)
          if (strcmp(img->symmap[j].name, r->sym) == 0) { g = &img->symmap[j]; break; }
        if (!g) { snprintf(err, errsz, "undefined reference to '%s'", r->sym); goto fail; }
        value = g->offset;
        is_code = g->is_code;
      }

      value += r->addend;

      if (r->type == R_CODE && is_code != 1)
      { snprintf(err, errsz, "R_CODE relocation on non-code symbol '%s'", r->sym); goto fail; }
      if (r->type == R_DATA && is_code != 0)
      { snprintf(err, errsz, "R_DATA relocation on non-data symbol '%s'", r->sym); goto fail; }
      // R_ADDR (address-of): value already holds the linear address for
      // either kind (instruction index for code, byte address for data).

      int site = (int) (text_base[m] + r->site);
      if (r->type == R_CODE) img->text[site].target = (int) value;
      else                   img->text[site].imm    = value;
    }
  }

  // 5. Entry point: the requested symbol (default 'main'), a global code symbol.
  const char* ename = entry_name ? entry_name : "main";
  img->entry = 0;
  int found_entry = 0;
  for (int j = 0; j < img->sym_count; ++j)
    if (strcmp(img->symmap[j].name, ename) == 0 && img->symmap[j].is_code == 1)
    { img->entry = img->symmap[j].offset; found_entry = 1; break; }
  if (!found_entry)
  {
    if (entry_name)   // explicitly requested via -e: a hard error
    {
      snprintf(err, errsz, "entry symbol '%s' not found (need a global code symbol)", ename);
      goto fail;
    }
    fprintf(stderr, "ld: warning: no global 'main'; entry point set to 0\n");
  }

  return 0;

fail:
  free(img->data);
  img->data = NULL;
  return -1;
}

void vimage_free(VImage* img)
{
  if (!img) return;
  free(img->data);
  img->data = NULL;
}

// ===========================================================================
//  Executable (.vx) writer / reader / loader
// ===========================================================================

int vx_write(const char* path, const VImage* img, char* err, size_t errsz)
{
  FILE* fp = fopen(path, "w");
  if (!fp) { snprintf(err, errsz, "cannot write '%s'", path); return -1; }

  fprintf(fp, "VX1\n");
  fprintf(fp, ".entry %lld\n\n", (long long) img->entry);

  fprintf(fp, ".text %d\n", img->text_count);
  fprintf(fp, "; idx op a b c imm fimm target\n");
  for (int i = 0; i < img->text_count; ++i) write_instr(fp, i, &img->text[i]);

  fprintf(fp, "\n.data %lld\n", (long long) img->data_count);
  write_data(fp, img->data, img->data_count);

  fprintf(fp, "\n.symmap %d\n", img->sym_count);
  fprintf(fp, "; name value is_code\n");
  for (int i = 0; i < img->sym_count; ++i)
    fprintf(fp, "%s %lld %d\n", img->symmap[i].name,
            (long long) img->symmap[i].offset, img->symmap[i].is_code);

  fprintf(fp, "\n.end\n");
  fclose(fp);
  return 0;
}

int vx_read(const char* path, VImage* img, char* err, size_t errsz)
{
  memset(img, 0, sizeof(*img));
  FILE* fp = fopen(path, "r");
  if (!fp) { snprintf(err, errsz, "cannot open '%s'", path); return -1; }

  char line[512];
  if (!next_line(fp, line, sizeof line) || strncmp(line, "VX1", 3) != 0)
  {
    snprintf(err, errsz, "'%s': bad magic (expected VX1)", path);
    fclose(fp); return -1;
  }

  while (next_line(fp, line, sizeof line))
  {
    char tag[32]; long long count = 0;
    int got = sscanf(line, "%31s %lld", tag, &count);
    if (got < 1) continue;

    if (strcmp(tag, ".end") == 0) break;

    if (strcmp(tag, ".entry") == 0)
    {
      img->entry = count;
    }
    else if (strcmp(tag, ".text") == 0)
    {
      img->text_count = (int) count;
      for (int i = 0; i < img->text_count; ++i)
      {
        if (!next_line(fp, line, sizeof line) || !parse_instr(line, &img->text[i]))
        { snprintf(err, errsz, "'%s': bad .text row %d", path, i); goto fail; }
      }
    }
    else if (strcmp(tag, ".data") == 0)
    {
      if (read_data(fp, &img->data, count, err, errsz) != 0) goto fail;
      img->data_count = count;
    }
    else if (strcmp(tag, ".symmap") == 0)
    {
      for (int i = 0; i < (int) count; ++i)
      {
        if (!next_line(fp, line, sizeof line)) { snprintf(err, errsz, "'%s': truncated symmap", path); goto fail; }
        char name[64]; long long value; int isc;
        if (sscanf(line, "%63s %lld %d", name, &value, &isc) != 3)
        { snprintf(err, errsz, "'%s': bad symmap row %d", path, i); goto fail; }
        ObjSym* s = &img->symmap[img->sym_count++];
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->offset  = value;
        s->is_code = isc;
        s->binding = BIND_GLOBAL;
        s->section = isc ? RSEC_TEXT : RSEC_DATA;
      }
    }
  }

  fclose(fp);
  return 0;

fail:
  free(img->data);
  img->data = NULL;
  fclose(fp);
  return -1;
}

int64_t vx_load(const VImage* img, VCpu* cpu, Instr* prog, int* prog_len)
{
  memcpy(prog, img->text, img->text_count * sizeof(Instr));
  *prog_len = img->text_count;
  if (img->data_count > 0)
    memcpy(cpu->mem, img->data, (size_t) img->data_count);

  // Publish global symbols so --trace/--debug can use them.
  vcpu_labels_clear();
  for (int i = 0; i < img->sym_count; ++i)
    vcpu_label_define(img->symmap[i].name, img->symmap[i].offset, img->symmap[i].is_code);

  return img->entry;
}
