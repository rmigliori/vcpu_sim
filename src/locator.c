// ---------------------------------------------------------------------------
//  locator.c — il lettore del file di collocazione. Il perche' e la forma
//  stanno in cima a locator.h (29/09/2026, la tappa 3).
//
//  Un tokenizzatore piatto e una discesa ricorsiva di tre produzioni: non c'e'
//  espressione da valutare, quindi non c'e' precedenza da gestire. I numeri si
//  leggono con strtoll in base 0, cosi' 0x200000 e 2097152 sono la stessa cosa.
// ---------------------------------------------------------------------------
#include "locator.h"
#include "devices.h"   // la mappa vera della scheda: SHARED_BASE, SHARED_SIZE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOC_MAX_SRC  (64 * 1024)
#define LOC_MAX_TOK  2048

typedef struct
{
  char text[LOC_NAME];
  int  line;
} LocTok;

typedef struct
{
  LocTok      tok[LOC_MAX_TOK];
  int         count;
  int         at;
  const char* origin;     // il nome del file, per i messaggi
  char*       err;
  size_t      errsz;
} LocParse;

// Un errore dice SEMPRE dove: file, riga. Un file di collocazione sbagliato si
// corregge a mano, e senza la riga si cerca a occhio.
static int loc_fail(LocParse* p, int line, const char* fmt, ...)
{
  char msg[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  if (line > 0) snprintf(p->err, p->errsz, "%s:%d: %s", p->origin, line, msg);
  else          snprintf(p->err, p->errsz, "%s: %s", p->origin, msg);
  return -1;
}

static int loc_is_word(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

// La virgola si salta come uno spazio: fra ORIGIN e LENGTH separa e non
// significa, ed e' li' perche' i linker script la mettono.
static int loc_lex(const char* src, LocParse* p)
{
  int line = 1;
  for (const char* s = src; *s; )
  {
    if (*s == '\n') { line += 1; s += 1; continue; }
    if (*s == ' ' || *s == '\t' || *s == '\r' || *s == ',') { s += 1; continue; }
    if (*s == '#')
    {
      while (*s && *s != '\n') s += 1;
      continue;
    }
    if (p->count >= LOC_MAX_TOK) return loc_fail(p, line, "file troppo grande");
    LocTok* t = &p->tok[p->count++];
    t->line = line;
    if (loc_is_word(*s))
    {
      size_t n = 0;
      while (loc_is_word(*s) && n + 1 < sizeof t->text) t->text[n++] = *s++;
      t->text[n] = 0;
      if (loc_is_word(*s)) return loc_fail(p, line, "nome troppo lungo");
    }
    else
    {
      t->text[0] = *s++;
      t->text[1] = 0;
    }
  }
  return 0;
}

static const char* loc_peek(LocParse* p)
{
  return p->at < p->count ? p->tok[p->at].text : "";
}

static int loc_line(LocParse* p)
{
  if (p->count == 0) return 0;
  return p->at < p->count ? p->tok[p->at].line : p->tok[p->count - 1].line;
}

static const char* loc_next(LocParse* p)
{
  return p->at < p->count ? p->tok[p->at++].text : "";
}

static int loc_eat(LocParse* p, const char* want)
{
  if (strcmp(loc_peek(p), want) != 0)
    return loc_fail(p, loc_line(p), "atteso '%s', trovato '%s'", want,
                    p->at < p->count ? loc_peek(p) : "la fine del file");
  p->at += 1;
  return 0;
}

// 'NOME = numero', dove NOME e' ORIGIN o LENGTH.
static int loc_keyed_number(LocParse* p, const char* key, int64_t* out)
{
  if (loc_eat(p, key) != 0) return -1;
  if (loc_eat(p, "=") != 0) return -1;
  int line = loc_line(p);
  const char* t = loc_next(p);
  char* end = NULL;
  long long v = strtoll(t, &end, 0);
  if (!*t || !end || *end) return loc_fail(p, line, "'%s' non e' un numero", t);
  *out = (int64_t) v;
  return 0;
}

// MEMORY { nome : CODE|DATA ORIGIN = n LENGTH = n ... }
static int loc_memory(LocParse* p, LocScope* s)
{
  if (loc_eat(p, "{") != 0) return -1;
  while (strcmp(loc_peek(p), "}") != 0)
  {
    if (p->at >= p->count) return loc_eat(p, "}");
    int line = loc_line(p);
    if (s->region_count >= LOC_MAX_REGIONS)
      return loc_fail(p, line, "troppe regioni (max %d)", LOC_MAX_REGIONS);
    LocRegion r;
    memset(&r, 0, sizeof r);
    snprintf(r.name, sizeof r.name, "%s", loc_next(p));
    if (loc_eat(p, ":") != 0) return -1;

    int uline = loc_line(p);
    const char* u = loc_next(p);
    if      (strcmp(u, "CODE") == 0) r.unit = LOC_CODE;
    else if (strcmp(u, "DATA") == 0) r.unit = LOC_DATA;
    else return loc_fail(p, uline, "unita' '%s': serve CODE (istruzioni) o DATA (byte)", u);

    if (loc_keyed_number(p, "ORIGIN", &r.origin) != 0) return -1;
    if (loc_keyed_number(p, "LENGTH", &r.length) != 0) return -1;

    if (r.length <= 0)
      return loc_fail(p, line, "la regione '%s' ha LENGTH %lld", r.name, (long long) r.length);
    // L'origine del codice: il PC indicizza prog[] da zero (vedi locator.h).
    if (r.unit == LOC_CODE && r.origin != 0)
      return loc_fail(p, line, "la regione CODE '%s' ha ORIGIN %lld: puo' essere solo 0, "
                               "perche' il contatore di programma indicizza prog[] da zero",
                      r.name, (long long) r.origin);
    // Il guard del NULL: all'indirizzo 0 non si mette un dato.
    if (r.unit == LOC_DATA && r.origin == 0)
      return loc_fail(p, line, "la regione DATA '%s' ha ORIGIN 0: l'indirizzo 0 resta NULL, "
                               "o un dato non si distingue da un puntatore nullo", r.name);
    for (int i = 0; i < s->region_count; ++i)
      if (strcmp(s->regions[i].name, r.name) == 0)
        return loc_fail(p, line, "la regione '%s' e' dichiarata due volte", r.name);
    s->regions[s->region_count++] = r;
  }
  return loc_eat(p, "}");
}

// SECTIONS { .nome > REGIONE ... }
static int loc_sections(LocParse* p, LocScope* s)
{
  if (loc_eat(p, "{") != 0) return -1;
  while (strcmp(loc_peek(p), "}") != 0)
  {
    if (p->at >= p->count) return loc_eat(p, "}");
    int line = loc_line(p);
    if (s->place_count >= LOC_MAX_PLACE)
      return loc_fail(p, line, "troppe sezioni (max %d)", LOC_MAX_PLACE);
    LocPlace pl;
    memset(&pl, 0, sizeof pl);
    snprintf(pl.name, sizeof pl.name, "%s", loc_next(p));
    if (pl.name[0] != '.')
      return loc_fail(p, line, "'%s': il nome di una sezione comincia con un punto", pl.name);
    if (loc_eat(p, ">") != 0) return -1;
    snprintf(pl.region, sizeof pl.region, "%s", loc_next(p));
    for (int i = 0; i < s->place_count; ++i)
      if (strcmp(s->places[i].name, pl.name) == 0)
        return loc_fail(p, line, "la sezione '%s' e' collocata due volte", pl.name);
    s->places[s->place_count++] = pl;
  }
  return loc_eat(p, "}");
}

// Le due produzioni che stanno sia nella scheda sia in un blocco PROCESSOR.
static int loc_scope_item(LocParse* p, LocScope* s, int* handled)
{
  *handled = 1;
  if (strcmp(loc_peek(p), "MEMORY") == 0)   { p->at += 1; return loc_memory(p, s); }
  if (strcmp(loc_peek(p), "SECTIONS") == 0) { p->at += 1; return loc_sections(p, s); }
  *handled = 0;
  return 0;
}

int loc_parse(const char* text, const char* origin, Locator* loc,
              char* err, size_t errsz)
{
  memset(loc, 0, sizeof *loc);
  LocParse p;
  memset(&p, 0, sizeof p);
  p.origin = origin;
  p.err    = err;
  p.errsz  = errsz;
  if (loc_lex(text, &p) != 0) return -1;

  while (p.at < p.count)
  {
    int handled = 0;
    if (loc_scope_item(&p, &loc->board, &handled) != 0) return -1;
    if (handled) continue;

    if (strcmp(loc_peek(&p), "PROCESSOR") == 0)
    {
      int line = loc_line(&p);
      p.at += 1;
      if (loc->proc_count >= LOC_MAX_PROC)
        return loc_fail(&p, line, "troppi processori (max %d)", LOC_MAX_PROC);
      LocScope* ps = &loc->proc[loc->proc_count];
      memset(ps, 0, sizeof *ps);
      snprintf(ps->name, sizeof ps->name, "%s", loc_next(&p));
      for (int i = 0; i < loc->proc_count; ++i)
        if (strcmp(loc->proc[i].name, ps->name) == 0)
          return loc_fail(&p, line, "il processore '%s' e' dichiarato due volte", ps->name);
      loc->proc_count += 1;
      if (loc_eat(&p, "{") != 0) return -1;
      while (strcmp(loc_peek(&p), "}") != 0)
      {
        if (p.at >= p.count) return loc_eat(&p, "}");
        int inner = 0;
        if (loc_scope_item(&p, ps, &inner) != 0) return -1;
        if (!inner)
          return loc_fail(&p, loc_line(&p), "dentro PROCESSOR '%s' ci vanno MEMORY e SECTIONS, "
                                            "non '%s'", ps->name, loc_peek(&p));
      }
      if (loc_eat(&p, "}") != 0) return -1;
      continue;
    }
    return loc_fail(&p, loc_line(&p), "atteso MEMORY, SECTIONS o PROCESSOR, trovato '%s'",
                    loc_peek(&p));
  }
  if (loc->board.region_count == 0 && loc->proc_count == 0)
    return loc_fail(&p, 0, "nessuna regione di memoria dichiarata");
  return 0;
}

int loc_read(const char* path, Locator* loc, char* err, size_t errsz)
{
  FILE* fp = fopen(path, "r");
  if (!fp) { snprintf(err, errsz, "cannot read '%s'", path); return -1; }
  char* buf = malloc(LOC_MAX_SRC);
  if (!buf) { fclose(fp); snprintf(err, errsz, "out of memory"); return -1; }
  size_t n = fread(buf, 1, LOC_MAX_SRC - 1, fp);
  int too_big = !feof(fp);
  fclose(fp);
  buf[n] = 0;
  if (too_big)
  {
    free(buf);
    snprintf(err, errsz, "%s: file troppo grande (max %d byte)", path, LOC_MAX_SRC - 1);
    return -1;
  }
  int rc = loc_parse(buf, path, loc, err, errsz);
  free(buf);
  return rc;
}

const LocRegion* loc_region(const LocScope* s, const char* name)
{
  for (int i = 0; i < s->region_count; ++i)
    if (strcmp(s->regions[i].name, name) == 0) return &s->regions[i];
  return NULL;
}

const LocRegion* loc_section_region(const LocScope* s, const char* section)
{
  for (int i = 0; i < s->place_count; ++i)
    if (strcmp(s->places[i].name, section) == 0)
      return loc_region(s, s->places[i].region);
  return NULL;
}

// Le due estremita' di una regione, nella sua unita'.
static int64_t loc_end(const LocRegion* r) { return r->origin + r->length; }

int loc_resolve(const Locator* loc, const char* cpu, LocScope* out,
                char* err, size_t errsz)
{
  *out = loc->board;
  out->name[0] = 0;

  if (cpu && *cpu)
  {
    const LocScope* ps = NULL;
    for (int i = 0; i < loc->proc_count; ++i)
      if (strcmp(loc->proc[i].name, cpu) == 0) { ps = &loc->proc[i]; break; }
    if (!ps)
    {
      // Nessun blocco vale "come la scheda" (locator.h, l'eredita'), ma un nome
      // SCRITTO e non trovato e' un errore: e' un blocco che si credeva di avere.
      if (loc->proc_count > 0)
      {
        snprintf(err, errsz, "processore '%s' non dichiarato nel file di collocazione", cpu);
        return -1;
      }
    }
    else
    {
      snprintf(out->name, sizeof out->name, "%s", cpu);
      // Un blocco dice solo cio' che DIFFERISCE: stesso nome = sostituzione,
      // nome nuovo = aggiunta.
      for (int i = 0; i < ps->region_count; ++i)
      {
        int j = 0;
        for (; j < out->region_count; ++j)
          if (strcmp(out->regions[j].name, ps->regions[i].name) == 0) break;
        if (j == out->region_count)
        {
          if (out->region_count >= LOC_MAX_REGIONS)
          { snprintf(err, errsz, "troppe regioni per '%s'", cpu); return -1; }
          out->region_count += 1;
        }
        out->regions[j] = ps->regions[i];
      }
      for (int i = 0; i < ps->place_count; ++i)
      {
        int j = 0;
        for (; j < out->place_count; ++j)
          if (strcmp(out->places[j].name, ps->places[i].name) == 0) break;
        if (j == out->place_count)
        {
          if (out->place_count >= LOC_MAX_PLACE)
          { snprintf(err, errsz, "troppe sezioni per '%s'", cpu); return -1; }
          out->place_count += 1;
        }
        out->places[j] = ps->places[i];
      }
    }
  }

  // I controlli che vogliono la vista intera, cioe' dopo l'eredita'.
  for (int i = 0; i < out->place_count; ++i)
    if (!loc_region(out, out->places[i].region))
    {
      snprintf(err, errsz, "la sezione '%s' va nella regione '%s', che non e' dichiarata",
               out->places[i].name, out->places[i].region);
      return -1;
    }
  for (int i = 0; i < out->region_count; ++i)
    for (int j = i + 1; j < out->region_count; ++j)
    {
      const LocRegion* a = &out->regions[i];
      const LocRegion* b = &out->regions[j];
      if (a->unit != b->unit) continue;   // spazi diversi: non si sovrappongono
      if (a->origin < loc_end(b) && b->origin < loc_end(a))
      {
        snprintf(err, errsz, "le regioni '%s' e '%s' si sovrappongono", a->name, b->name);
        return -1;
      }
    }
  return 0;
}

// Il layout di sempre, nella lingua del file -- e i numeri NON sono ricopiati:
// vengono dalle costanti della macchina (MAX_INSTR, NULL_GUARD, MEM_SIZE,
// SHARED_BASE, SHARED_SIZE), cosi' il file incorporato non e' una seconda
// dichiarazione della mappa di memoria, e' quella.
const char* loc_builtin(void)
{
  static char buf[1024];
  if (!buf[0])
    snprintf(buf, sizeof buf,
      "# Il file di collocazione INCORPORATO: il layout di sempre, ed e' cio' che\n"
      "# `ld` usa quando nessuno passa -T. Si stampa con `ld --verbose`, e i suoi\n"
      "# numeri vengono dalle costanti della macchina.\n"
      "MEMORY\n"
      "{\n"
      "  PM     : CODE  ORIGIN = 0,          LENGTH = %d            # in ISTRUZIONI\n"
      "  LOCAL  : DATA  ORIGIN = %d,          LENGTH = 0x%llX      # la RAM locale, meno il guard\n"
      "  SHARED : DATA  ORIGIN = 0x%llX,   LENGTH = 0x%llX      # la RAM CONDIVISA\n"
      "}\n"
      "\n"
      "SECTIONS\n"
      "{\n"
      "  .text   > PM\n"
      "  .data   > LOCAL\n"
      "  .shared > SHARED\n"
      "}\n",
      MAX_INSTR, NULL_GUARD, (unsigned long long) (MEM_SIZE - NULL_GUARD),
      (unsigned long long) SHARED_BASE, (unsigned long long) SHARED_SIZE);
  return buf;
}
