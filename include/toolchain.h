#ifndef TOOLCHAIN_H
#define TOOLCHAIN_H

#include "vcpu.h"
#include "locator.h"
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
//  Multi-file toolchain: object (.vo) and executable (.vx) model.
//  See docs/toolchain-spec.md for the on-disk text formats.
// ---------------------------------------------------------------------------

typedef enum { BIND_LOCAL, BIND_GLOBAL, BIND_EXTERN } Binding;
// RSEC_SHARED: la RAM CONDIVISA (29/09/2026, la tappa 3). Due differenze dalle
// altre due, e sono le regole decise prima di scriverla:
//   - NON SI INIZIALIZZA. Due immagini che portassero entrambe il valore
//     iniziale lo scriverebbero due volte, e su hardware vero l'ordine dei due
//     caricamenti deciderebbe chi vince. Chi ha bisogno di un valore lo scrive
//     in codice da una CPU, e l'altra aspetta un flag -- e' cio' che fa
//     test_amp, ed e' la pratica dell'AMP vera. Quindi .shared RISERVA e basta:
//     .word dentro .shared e' un errore di assemblaggio
//   - UN SIMBOLO CONDIVISO E' GLOBALE PER DEFINIZIONE (dell'utente). La sezione
//     dice gia' che il dato sta nella RAM che l'altra CPU vede; pretendere
//     anche il .global sarebbe dichiarare due volte lo stesso fatto, e un
//     simbolo in .shared invisibile fuori dal suo oggetto e' una
//     contraddizione nei termini, non solo una cosa inutile
typedef enum { RSEC_TEXT = 0, RSEC_DATA = 1, RSEC_SHARED = 2, RSEC_NONE = -1 } RSection;
// R_ADDR: address-of a symbol into imm; the linker picks the value by is_code
// (instruction index for code, byte address for data) -> works for both kinds.
typedef enum { R_CODE, R_DATA, R_ADDR } RelocType;

// A symbol as it appears in an object's symbol table.
typedef struct
{
  char    name[64];
  int     section;   // RSection; RSEC_NONE for undefined (extern)
  int64_t offset;    // instruction index (text) or byte offset (data/shared)
  // Quanto occupa: istruzioni nel testo, byte nei dati. Lo calcola l'assembler
  // come distanza dal simbolo SEGUENTE nella stessa sezione, chiusa alla fine
  // della sezione -- che e' un dato che solo lui ha. Dedurlo dopo, per
  // differenza, sbaglia sistematicamente sull'ultimo simbolo. Due etichette
  // sullo stesso indirizzo (un alias) hanno la stessa dimensione.
  int64_t size;
  int     binding;   // Binding
  int     is_code;   // 1 code, 0 data, -1 unknown (extern)
} ObjSym;

// A relocation: patch prog[site].<field> with value(sym)+addend at link time.
typedef struct
{
  int     type;      // RelocType: R_CODE -> target, R_DATA/R_ADDR -> imm
  int     site;      // instruction index within this object
  char    sym[64];
  int64_t addend;
} ObjReloc;

// A relocatable object produced by the assembler.
typedef struct
{
  Instr    text[MAX_INSTR];
  int      text_count;
  uint8_t* data;         // heap; size == data_count (NULL if empty)
  int64_t  data_count;
  int64_t  shared_count; // byte RISERVATI in .shared: nessun contenuto (vedi RSEC_SHARED)
  ObjSym   syms[MAX_SYMBOLS];
  int      sym_count;
  ObjReloc relocs[MAX_INSTR];
  int      reloc_count;
} VObject;

// A fully linked executable image.
typedef struct
{
  Instr    text[MAX_INSTR];
  int      text_count;
  uint8_t* data;         // heap; size == data_count (NULL if empty)
  int64_t  data_count;
  int64_t  shared_base;  // dove comincia la RAM CONDIVISA per questa immagine
  int64_t  shared_count; // byte riservati: non c'e' contenuto da caricare
  ObjSym   symmap[MAX_SYMBOLS];  // globals only, resolved (value in .offset)
  int      sym_count;
  int64_t  entry;        // entry instruction index
} VImage;

// --- assembler.c -----------------------------------------------------------
// Assemble a .vasm source into a relocatable object. Returns 0 on success,
// -1 on error (message in 'err'). 'obj->data' is heap-allocated.
// 'expanded_out': if non-NULL, writes the fully macro-expanded text-section
// listing there (see dump_expanded() in assembler.c) right after pass 1 —
// even if pass 2 (encoding) later fails, so it stays useful for debugging.
int  assemble_object(const char* path, VObject* obj, const char* expanded_out,
                      char* err, size_t errsz);
void vobject_free(VObject* obj);

// .include search path (-I), shared by assemble() and assemble_object():
// a name that is not found next to the top-level source is looked up in these
// directories, in the order they were added. Returns 0, or -1 if full.
int  asm_add_include_dir(const char* dir);
void asm_clear_include_dirs(void);

// Conditional assembly (-D), shared by the same two entry points: the set of
// names that .ifdef/.ifndef ask about. PRESENCE only — a name is defined or it
// is not, it never carries a value, and .equ constants are NOT in this set (see
// the header over the conditionals in assembler.c). Returns 0, or -1 if full.
int  asm_add_define(const char* name);
void asm_clear_defines(void);

// --- toolchain.c -----------------------------------------------------------
int  vo_write(const char* path, const VObject* obj, char* err, size_t errsz);
int  vo_read(const char* path, VObject* obj, char* err, size_t errsz);

// Archive (.va): a bundle of objects with selective linking.
int  va_write(const char* path, const char* const* member_paths, int nmemb,
              char* err, size_t errsz);
int  va_read(const char* path, VObject* objs, char names[][64], int* count,
             int maxobj, char* err, size_t errsz);

// Link 'nobj' objects (in command order) into an image, placing each section in
// the region the locator gives it. Returns 0 or -1.
int  link_objects(const VObject* const* objs, int nobj, VImage* img,
                  const char* entry_name, const LocScope* loc,
                  char* err, size_t errsz);
void vimage_free(VImage* img);

int  vx_write(const char* path, const VImage* img, char* err, size_t errsz);
int  vx_read(const char* path, VImage* img, char* err, size_t errsz);

// La MAPPA: dove e' finito ogni simbolo, quanto occupa, e quanto resta di ogni
// regione. Le mappe vere hanno la sezione e la dimensione, e questa le ha.
int  map_write(const char* path, const VImage* img, const LocScope* loc,
               char* err, size_t errsz);

// Le due immagini si mettono d'accordo? Confronta il sottoinsieme CONDIVISO dei
// due symmap: stessi nomi, stessi indirizzi, stesse dimensioni, nello stesso
// ordine. L'accordo viene dal file di collocazione letto da tutti e due i link,
// e questo e' il controllo che dice se e' davvero cosi' -- al caricamento,
// invece di presentarsi come un dato che si corrompe.
int  shared_agree(const VImage* a, const VImage* b, const char* na, const char* nb,
                  char* err, size_t errsz);

// Load a linked image: fills prog[], copies data into 'mem' (la memoria della
// scheda), sets *prog_len, returns the entry instruction index.
int64_t vx_load(const VImage* img, uint8_t* mem, Instr* prog, int* prog_len);

#endif // TOOLCHAIN_H
