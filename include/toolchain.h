#ifndef TOOLCHAIN_H
#define TOOLCHAIN_H

#include "vcpu.h"
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
//  Multi-file toolchain: object (.vo) and executable (.vx) model.
//  See docs/toolchain-spec.md for the on-disk text formats.
// ---------------------------------------------------------------------------

typedef enum { BIND_LOCAL, BIND_GLOBAL, BIND_EXTERN } Binding;
typedef enum { RSEC_TEXT = 0, RSEC_DATA = 1, RSEC_NONE = -1 } RSection;
// R_ADDR: address-of a symbol into imm; the linker picks the value by is_code
// (instruction index for code, byte address for data) -> works for both kinds.
typedef enum { R_CODE, R_DATA, R_ADDR } RelocType;

// A symbol as it appears in an object's symbol table.
typedef struct
{
  char    name[64];
  int     section;   // RSection; RSEC_NONE for undefined (extern)
  int64_t offset;    // instruction index (text) or byte offset (data)
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

// --- toolchain.c -----------------------------------------------------------
int  vo_write(const char* path, const VObject* obj, char* err, size_t errsz);
int  vo_read(const char* path, VObject* obj, char* err, size_t errsz);

// Archive (.va): a bundle of objects with selective linking.
int  va_write(const char* path, const char* const* member_paths, int nmemb,
              char* err, size_t errsz);
int  va_read(const char* path, VObject* objs, char names[][64], int* count,
             int maxobj, char* err, size_t errsz);

// Link 'nobj' objects (in command order) into an image. Returns 0 or -1.
int  link_objects(const VObject* const* objs, int nobj, VImage* img,
                  const char* entry_name, char* err, size_t errsz);
void vimage_free(VImage* img);

int  vx_write(const char* path, const VImage* img, char* err, size_t errsz);
int  vx_read(const char* path, VImage* img, char* err, size_t errsz);

// Load a linked image into the CPU: fills prog[], copies data into memory,
// sets *prog_len, returns the entry instruction index.
int64_t vx_load(const VImage* img, VCpu* cpu, Instr* prog, int* prog_len);

#endif // TOOLCHAIN_H
