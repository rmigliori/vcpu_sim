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
  return s == RSEC_TEXT ? "text" : s == RSEC_DATA ? "data"
       : s == RSEC_SHARED ? "shared" : "----";
}

static int rsec_parse(const char* t)
{
  if (strcmp(t, "text") == 0) return RSEC_TEXT;
  if (strcmp(t, "data") == 0) return RSEC_DATA;
  if (strcmp(t, "shared") == 0) return RSEC_SHARED;
  return RSEC_NONE;
}

// Il nome della sezione come lo scrive un sorgente, per i messaggi e la mappa.
static const char* rsec_dot(int s)
{
  return s == RSEC_TEXT ? ".text" : s == RSEC_DATA ? ".data"
       : s == RSEC_SHARED ? ".shared" : "(nessuna)";
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

  if (obj->shared_count > 0)
    fprintf(fp, "\n.shared %lld\n", (long long) obj->shared_count);

  fprintf(fp, "\n.symtab %d\n", obj->sym_count);
  fprintf(fp, "; name section offset size binding is_code\n");
  for (int i = 0; i < obj->sym_count; ++i)
  {
    const ObjSym* s = &obj->syms[i];
    if (s->section == RSEC_NONE)
      fprintf(fp, "%s ---- - - %s -\n", s->name, bind_name(s->binding));
    else
      fprintf(fp, "%s %s %lld %lld %s %d\n", s->name, rsec_name(s->section),
              (long long) s->offset, (long long) s->size,
              bind_name(s->binding), s->is_code);
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
    else if (strcmp(tag, ".shared") == 0)
    {
      obj->shared_count = count;
    }
    else if (strcmp(tag, ".symtab") == 0)
    {
      for (int i = 0; i < (int) count; ++i)
      {
        if (!next_line(fp, line, sizeof line)) { snprintf(err, errsz, "'%s': truncated symtab", path); goto fail; }
        char name[64], sec[16], bind[16], off[32], sz[32], isc[8];
        if (sscanf(line, "%63s %15s %31s %31s %15s %7s", name, sec, off, sz, bind, isc) != 6)
        { snprintf(err, errsz, "'%s': bad symtab row %d", path, i); goto fail; }
        ObjSym* s = &obj->syms[obj->sym_count++];
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->section = rsec_parse(sec);
        s->offset  = (off[0] == '-') ? 0 : strtoll(off, NULL, 10);
        s->size    = (sz[0]  == '-') ? 0 : strtoll(sz,  NULL, 10);
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
                 const char* entry_name, const LocScope* loc,
                 char* err, size_t errsz)
{
  memset(img, 0, sizeof(*img));

  // 1. Assign per-module bases (command order) INSIDE the region each section
  // is placed in. Until the 29/09 the two bases were cabled here -- code from
  // 0, data from NULL_GUARD -- and neither the 1 MiB limit of the data nor the
  // reason for the guard was written anywhere a person would look. Now they are
  // facts of the memory map, and this reads them (see locator.h).
  const LocRegion* rtext = loc_section_region(loc, ".text");
  const LocRegion* rdata = loc_section_region(loc, ".data");
  if (!rtext) { snprintf(err, errsz, "nessuna regione per .text nel file di collocazione"); return -1; }
  if (!rdata) { snprintf(err, errsz, "nessuna regione per .data nel file di collocazione"); return -1; }
  if (rtext->unit != LOC_CODE)
  { snprintf(err, errsz, ".text va in '%s', che e' una regione DATA", rtext->name); return -1; }
  if (rdata->unit != LOC_DATA)
  { snprintf(err, errsz, ".data va in '%s', che e' una regione CODE", rdata->name); return -1; }

  // La regione condivisa serve solo se qualcuno ci mette qualcosa: un programma
  // di una CPU sola non deve dichiararla.
  int64_t shared_total = 0;
  for (int m = 0; m < nobj; ++m) shared_total += objs[m]->shared_count;
  const LocRegion* rshared = loc_section_region(loc, ".shared");
  if (shared_total > 0 && !rshared)
  { snprintf(err, errsz, "c'e' una .shared ma il file di collocazione non le da' una regione"); return -1; }
  if (rshared && rshared->unit != LOC_DATA)
  { snprintf(err, errsz, ".shared va in '%s', che e' una regione CODE", rshared->name); return -1; }

  int64_t text_base[64], data_base[64], shared_base[64];
  if (nobj > 64) { snprintf(err, errsz, "too many objects (max 64)"); return -1; }
  int64_t tcur = rtext->origin, dcur = rdata->origin;
  int64_t scur = rshared ? rshared->origin : 0;
  for (int m = 0; m < nobj; ++m)
  {
    text_base[m] = tcur; tcur += objs[m]->text_count;
    data_base[m] = dcur; dcur += objs[m]->data_count;
    shared_base[m] = scur; scur += objs[m]->shared_count;
  }
  if (rshared && scur > rshared->origin + rshared->length)
  {
    snprintf(err, errsz, "la regione '%s' trabocca: %lld byte condivisi, ne tiene %lld",
             rshared->name, (long long) (scur - rshared->origin), (long long) rshared->length);
    return -1;
  }
  img->shared_base  = rshared ? rshared->origin : 0;
  img->shared_count = shared_total;
  // Il traboccamento di una regione e' un errore del link, non una scrittura
  // oltre il bordo. Per i dati non era controllato affatto: la somma dei moduli
  // non la guardava nessuno, e vx_load copia data_count byte in un buffer di
  // MEM_SIZE.
  if (tcur > rtext->origin + rtext->length)
  {
    snprintf(err, errsz, "la regione '%s' trabocca: %lld istruzioni, ne tiene %lld",
             rtext->name, (long long) (tcur - rtext->origin), (long long) rtext->length);
    return -1;
  }
  if (dcur > rdata->origin + rdata->length)
  {
    snprintf(err, errsz, "la regione '%s' trabocca: %lld byte di dati, ne tiene %lld",
             rdata->name, (long long) (dcur - rdata->origin), (long long) rdata->length);
    return -1;
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
      g->offset  = (s->section == RSEC_TEXT   ? text_base[m]
                  : s->section == RSEC_SHARED ? shared_base[m]
                                              : data_base[m]) + s->offset;
      g->size    = s->size;
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
        value = (d->section == RSEC_TEXT   ? text_base[m]
               : d->section == RSEC_SHARED ? shared_base[m]
                                           : data_base[m]) + d->offset;
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

  if (img->shared_count > 0)
    fprintf(fp, "\n.shared %lld %lld\n",
            (long long) img->shared_base, (long long) img->shared_count);

  // La SEZIONE e la DIMENSIONE, dal 29/09 (la tappa 3). La sezione perche' un
  // simbolo condiviso va distinto da uno locale, e dedurlo dall'indirizzo
  // (>= la base della RAM condivisa) sarebbe una convenzione che nessuno
  // garantisce; la dimensione perche' e' cio' che una mappa deve dire.
  fprintf(fp, "\n.symmap %d\n", img->sym_count);
  fprintf(fp, "; name section value size is_code\n");
  for (int i = 0; i < img->sym_count; ++i)
    fprintf(fp, "%s %s %lld %lld %d\n", img->symmap[i].name,
            rsec_name(img->symmap[i].section),
            (long long) img->symmap[i].offset,
            (long long) img->symmap[i].size, img->symmap[i].is_code);

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
    else if (strcmp(tag, ".shared") == 0)
    {
      long long base = 0, n = 0;
      if (sscanf(line, "%31s %lld %lld", tag, &base, &n) != 3)
      { snprintf(err, errsz, "'%s': bad .shared row", path); goto fail; }
      img->shared_base  = base;
      img->shared_count = n;
    }
    else if (strcmp(tag, ".symmap") == 0)
    {
      for (int i = 0; i < (int) count; ++i)
      {
        if (!next_line(fp, line, sizeof line)) { snprintf(err, errsz, "'%s': truncated symmap", path); goto fail; }
        char name[64], sec[16]; long long value, size; int isc;
        if (sscanf(line, "%63s %15s %lld %lld %d", name, sec, &value, &size, &isc) != 5)
        { snprintf(err, errsz, "'%s': bad symmap row %d", path, i); goto fail; }
        ObjSym* s = &img->symmap[img->sym_count++];
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->offset  = value;
        s->size    = size;
        s->is_code = isc;
        s->binding = BIND_GLOBAL;
        s->section = rsec_parse(sec);
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

// ===========================================================================
//  LA MAPPA (29/09/2026, la tappa 3)
//
//  Cio' che una mappa deve dire, e nelle mappe vere c'e': in quale REGIONE sta
//  ogni cosa, quanto ne resta, e per ogni simbolo la SEZIONE, l'indirizzo e la
//  DIMENSIONE. Le due colonne che il .vx ha guadagnato lo stesso giorno sono
//  esattamente queste due: la mappa non calcola niente per conto suo, riporta.
//
//  Gli indirizzi sono nell'unita' della regione -- istruzioni nel codice, byte
//  nei dati -- e la colonna lo dice, perche' su una macchina Harvard
//  l'istruzione 8 e il byte 8 non sono lo stesso posto.
// ===========================================================================
static int64_t region_used(const VImage* img, const LocRegion* r, int section)
{
  if (section == RSEC_TEXT)   return img->text_count - r->origin;
  if (section == RSEC_SHARED) return img->shared_count;
  return img->data_count - r->origin;
}

int map_write(const char* path, const VImage* img, const LocScope* loc,
              char* err, size_t errsz)
{
  FILE* fp = fopen(path, "w");
  if (!fp) { snprintf(err, errsz, "cannot write '%s'", path); return -1; }

  fprintf(fp, "# la mappa di '%s'\n\n", path);
  fprintf(fp, "REGIONI\n");
  fprintf(fp, "%-10s %-6s %10s %10s %10s %10s\n",
          "regione", "unita'", "origine", "capienza", "usato", "libero");
  for (int i = 0; i < loc->place_count; ++i)
  {
    const LocRegion* r = loc_region(loc, loc->places[i].region);
    if (!r) continue;
    int sec = rsec_parse(loc->places[i].name + 1);   // ".data" -> "data"
    int64_t used = sec == RSEC_NONE ? 0 : region_used(img, r, sec);
    if (used < 0) used = 0;
    fprintf(fp, "%-10s %-6s %10lld %10lld %10lld %10lld   %s\n",
            r->name, r->unit == LOC_CODE ? "istr" : "byte",
            (long long) r->origin, (long long) r->length,
            (long long) used, (long long) (r->length - used),
            loc->places[i].name);
  }

  fprintf(fp, "\nSIMBOLI\n");
  fprintf(fp, "%-8s %12s %8s  %s\n", "sezione", "indirizzo", "dim", "nome");
  // Per sezione, e dentro la sezione per indirizzo: una mappa si legge
  // scorrendo la memoria, non l'ordine in cui il linker ha incontrato i nomi.
  const int order[3] = { RSEC_TEXT, RSEC_DATA, RSEC_SHARED };
  for (int k = 0; k < 3; ++k)
  {
    // Selezione per indirizzo crescente, O(n^2) su n <= 512: la mappa si scrive
    // una volta per link, e un ordinamento in piu' non merita una struttura.
    int64_t prev = INT64_MIN;
    for (;;)
    {
      const ObjSym* best = NULL;
      for (int i = 0; i < img->sym_count; ++i)
      {
        const ObjSym* s = &img->symmap[i];
        if (s->section != order[k]) continue;
        if (s->offset <= prev) continue;
        if (!best || s->offset < best->offset) best = s;
      }
      if (!best) break;
      prev = best->offset;
      // Gli alias -- piu' nomi sullo stesso indirizzo -- si stampano tutti.
      for (int i = 0; i < img->sym_count; ++i)
      {
        const ObjSym* s = &img->symmap[i];
        if (s->section != order[k] || s->offset != prev) continue;
        // Dimensione 0 = NON SI SA, e si stampa "-": un'etichetta di codice che
        // non sia una procedura dichiarata non ha una fine, e un numero al suo
        // posto sarebbe l'estensione di un blocco spacciata per una funzione.
        char dim[24];
        if (s->size > 0) snprintf(dim, sizeof dim, "%lld", (long long) s->size);
        else             snprintf(dim, sizeof dim, "-");
        fprintf(fp, "%-8s %12lld %8s  %s\n", rsec_dot(s->section),
                (long long) s->offset, dim, s->name);
      }
    }
  }

  fprintf(fp, "\n.entry %lld\n", (long long) img->entry);
  fclose(fp);
  return 0;
}

int shared_agree(const VImage* a, const VImage* b, const char* na, const char* nb,
                 char* err, size_t errsz)
{
  const ObjSym* sa[MAX_SYMBOLS]; int n_a = 0;
  const ObjSym* sb[MAX_SYMBOLS]; int n_b = 0;
  for (int i = 0; i < a->sym_count; ++i)
    if (a->symmap[i].section == RSEC_SHARED) sa[n_a++] = &a->symmap[i];
  for (int i = 0; i < b->sym_count; ++i)
    if (b->symmap[i].section == RSEC_SHARED) sb[n_b++] = &b->symmap[i];

  if (n_a != n_b)
  {
    snprintf(err, errsz, "'%s' dichiara %d variabili condivise, '%s' ne dichiara %d: "
                         "le due immagini non vengono dallo stesso file di collocazione",
             na, n_a, nb, n_b);
    return -1;
  }
  for (int i = 0; i < n_a; ++i)
  {
    if (strcmp(sa[i]->name, sb[i]->name) != 0)
    {
      snprintf(err, errsz, "la variabile condivisa %d si chiama '%s' in '%s' e '%s' in '%s'",
               i, sa[i]->name, na, sb[i]->name, nb);
      return -1;
    }
    if (sa[i]->offset != sb[i]->offset || sa[i]->size != sb[i]->size)
    {
      snprintf(err, errsz, "la variabile condivisa '%s' e' a %lld (%lld byte) in '%s' "
                           "e a %lld (%lld byte) in '%s'",
               sa[i]->name, (long long) sa[i]->offset, (long long) sa[i]->size, na,
               (long long) sb[i]->offset, (long long) sb[i]->size, nb);
      return -1;
    }
  }
  return 0;
}

int64_t vx_load(const VImage* img, uint8_t* mem, Instr* prog, int* prog_len)
{
  memcpy(prog, img->text, img->text_count * sizeof(Instr));
  *prog_len = img->text_count;
  if (img->data_count > 0)
    memcpy(mem, img->data, (size_t) img->data_count);

  // Publish global symbols so --trace/--debug can use them.
  vcpu_labels_clear();
  for (int i = 0; i < img->sym_count; ++i)
    vcpu_label_define(img->symmap[i].name, img->symmap[i].offset, img->symmap[i].is_code);

  return img->entry;
}
