#include "vcpu.h"
#include "toolchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
//  Symbol table: code labels map to instruction indices, data labels to
//  memory addresses. Both are stored as plain int64 values. The section and
//  binding fields are only meaningful during object assembly.
// ---------------------------------------------------------------------------
typedef struct
{
  char    name[64];
  int64_t value;
  int     is_code;   // code label (value = instr index) vs data label (address)
  int     section;   // RSection (object assembly only)
  int     binding;   // Binding   (object assembly only)
} Symbol;

static Symbol g_symbols[MAX_SYMBOLS];
static int    g_symbol_count;

// Conventional link register (ra) used by the call/ret sugar. Kept at the top
// of the file so it stays clear of r1..r6, which the examples use for data.
#define REG_RA 15

// Object-assembly recording state: when set, parse_int() does not resolve a
// symbolic operand but records its name so the caller can emit a relocation.
static int     g_obj_mode;
static char    g_pending_sym[64];
static int64_t g_pending_addend;
static int     g_pending_hit;

static int add_symbol(const char* name, int64_t value, int is_code, char* err, size_t errsz)
{
  if (g_symbol_count >= MAX_SYMBOLS)
  {
    snprintf(err, errsz, "too many symbols");
    return -1;
  }
  for (int i = 0; i < g_symbol_count; ++i)
  {
    if (strcmp(g_symbols[i].name, name) == 0)
    {
      snprintf(err, errsz, "duplicate label '%s'", name);
      return -1;
    }
  }
  snprintf(g_symbols[g_symbol_count].name, sizeof(g_symbols[0].name), "%s", name);
  g_symbols[g_symbol_count].value   = value;
  g_symbols[g_symbol_count].is_code = is_code;
  g_symbols[g_symbol_count].section = is_code ? RSEC_TEXT : RSEC_DATA;
  g_symbols[g_symbol_count].binding = BIND_LOCAL;
  g_symbol_count += 1;
  return 0;
}

static int find_symbol_idx(const char* name)
{
  for (int i = 0; i < g_symbol_count; ++i)
    if (strcmp(g_symbols[i].name, name) == 0)
      return i;
  return -1;
}

static int find_symbol(const char* name, int64_t* out)
{
  for (int i = 0; i < g_symbol_count; ++i)
  {
    if (strcmp(g_symbols[i].name, name) == 0)
    {
      *out = g_symbols[i].value;
      return 0;
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
//  Assembler-time integer constants (.equ, .struct/.field). Pure compile-time
//  values (field offsets, sizes): resolved as literals wherever an integer is
//  expected (displacements, immediates), never relocated. Separate namespace
//  from labels so they cannot be mistaken for relocatable symbols.
// ---------------------------------------------------------------------------
typedef struct { char name[64]; int64_t value; } Const;
static Const   g_consts[MAX_SYMBOLS];
static int     g_const_count;

// Active .struct block: field offsets accumulate here until .ends.
static int     g_struct_active;
static char    g_struct_name[48];
static int64_t g_struct_off;

static int find_const(const char* name, int64_t* out)
{
  for (int i = 0; i < g_const_count; ++i)
    if (strcmp(g_consts[i].name, name) == 0) { *out = g_consts[i].value; return 1; }
  return 0;
}

static int def_const(const char* name, int64_t value, char* err, size_t errsz)
{
  int64_t dummy;
  if (find_const(name, &dummy)) { snprintf(err, errsz, "duplicate constant '%s'", name); return -1; }
  if (g_const_count >= MAX_SYMBOLS) { snprintf(err, errsz, "too many constants"); return -1; }
  snprintf(g_consts[g_const_count].name, sizeof g_consts[0].name, "%s", name);
  g_consts[g_const_count].value = value;
  g_const_count += 1;
  return 0;
}

// Resolve an integer token: a known constant, or a decimal/hex literal.
static int const_value(const char* tok, int64_t* out)
{
  if (find_const(tok, out)) return 0;
  char* end = NULL;
  *out = (int64_t) strtoll(tok, &end, 0);
  if (end == tok || *end != '\0') return -1;
  return 0;
}

// Compile-time directives (.equ/.struct/.field/.ends). Returns 1 if 'first' was
// one of them (handled), 0 if not one of them, -1 on error (err filled). Struct
// fields become qualified constants "NAME.field"; .ends also defines "NAME.size".
static int handle_const_directive(const char* first, char** toks, int k, int n,
                                  int lineno, char* err, size_t errsz)
{
  if (strcmp(first, ".equ") == 0 || strcmp(first, ".set") == 0)
  {
    if (n - k != 3) { snprintf(err, errsz, "line %d: .equ needs NAME VALUE", lineno); return -1; }
    int64_t v;
    if (const_value(toks[k + 2], &v) != 0) { snprintf(err, errsz, "line %d: invalid value '%s'", lineno, toks[k + 2]); return -1; }
    if (def_const(toks[k + 1], v, err, errsz) != 0) return -1;
    return 1;
  }
  if (strcmp(first, ".struct") == 0)
  {
    if (g_struct_active) { snprintf(err, errsz, "line %d: nested .struct", lineno); return -1; }
    if (n - k != 2) { snprintf(err, errsz, "line %d: .struct needs a name", lineno); return -1; }
    snprintf(g_struct_name, sizeof g_struct_name, "%s", toks[k + 1]);
    g_struct_off    = 0;
    g_struct_active = 1;
    return 1;
  }
  if (strcmp(first, ".field") == 0)
  {
    if (!g_struct_active) { snprintf(err, errsz, "line %d: .field outside .struct", lineno); return -1; }
    if (n - k < 2 || n - k > 3) { snprintf(err, errsz, "line %d: .field needs NAME [size]", lineno); return -1; }
    int64_t size = 4;   // default: one word
    if (n - k == 3 && const_value(toks[k + 2], &size) != 0) { snprintf(err, errsz, "line %d: invalid field size '%s'", lineno, toks[k + 2]); return -1; }
    char fq[64];
    snprintf(fq, sizeof fq, "%s.%s", g_struct_name, toks[k + 1]);
    if (def_const(fq, g_struct_off, err, errsz) != 0) return -1;
    g_struct_off += size;
    return 1;
  }
  if (strcmp(first, ".ends") == 0)
  {
    if (!g_struct_active) { snprintf(err, errsz, "line %d: .ends without .struct", lineno); return -1; }
    char fq[64];
    snprintf(fq, sizeof fq, "%s.size", g_struct_name);
    if (def_const(fq, g_struct_off, err, errsz) != 0) return -1;
    g_struct_active = 0;
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
//  Tokenizer: strips comments, treats commas as separators, splits on spaces.
//  Mutates 'line'. Returns token count.
// ---------------------------------------------------------------------------
static int tokenize(char* line, char** toks, int max_toks)
{
  for (char* p = line; *p; ++p)
  {
    if (*p == ';' || *p == '#') { *p = '\0'; break; }
    if (*p == ',') *p = ' ';
  }
  int n = 0;
  char* save = NULL;
  for (char* t = strtok_r(line, " \t\r\n", &save);
       t && n < max_toks;
       t = strtok_r(NULL, " \t\r\n", &save))
  {
    toks[n++] = t;
  }
  return n;
}

// Parse a register operand with the expected prefix ('r', 'f' or 'v').
static int parse_reg(const char* tok, char prefix, int count, char* err, size_t errsz)
{
  if (tok[0] != prefix || !isdigit((unsigned char) tok[1]))
  {
    snprintf(err, errsz, "expected %c-register, got '%s'", prefix, tok);
    return -1;
  }
  int idx = atoi(tok + 1);
  if (idx < 0 || idx >= count)
  {
    snprintf(err, errsz, "register '%s' out of range", tok);
    return -1;
  }
  return idx;
}

// Parse an integer immediate, or a symbol with optional addend ("sym", "sym+N",
// "sym-N"). The addend unit matches the target: bytes for data, instruction
// index for code. Decimal or hex.
static int parse_int(const char* tok, int64_t* out, char* err, size_t errsz)
{
  if (find_const(tok, out)) return 0;   // assembler-time constant (.equ / struct field)

  // Negated constant: "-NOME". It belongs here and not in the symbol branch
  // below, because a constant is not relocatable: its value is known now, so
  // negating it is compile-time arithmetic. Without this, every immediate that
  // allocates downwards has to spell the number out again -- "addi r1, r1, -64"
  // next to a CTX_FRAME_SIZE that exists precisely to avoid saying 64 twice.
  // Relocatable symbols stay non-negatable, which is correct: the linker patches
  // an address, and the negative of an address is not one.
  if (tok[0] == '-' && find_const(tok + 1, out)) { *out = -*out; return 0; }
  if (isalpha((unsigned char) tok[0]) || tok[0] == '_')
  {
    // Split the identifier from an optional "+off"/"-off" suffix.
    const char* p = tok + 1;
    while (*p && (isalnum((unsigned char) *p) || *p == '_')) ++p;
    size_t nlen = (size_t) (p - tok);
    if (nlen >= sizeof g_pending_sym)
    { snprintf(err, errsz, "symbol name too long in '%s'", tok); return -1; }
    char name[64];
    memcpy(name, tok, nlen);
    name[nlen] = '\0';

    int64_t addend = 0;
    if (*p == '+' || *p == '-')
    {
      char* aend = NULL;
      addend = (int64_t) strtoll(p, &aend, 0);   // strtoll consumes the sign
      if (aend == p || *aend != '\0')
      { snprintf(err, errsz, "invalid offset in '%s'", tok); return -1; }
    }
    else if (*p != '\0')
    { snprintf(err, errsz, "invalid symbol operand '%s'", tok); return -1; }

    if (g_obj_mode)
    {
      snprintf(g_pending_sym, sizeof(g_pending_sym), "%s", name);
      g_pending_addend = addend;
      g_pending_hit = 1;
      *out = 0;   // placeholder, patched by the linker (value + addend)
      return 0;
    }
    if (find_symbol(name, out) != 0)
    {
      snprintf(err, errsz, "unknown symbol '%s'", name);
      return -1;
    }
    *out += addend;
    return 0;
  }
  char* end = NULL;
  *out = (int64_t) strtoll(tok, &end, 0);
  if (end == tok || *end != '\0')
  {
    snprintf(err, errsz, "invalid integer '%s'", tok);
    return -1;
  }
  return 0;
}

// Parse a branch/jump target: it MUST be a label, so every control-flow target
// is relocatable. Numeric targets are rejected (they would not survive linking).
static int parse_target(const char* tok, int64_t* out, char* err, size_t errsz)
{
  if (!(isalpha((unsigned char) tok[0]) || tok[0] == '_'))
  {
    snprintf(err, errsz, "branch/jump target must be a label, got '%s'", tok);
    return -1;
  }
  return parse_int(tok, out, err, errsz);
}

// Parse a memory operand: either "rN" (displacement 0) or "disp(rN)" with an
// integer displacement (dec/hex, signed). Fills *disp and *reg.
static int parse_mem(const char* tok, int64_t* disp, int* reg, char* err, size_t errsz)
{
  const char* lp = strchr(tok, '(');
  if (lp == NULL)
  {
    *disp = 0;
    int idx = parse_reg(tok, 'r', NUM_SCALAR, err, errsz);
    if (idx < 0) return -1;
    *reg = idx;
    return 0;
  }

  size_t dlen = (size_t) (lp - tok);
  char dbuf[32];
  if (dlen == 0 || dlen >= sizeof(dbuf))
  { snprintf(err, errsz, "invalid displacement in '%s'", tok); return -1; }
  memcpy(dbuf, tok, dlen);
  dbuf[dlen] = '\0';
  if (const_value(dbuf, disp) != 0)
  { snprintf(err, errsz, "invalid displacement in '%s'", tok); return -1; }

  const char* rp    = lp + 1;
  const char* close = strchr(rp, ')');
  if (close == NULL || close[1] != '\0')
  { snprintf(err, errsz, "malformed memory operand '%s'", tok); return -1; }
  size_t rlen = (size_t) (close - rp);
  char rbuf[16];
  if (rlen == 0 || rlen >= sizeof(rbuf))
  { snprintf(err, errsz, "malformed memory operand '%s'", tok); return -1; }
  memcpy(rbuf, rp, rlen);
  rbuf[rlen] = '\0';
  int idx = parse_reg(rbuf, 'r', NUM_SCALAR, err, errsz);
  if (idx < 0) return -1;
  *reg = idx;
  return 0;
}

// ---------------------------------------------------------------------------
//  Instruction encoding (second pass)
// ---------------------------------------------------------------------------
#define R(prefix, count) do {                                   \
    int _idx = parse_reg(toks[k++], prefix, count, err, errsz); \
    if (_idx < 0) return -1;                                    \
    reg = _idx;                                                 \
  } while (0)

static int need(int have, int want, const char* mn, char* err, size_t errsz)
{
  if (have != want)
  {
    snprintf(err, errsz, "'%s' expects %d operand(s), got %d", mn, want, have);
    return -1;
  }
  return 0;
}

static int encode(char** toks, int n, Instr* out, char* err, size_t errsz)
{
  const char* mn = toks[0];
  int k = 1;          // operand cursor
  int reg;
  int64_t imm;
  memset(out, 0, sizeof(*out));

  #define ARGS (n - 1)

  if (strcmp(mn, "li") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    if (parse_int(toks[k++], &imm, err, errsz)) return -1;
    out->op = OP_LI; out->imm = imm;
  }
  else if (strcmp(mn, "mov") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_MOV;
  }
  else if (strcmp(mn, "add") == 0 || strcmp(mn, "sub") == 0 || strcmp(mn, "mul") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    R('r', NUM_SCALAR); out->c = reg;
    out->op = (mn[0] == 'a') ? OP_ADD : (mn[0] == 's' ? OP_SUB : OP_MUL);
  }
  else if (strcmp(mn, "addi") == 0 || strcmp(mn, "slli") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    if (parse_int(toks[k++], &imm, err, errsz)) return -1;
    out->imm = imm;
    out->op  = (mn[0] == 'a') ? OP_ADDI : OP_SLLI;
  }
  else if (strcmp(mn, "srli") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    if (parse_int(toks[k++], &imm, err, errsz)) return -1;
    out->imm = imm;
    out->op  = OP_SRLI;
  }
  else if (strcmp(mn, "and") == 0 || strcmp(mn, "or") == 0 || strcmp(mn, "xor") == 0 ||
           strcmp(mn, "div") == 0 || strcmp(mn, "rem") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    R('r', NUM_SCALAR); out->c = reg;
    out->op = (strcmp(mn, "and") == 0) ? OP_AND :
              (strcmp(mn, "or")  == 0) ? OP_OR  :
              (strcmp(mn, "xor") == 0) ? OP_XOR :
              (strcmp(mn, "div") == 0) ? OP_DIV : OP_REM;
  }
  else if (strcmp(mn, "fli") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    out->fimm = strtod(toks[k++], NULL);
    out->op   = OP_FLI;
  }
  else if (strcmp(mn, "flw") == 0 || strcmp(mn, "fsw") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    if (parse_mem(toks[k++], &imm, &reg, err, errsz)) return -1;
    out->b = reg; out->imm = imm;
    out->op = (mn[1] == 'l') ? OP_FLW : OP_FSW;
  }
  else if (strcmp(mn, "fadd") == 0 || strcmp(mn, "fmul") == 0 || strcmp(mn, "fmacc") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    R('f', NUM_FLOAT); out->b = reg;
    R('f', NUM_FLOAT); out->c = reg;
    out->op = (mn[1] == 'a') ? OP_FADD : (strcmp(mn, "fmul") == 0 ? OP_FMUL : OP_FMACC);
  }
  else if (strcmp(mn, "fmov") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    R('f', NUM_FLOAT); out->b = reg;
    out->op = OP_FMOV;
  }
  else if (strcmp(mn, "fmin") == 0 || strcmp(mn, "fmax") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    R('f', NUM_FLOAT); out->b = reg;
    R('f', NUM_FLOAT); out->c = reg;
    out->op = (strcmp(mn, "fmin") == 0) ? OP_FMIN : OP_FMAX;
  }
  else if (strcmp(mn, "fsub") == 0 || strcmp(mn, "fdiv") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    R('f', NUM_FLOAT); out->b = reg;
    R('f', NUM_FLOAT); out->c = reg;
    out->op = (strcmp(mn, "fsub") == 0) ? OP_FSUB : OP_FDIV;
  }
  else if (strcmp(mn, "fneg") == 0 || strcmp(mn, "fsqrt") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->a = reg;
    R('f', NUM_FLOAT); out->b = reg;
    out->op = (strcmp(mn, "fneg") == 0) ? OP_FNEG : OP_FSQRT;
  }
  else if (strcmp(mn, "lw") == 0 || strcmp(mn, "sw") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    if (parse_mem(toks[k++], &imm, &reg, err, errsz)) return -1;
    out->b = reg; out->imm = imm;
    out->op = (strcmp(mn, "lw") == 0) ? OP_LW : OP_SW;
  }
  else if (strcmp(mn, "setvl") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_SETVL;
  }
  else if (strcmp(mn, "beq") == 0 || strcmp(mn, "bne") == 0 || strcmp(mn, "blt") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    R('r', NUM_SCALAR); out->c = reg;
    if (parse_target(toks[k++], &imm, err, errsz)) return -1;
    out->target = (int) imm;
    out->op = (mn[1] == 'e') ? OP_BEQ : (mn[1] == 'n' ? OP_BNE : OP_BLT);
  }
  else if (strcmp(mn, "j") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    if (parse_target(toks[k++], &imm, err, errsz)) return -1;
    out->target = (int) imm;
    out->op = OP_J;
  }
  else if (strcmp(mn, "jal") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    if (parse_target(toks[k++], &imm, err, errsz)) return -1;
    out->target = (int) imm;
    out->op = OP_JAL;
  }
  else if (strcmp(mn, "jalr") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_JALR;
  }
  else if (strcmp(mn, "call") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    out->a = REG_RA;
    if (parse_target(toks[k++], &imm, err, errsz)) return -1;
    out->target = (int) imm;
    out->op = OP_JAL;
  }
  else if (strcmp(mn, "ret") == 0)
  {
    if (need(ARGS, 0, mn, err, errsz)) return -1;
    out->a = 0; out->b = REG_RA;
    out->op = OP_JALR;
  }
  else if (strcmp(mn, "jr") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    out->a = 0;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_JALR;
  }
  else if (strcmp(mn, "sti") == 0)
  {
    if (need(ARGS, 0, mn, err, errsz)) return -1;
    out->op = OP_STI;
  }
  else if (strcmp(mn, "cli") == 0)
  {
    if (need(ARGS, 0, mn, err, errsz)) return -1;
    out->op = OP_CLI;
  }
  else if (strcmp(mn, "reti") == 0)
  {
    if (need(ARGS, 0, mn, err, errsz)) return -1;
    out->op = OP_RETI;
  }
  else if (strcmp(mn, "sethandler") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    if (parse_target(toks[k++], &imm, err, errsz)) return -1;
    out->target = (int) imm;
    out->op = OP_SETHANDLER;
  }
  else if (strcmp(mn, "settimer") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_SETTIMER;
  }
  else if (strcmp(mn, "mfpsw") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    out->op = OP_MFPSW;
  }
  else if (strcmp(mn, "mtpsw") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_MTPSW;
  }
  else if (strcmp(mn, "mfepc") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    out->op = OP_MFEPC;
  }
  else if (strcmp(mn, "mtepc") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_MTEPC;
  }
  else if (strcmp(mn, "mfepsw") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->a = reg;
    out->op = OP_MFEPSW;
  }
  else if (strcmp(mn, "mtepsw") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_MTEPSW;
  }
  else if (strcmp(mn, "halt") == 0)
  {
    if (need(ARGS, 0, mn, err, errsz)) return -1;
    out->op = OP_HALT;
  }
  else if (strcmp(mn, "vload") == 0)
  {
    if (ARGS == 2)
    {
      R('v', NUM_VECTOR); out->a = reg;
      R('r', NUM_SCALAR); out->b = reg;
      out->op = OP_VLOAD;
    }
    else if (ARGS == 3)
    {
      R('v', NUM_VECTOR); out->a = reg;
      R('r', NUM_SCALAR); out->b = reg;
      R('r', NUM_SCALAR); out->c = reg;
      out->op = OP_VLOADS;
    }
    else
    {
      snprintf(err, errsz, "'vload' expects 2 or 3 operands, got %d", ARGS);
      return -1;
    }
  }
  else if (strcmp(mn, "vstore") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_VSTORE;
  }
  else if (strcmp(mn, "vloadx") == 0 || strcmp(mn, "vstorex") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = (strcmp(mn, "vloadx") == 0) ? OP_VLOADX : OP_VSTOREX;
  }
  else if (strcmp(mn, "vadd") == 0 || strcmp(mn, "vsub") == 0 || strcmp(mn, "vmul") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = (mn[1] == 'a') ? OP_VADD : (mn[1] == 's' ? OP_VSUB : OP_VMUL);
  }
  else if (strcmp(mn, "vmacc") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('f', NUM_FLOAT);  out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = OP_VMACC;
  }
  else if (strcmp(mn, "vscale") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    R('f', NUM_FLOAT);  out->c = reg;
    out->op = OP_VSCALE;
  }
  else if (strcmp(mn, "vredsum") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT);  out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    out->op = OP_VREDSUM;
  }
  else if (strcmp(mn, "vredmax") == 0 || strcmp(mn, "vredmin") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT);  out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    out->op = (strcmp(mn, "vredmax") == 0) ? OP_VREDMAX : OP_VREDMIN;
  }
  else if (strcmp(mn, "vsplat") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('f', NUM_FLOAT);  out->b = reg;
    out->op = OP_VSPLAT;
  }
  else if (strcmp(mn, "vadds") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    R('f', NUM_FLOAT);  out->c = reg;
    out->op = OP_VADDS;
  }
  else if (strcmp(mn, "vmin") == 0 || strcmp(mn, "vmax") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = (strcmp(mn, "vmin") == 0) ? OP_VMIN : OP_VMAX;
  }
  else if (strcmp(mn, "vmslt") == 0 || strcmp(mn, "vmsgt") == 0 || strcmp(mn, "vmseq") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = (strcmp(mn, "vmslt") == 0) ? OP_VMSLT :
              (strcmp(mn, "vmsgt") == 0) ? OP_VMSGT : OP_VMSEQ;
  }
  else if (strcmp(mn, "vmerge") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = OP_VMERGE;
  }
  else if (strcmp(mn, "vaddm") == 0 || strcmp(mn, "vsubm") == 0 || strcmp(mn, "vmulm") == 0)
  {
    if (need(ARGS, 3, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('v', NUM_VECTOR); out->b = reg;
    R('v', NUM_VECTOR); out->c = reg;
    out->op = (strcmp(mn, "vaddm") == 0) ? OP_VADDM :
              (strcmp(mn, "vsubm") == 0) ? OP_VSUBM : OP_VMULM;
  }
  else if (strcmp(mn, "vstorem") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_VSTOREM;
  }
  else if (strcmp(mn, "dumps") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    out->op = OP_DUMPS;
  }
  else if (strcmp(mn, "dumpf") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('f', NUM_FLOAT); out->b = reg;
    out->op = OP_DUMPF;
  }
  else if (strcmp(mn, "dumpv") == 0)
  {
    if (need(ARGS, 1, mn, err, errsz)) return -1;
    R('v', NUM_VECTOR); out->a = reg;
    out->op = OP_DUMPV;
  }
  else if (strcmp(mn, "dumpm") == 0)
  {
    if (need(ARGS, 2, mn, err, errsz)) return -1;
    R('r', NUM_SCALAR); out->b = reg;
    if (parse_int(toks[k++], &imm, err, errsz)) return -1;
    out->imm = imm;
    out->op  = OP_DUMPM;
  }
  else if (strcmp(mn, "dumpmask") == 0)
  {
    if (need(ARGS, 0, mn, err, errsz)) return -1;
    out->op = OP_DUMPMASK;
  }
  else
  {
    snprintf(err, errsz, "unknown mnemonic '%s'", mn);
    return -1;
  }

  (void) reg;
  return 0;
  #undef ARGS
}

// ---------------------------------------------------------------------------
//  Assembler driver: two passes over the source file.
// ---------------------------------------------------------------------------
enum { SEC_TEXT, SEC_DATA };

static char* g_code_lines[MAX_INSTR];  // text-section lines kept for pass 2
static int   g_code_count;

static void free_code_lines(void)
{
  for (int i = 0; i < g_code_count; ++i) free(g_code_lines[i]);
  g_code_count = 0;
}

// Writes the fully-expanded text-section listing (post .include/.equ/.struct/
// .proc expansion, pre pass-2 encoding) to PATH: one code label per line where
// defined, then "<index>  <instruction>" for every entry in g_code_lines. Lets
// you see exactly what .proc/.endproc (and the other compile-time directives)
// generated, without having to reason about it by hand.
static int dump_expanded(const char* path, char* err, size_t errsz)
{
  FILE* fp = fopen(path, "w");
  if (!fp) { snprintf(err, errsz, "cannot write '%s'", path); return -1; }
  for (int i = 0; i < g_code_count; ++i)
  {
    for (int s = 0; s < g_symbol_count; ++s)
      if (g_symbols[s].is_code == 1 && g_symbols[s].value == i)
        fprintf(fp, "%s:\n", g_symbols[s].name);
    fprintf(fp, "%6d  %s\n", i, g_code_lines[i]);
  }
  fclose(fp);
  return 0;
}

static void join_tokens(char** toks, int start, int n, char* out, size_t outsz);

// ---------------------------------------------------------------------------
//  .proc / .endproc: optional sugar for the common non-leaf procedure shape
//  (single linear body, one exit). ".proc NAME" DEFINES the label NAME at
//  this point (like "NAME:"); ".endproc NAME" checks the name matches the
//  open .proc. A separate "NAME:" label before ".proc NAME" would just
//  redefine the same symbol twice — an error, not a no-op — since .proc IS
//  the label. Anything that does not fit this shape (branches to a shared
//  exit, routines that never return, true leaves) is still written by hand.
//
//  The body between .proc/.endproc is buffered (not emitted) as it is read,
//  because the prologue depends on the whole body: at .endproc it is scanned
//  for scalar registers r1..r13 written by a plain destination-writing
//  instruction (li/mov/add/sub/mul/addi/slli/srli/and/or/xor/div/rem/lw/
//  setvl/mfpsw/mfepc/mfepsw), and only THOSE are pushed — plus r15 always, since
//  "call" is always "jal r15, target" (link register cabled in the assembler)
//  and must be saved before any call regardless of what the body computes.
//  r15 is pushed first/popped last so it survives every call in the body;
//  the detected registers are pushed/popped around it, ascending/descending,
//  matching the LIFO push/pop-pair style used elsewhere (see ctx_save).
//
//  This is a static scan of the body's own instructions — it does NOT see
//  what a callee clobbers. A register that only gets its value from a called
//  routine (like the psw idiom in coda.vasm's *_s wrappers, set by
//  irq_save/irq_restore) is invisible to it and must still be saved by hand
//  around the call, exactly as before.
//
//  Because the prologue length isn't known until .endproc, the body may only
//  contain plain instruction lines: labels and directives inside .proc/
//  .endproc are rejected (kept consistent with the documented "linear body,
//  one exit" shape).
// ---------------------------------------------------------------------------
static int    g_proc_active;
static char   g_proc_name[64];
static char*  g_proc_body[MAX_INSTR];
static int    g_proc_body_count;

static void free_proc_body(void)
{
  for (int i = 0; i < g_proc_body_count; ++i) free(g_proc_body[i]);
  g_proc_body_count = 0;
}

static int emit_synth_line(const char* text, char* err, size_t errsz)
{
  if (g_code_count >= MAX_INSTR)
  {
    snprintf(err, errsz, "too many instructions");
    return -1;
  }
  g_code_lines[g_code_count] = strdup(text);
  g_code_count += 1;
  return 0;
}

// Scalar destination register (1..13) written by TOKS, or -1 if TOKS does not
// write one. Only plain, unambiguous "dest is the first operand" mnemonics
// are recognized — see the block comment above for the rationale/limits.
static int scalar_dest_reg(char** toks, int n)
{
  static const char* dest1[] = {
    "li", "mov", "add", "sub", "mul", "addi", "slli", "srli",
    "and", "or", "xor", "div", "rem", "lw", "setvl", "mfpsw", "mfepc",
    "mfepsw", NULL
  };
  if (n < 2) return -1;
  int match = 0;
  for (int i = 0; dest1[i] != NULL; ++i)
    if (strcmp(toks[0], dest1[i]) == 0) { match = 1; break; }
  if (!match) return -1;
  const char* t = toks[1];
  if (t[0] != 'r') return -1;
  char* endp;
  long v = strtol(t + 1, &endp, 10);
  if (*endp != '\0' || v < 1 || v > 13) return -1;
  return (int) v;
}

// .endproc: scans the buffered body for clobbered scalars, then emits the
// prologue, the body (verbatim), and the matching epilogue.
static int close_and_emit_proc(char* err, size_t errsz)
{
  int used[14] = { 0 };
  for (int i = 0; i < g_proc_body_count; ++i)
  {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", g_proc_body[i]);
    char* etoks[64];
    int en = tokenize(buf, etoks, 64);
    if (en == 0) continue;
    int r = scalar_dest_reg(etoks, en);
    if (r >= 1 && r <= 13) used[r] = 1;
  }

  // Comment marking which .proc this block came from and which scalars got
  // auto-saved, so an --emit-expanded listing shows where the prologue/
  // epilogue idiom (manual.md §4.2.1) was generated instead of looking
  // hand-written. Each list is in the actual push/pop order (r15 first/last,
  // the rest ascending/descending around it) so it reads like a trace of the
  // block below it. Trailing "; ..." is stripped by tokenize() in pass 2, so
  // it has no effect on encoding.
  char push_reglist[160] = "r15";
  for (int r = 1; r <= 13; ++r)
    if (used[r])
    {
      char tmp[8];
      snprintf(tmp, sizeof tmp, ",r%d", r);
      strncat(push_reglist, tmp, sizeof push_reglist - strlen(push_reglist) - 1);
    }
  char pop_reglist[160] = "";
  for (int r = 13; r >= 1; --r)
    if (used[r])
    {
      char tmp[8];
      snprintf(tmp, sizeof tmp, "r%d,", r);
      strncat(pop_reglist, tmp, sizeof pop_reglist - strlen(pop_reglist) - 1);
    }
  strncat(pop_reglist, "r15", sizeof pop_reglist - strlen(pop_reglist) - 1);
  char prologue_line[192];
  snprintf(prologue_line, sizeof prologue_line,
           "addi r14, r14, -4  ; .proc %s: prologo auto (%s)", g_proc_name, push_reglist);
  char epilogue_line[192];
  snprintf(epilogue_line, sizeof epilogue_line,
           "ret  ; .proc %s: fine epilogo auto (%s)", g_proc_name, pop_reglist);

  if (emit_synth_line(prologue_line, err, errsz) != 0) return -1;
  if (emit_synth_line("sw r15, 0(r14)", err, errsz) != 0) return -1;
  for (int r = 1; r <= 13; ++r)
  {
    if (!used[r]) continue;
    char line[32];
    if (emit_synth_line("addi r14, r14, -4", err, errsz) != 0) return -1;
    snprintf(line, sizeof line, "sw r%d, 0(r14)", r);
    if (emit_synth_line(line, err, errsz) != 0) return -1;
  }

  for (int i = 0; i < g_proc_body_count; ++i)
  {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", g_proc_body[i]);
    char* etoks[64];
    int en = tokenize(buf, etoks, 64);
    free(g_proc_body[i]);
    if (en == 0) continue;
    if (g_code_count >= MAX_INSTR)
    {
      snprintf(err, errsz, "too many instructions");
      g_proc_body_count = 0;
      return -1;
    }
    char joined[512];
    join_tokens(etoks, 0, en, joined, sizeof joined);
    g_code_lines[g_code_count] = strdup(joined);
    g_code_count += 1;
  }
  g_proc_body_count = 0;

  for (int r = 13; r >= 1; --r)
  {
    if (!used[r]) continue;
    char line[32];
    snprintf(line, sizeof line, "lw r%d, 0(r14)", r);
    if (emit_synth_line(line, err, errsz) != 0) return -1;
    if (emit_synth_line("addi r14, r14, 4", err, errsz) != 0) return -1;
  }
  if (emit_synth_line("lw r15, 0(r14)", err, errsz) != 0) return -1;
  if (emit_synth_line("addi r14, r14, 4", err, errsz) != 0) return -1;
  if (emit_synth_line(epilogue_line, err, errsz) != 0) return -1;

  g_proc_active = 0;
  return 1;
}

static int handle_proc_directive(const char* first, char** toks, int k, int n,
                                  int section, int lineno, char* err, size_t errsz)
{
  if (strcmp(first, ".proc") == 0)
  {
    if (g_proc_active) { snprintf(err, errsz, "line %d: nested .proc (already inside '%s')", lineno, g_proc_name); return -1; }
    if (n - k != 2) { snprintf(err, errsz, "line %d: .proc needs a name", lineno); return -1; }
    if (section != SEC_TEXT) { snprintf(err, errsz, "line %d: .proc outside .text", lineno); return -1; }
    snprintf(g_proc_name, sizeof g_proc_name, "%s", toks[k + 1]);
    g_proc_active = 1;
    if (add_symbol(g_proc_name, g_code_count, 1, err, errsz) != 0) return -1;
    return 1;
  }
  if (strcmp(first, ".endproc") == 0)
  {
    // Reached only when no .proc is open: handle_proc_body_line() already
    // intercepts ".endproc" while g_proc_active is true (see below).
    snprintf(err, errsz, "line %d: .endproc without .proc", lineno);
    return -1;
  }
  return 0;
}

// Called for every physical line while a .proc body is being buffered
// (before the normal label/directive/instruction dispatch even looks at it).
// Returns 1 if the line was consumed (caller should `continue`), 0 if no
// .proc is open (caller proceeds as usual), -1 on error.
static int handle_proc_body_line(const char* raw_line, char** toks, int n,
                                  int lineno, char* err, size_t errsz)
{
  if (!g_proc_active) return 0;

  int k0 = 0;
  size_t len0 = strlen(toks[0]);
  if (len0 > 1 && toks[0][len0 - 1] == ':') k0 = 1;

  if (k0 < n && strcmp(toks[k0], ".endproc") == 0)
  {
    if (n - k0 != 2) { snprintf(err, errsz, "line %d: .endproc needs a name", lineno); return -1; }
    if (strcmp(toks[k0 + 1], g_proc_name) != 0)
    { snprintf(err, errsz, "line %d: .endproc '%s' does not match open .proc '%s'", lineno, toks[k0 + 1], g_proc_name); return -1; }
    return close_and_emit_proc(err, errsz);
  }

  if (k0 != 0) { snprintf(err, errsz, "line %d: labels are not allowed inside .proc/.endproc", lineno); return -1; }
  if (toks[0][0] == '.') { snprintf(err, errsz, "line %d: directives are not allowed inside .proc/.endproc", lineno); return -1; }

  if (g_proc_body_count >= MAX_INSTR) { snprintf(err, errsz, "too many instructions"); return -1; }
  g_proc_body[g_proc_body_count++] = strdup(raw_line);
  return 1;
}

static void join_tokens(char** toks, int start, int n, char* out, size_t outsz)
{
  out[0] = '\0';
  for (int i = start; i < n; ++i)
  {
    strncat(out, toks[i], outsz - strlen(out) - 1);
    if (i + 1 < n) strncat(out, " ", outsz - strlen(out) - 1);
  }
}

// ---------------------------------------------------------------------------
//  .include support: a small stack of open source files. The bottom of the
//  stack is the top-level file (left open so the caller's fclose(fp) frees it);
//  nested includes are closed on EOF.
//
//  A name is resolved, in order, against: the directory of the top-level file,
//  then each -I directory in the order it was given. An absolute name is used
//  as-is. That order is what lets a bare name ("pool.vinc") be found through -I
//  while the existing relative spellings ("../include/pool.vinc") keep working.
//
//  .include is IDEMPOTENT: every file that enters an assembly unit is recorded
//  by its canonical path (realpath), and a second .include of the same file is
//  a silent no-op — the semantics of #pragma once. Without it a .vinc could not
//  include another .vinc, since the second arrival would redefine every
//  constant ("duplicate constant"); with it the leaf-only rule that types.vinc
//  and pool.vinc carry in their headers is no longer needed.
// ---------------------------------------------------------------------------
#define MAX_INCLUDE   8
#define MAX_INC_DIRS 16
#define MAX_INC_SEEN 64

// Search path from -I. Global because it is set once per process by the CLI and
// applies to whatever the assembler is asked to assemble afterwards.
static char g_inc_dirs[MAX_INC_DIRS][400];
static int  g_inc_dir_count;

void asm_clear_include_dirs(void)
{
  g_inc_dir_count = 0;
}

int asm_add_include_dir(const char* dir)
{
  if (g_inc_dir_count >= MAX_INC_DIRS) return -1;
  snprintf(g_inc_dirs[g_inc_dir_count++], sizeof g_inc_dirs[0], "%s", dir);
  return 0;
}

// Per-assembly-unit include state: the open-file stack plus the set of files
// already pulled in (canonical paths), which is what makes .include idempotent.
typedef struct
{
  FILE* stk[MAX_INCLUDE];
  int   sp;
  char  base_dir[400];
  char  seen[MAX_INC_SEEN][512];
  int   nseen;
} IncState;

static void base_dir_of(const char* path, char* out, size_t sz)
{
  const char* slash = strrchr(path, '/');
  if (slash) snprintf(out, sz, "%.*s", (int) (slash - path), path);
  else if (sz) out[0] = '\0';
}

// Canonical form of 'path', for identity comparison. realpath() only fails on a
// file we cannot reach, and every caller has just opened it; the raw path is a
// safe fallback anyway (it only ever costs a missed duplicate, never a wrong
// match, because two spellings that canonicalise differently stay different).
static void inc_canonical(const char* path, char* out, size_t sz)
{
  char* real = realpath(path, NULL);
  snprintf(out, sz, "%s", real ? real : path);
  free(real);
}

static int inc_already_seen(const IncState* inc, const char* canon)
{
  for (int i = 0; i < inc->nseen; ++i)
    if (strcmp(inc->seen[i], canon) == 0) return 1;
  return 0;
}

static int inc_remember(IncState* inc, const char* canon, char* err, size_t errsz)
{
  if (inc->nseen >= MAX_INC_SEEN)
  {
    snprintf(err, errsz, "too many included files");
    return -1;
  }
  snprintf(inc->seen[inc->nseen++], sizeof inc->seen[0], "%s", canon);
  return 0;
}

// Start an assembly unit on an already-open top-level file. The top-level file
// itself joins the seen-set, so a source that .includes itself is a no-op too.
static int inc_begin(IncState* inc, FILE* top, const char* path, char* err, size_t errsz)
{
  memset(inc, 0, sizeof *inc);
  base_dir_of(path, inc->base_dir, sizeof inc->base_dir);
  inc->stk[inc->sp++] = top;
  char canon[512];
  inc_canonical(path, canon, sizeof canon);
  return inc_remember(inc, canon, err, errsz);
}

static int inc_next_line(IncState* inc, char* line, size_t sz)
{
  while (inc->sp > 0)
  {
    if (fgets(line, sz, inc->stk[inc->sp - 1])) return 1;
    if (inc->sp == 1) return 0;      // leave the top-level file open for the caller
    fclose(inc->stk[inc->sp - 1]);
    inc->sp -= 1;
  }
  return 0;
}

// Close every nested include still open. The top-level file stays open: the
// caller owns it and fcloses it itself, on the error paths too.
static void inc_cleanup(IncState* inc)
{
  while (inc->sp > 1) fclose(inc->stk[--inc->sp]);
}

// Handle one .include directive: resolve, skip if already included, push.
// Returns 1 pushed, 0 skipped (already seen), -1 error (message in 'err').
static int inc_push(IncState* inc, const char* raw, int lineno, char* err, size_t errsz)
{
  char name[400];
  size_t L = strlen(raw);
  if (L >= 2 && raw[0] == '"' && raw[L - 1] == '"')
    snprintf(name, sizeof name, "%.*s", (int) (L - 2), raw + 1);
  else
    snprintf(name, sizeof name, "%s", raw);

  // Candidate directories: top-level file's directory first, then each -I.
  // A NULL entry means "use the name as given".
  const char* dirs[1 + MAX_INC_DIRS];
  int ndirs = 0;
  if (name[0] == '/') dirs[ndirs++] = NULL;
  else
  {
    if (inc->base_dir[0] != '\0') dirs[ndirs++] = inc->base_dir;
    else                          dirs[ndirs++] = NULL;
    for (int i = 0; i < g_inc_dir_count; ++i) dirs[ndirs++] = g_inc_dirs[i];
  }

  char  full[512];
  FILE* f = NULL;
  for (int i = 0; i < ndirs && !f; ++i)
  {
    if (dirs[i]) snprintf(full, sizeof full, "%s/%s", dirs[i], name);
    else         snprintf(full, sizeof full, "%s", name);
    f = fopen(full, "r");
  }
  if (!f)
  {
    int off = snprintf(err, errsz, "line %d: cannot open include '%s'", lineno, name);
    if (g_inc_dir_count > 0 && off > 0 && (size_t) off < errsz)
    {
      off += snprintf(err + off, errsz - (size_t) off, " (searched: %s", inc->base_dir);
      for (int i = 0; i < g_inc_dir_count && off > 0 && (size_t) off < errsz; ++i)
        off += snprintf(err + off, errsz - (size_t) off, ", %s", g_inc_dirs[i]);
      if (off > 0 && (size_t) off < errsz) snprintf(err + off, errsz - (size_t) off, ")");
    }
    return -1;
  }

  char canon[512];
  inc_canonical(full, canon, sizeof canon);
  if (inc_already_seen(inc, canon)) { fclose(f); return 0; }

  if (inc->sp >= MAX_INCLUDE)
  {
    snprintf(err, errsz, "line %d: .include nested too deep", lineno);
    fclose(f);
    return -1;
  }
  if (inc_remember(inc, canon, err, errsz) != 0) { fclose(f); return -1; }
  inc->stk[inc->sp++] = f;
  return 1;
}

int assemble(const char* path, VCpu* cpu, Instr* prog, char* err, size_t errsz)
{
  g_symbol_count = 0;
  g_code_count   = 0;
  g_const_count   = 0;
  g_struct_active = 0;
  g_proc_active   = 0;
  g_proc_body_count = 0;

  FILE* fp = fopen(path, "r");
  if (!fp)
  {
    snprintf(err, errsz, "cannot open '%s'", path);
    return -1;
  }

  // ---- Pass 1: collect symbols, emit data, keep code lines --------------
  char    line[512];
  char*   toks[64];
  int     section  = SEC_TEXT;
  // Skip the guard word: address 0 must stay NULL (see NULL_GUARD in vcpu.h).
  // In the linked path the same reservation is made once by link_objects();
  // here there is no linker, so this single-file path makes it itself.
  int64_t data_ptr = NULL_GUARD;
  int     lineno   = 0;

  // .include: file stack (bottom = top-level file, kept open for fclose(fp)).
  IncState inc;
  if (inc_begin(&inc, fp, path, err, errsz) != 0) { fclose(fp); return -1; }

  while (inc_next_line(&inc, line, sizeof(line)))
  {
    lineno += 1;
    char work[512];
    snprintf(work, sizeof(work), "%s", line);

    int n = tokenize(work, toks, 64);
    if (n == 0) continue;

    int hb = handle_proc_body_line(line, toks, n, lineno, err, errsz);
    if (hb < 0) { inc_cleanup(&inc); fclose(fp); return -1; }
    if (hb == 1) continue;

    int k = 0;
    // Optional leading label "name:"
    size_t len = strlen(toks[0]);
    if (len > 1 && toks[0][len - 1] == ':')
    {
      char name[64];
      snprintf(name, sizeof(name), "%.*s", (int) (len - 1), toks[0]);
      int64_t value = (section == SEC_DATA) ? data_ptr : g_code_count;
      if (add_symbol(name, value, section == SEC_TEXT, err, errsz) != 0) { inc_cleanup(&inc); fclose(fp); return -1; }
      k = 1;
    }
    if (k >= n) continue;  // label-only line

    const char* first = toks[k];
    if (first[0] == '.')
    {
      if (strcmp(first, ".text") == 0) { section = SEC_TEXT; }
      else if (strcmp(first, ".data") == 0) { section = SEC_DATA; }
      else if (strcmp(first, ".include") == 0)
      {
        if (k + 1 >= n) { snprintf(err, errsz, "line %d: .include needs a file", lineno); inc_cleanup(&inc); fclose(fp); return -1; }
        if (inc_push(&inc, toks[k + 1], lineno, err, errsz) < 0) { inc_cleanup(&inc); fclose(fp); return -1; }
      }
      else if (strcmp(first, ".float") == 0)
      {
        for (int i = k + 1; i < n; ++i)
        {
          float val = strtof(toks[i], NULL);
          if ((uint64_t) data_ptr + 4 > MEM_SIZE)
          {
            snprintf(err, errsz, "line %d: data overflow", lineno);
            fclose(fp); return -1;
          }
          memcpy(&cpu->mem[data_ptr], &val, 4);
          data_ptr += 4;
        }
      }
      else if (strcmp(first, ".word") == 0)
      {
        for (int i = k + 1; i < n; ++i)
        {
          int32_t val = (int32_t) strtoll(toks[i], NULL, 0);
          if ((uint64_t) data_ptr + 4 > MEM_SIZE)
          {
            snprintf(err, errsz, "line %d: data overflow", lineno);
            fclose(fp); return -1;
          }
          memcpy(&cpu->mem[data_ptr], &val, 4);
          data_ptr += 4;
        }
      }
      else if (strcmp(first, ".space") == 0)
      {
        if (k + 1 >= n) { snprintf(err, errsz, "line %d: .space needs a count", lineno); fclose(fp); return -1; }
        data_ptr += (int64_t) atoll(toks[k + 1]) * 4;
      }
      else if (strcmp(first, ".res") == 0)
      {
        // .res TYPE : reserve TYPE.size bytes (size-aware .space); the label defines the symbol.
        if (k + 1 >= n) { snprintf(err, errsz, "line %d: .res needs a type name", lineno); fclose(fp); return -1; }
        char fq[80]; snprintf(fq, sizeof fq, "%s.size", toks[k + 1]);
        int64_t sz;
        if (!find_const(fq, &sz)) { snprintf(err, errsz, "line %d: unknown struct '%s'", lineno, toks[k + 1]); fclose(fp); return -1; }
        if ((uint64_t) data_ptr + sz > MEM_SIZE) { snprintf(err, errsz, "line %d: data overflow", lineno); fclose(fp); return -1; }
        data_ptr += sz;
      }
      else if (strcmp(first, ".global") == 0 || strcmp(first, ".globl") == 0 ||
               strcmp(first, ".extern") == 0)
      {
        // Linkage directives are meaningless in a single-file run; ignore
        // them so the same source assembles both legacy and via the linker.
      }
      else
      {
        int hp = handle_proc_directive(first, toks, k, n, section, lineno, err, errsz);
        if (hp < 0) { fclose(fp); return -1; }
        if (hp == 0)
        {
          int h = handle_const_directive(first, toks, k, n, lineno, err, errsz);
          if (h < 0) { fclose(fp); return -1; }
          if (h == 0)
          {
            snprintf(err, errsz, "line %d: unknown directive '%s'", lineno, first);
            fclose(fp); return -1;
          }
        }
      }
      continue;
    }

    // A text instruction: store its tokens (sans label) for pass 2.
    if (g_code_count >= MAX_INSTR)
    {
      snprintf(err, errsz, "too many instructions");
      fclose(fp); return -1;
    }
    char joined[512];
    join_tokens(toks, k, n, joined, sizeof(joined));
    g_code_lines[g_code_count] = strdup(joined);
    g_code_count += 1;
  }

  if (g_struct_active) { snprintf(err, errsz, "unterminated .struct '%s'", g_struct_name); free_code_lines(); fclose(fp); return -1; }
  if (g_proc_active) { snprintf(err, errsz, "unterminated .proc '%s'", g_proc_name); free_proc_body(); free_code_lines(); fclose(fp); return -1; }

  // ---- Pass 2: encode instructions with resolved symbols ----------------
  for (int i = 0; i < g_code_count; ++i)
  {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", g_code_lines[i]);
    char* etoks[64];
    int en = tokenize(buf, etoks, 64);
    if (en == 0) continue;
    if (encode(etoks, en, &prog[i], err, errsz) != 0)
    {
      char tmp[256];
      snprintf(tmp, sizeof(tmp), "instr %d: %s", i, err);
      snprintf(err, errsz, "%s", tmp);
      free_code_lines();
      fclose(fp);
      return -1;
    }
  }

  int count = g_code_count;
  free_code_lines();
  fclose(fp);
  return count;
}

// ---------------------------------------------------------------------------
//  Label table access (used by the tracer and interactive debugger). The
//  symbol table has static storage, so it stays valid after assemble() ends.
// ---------------------------------------------------------------------------
int vcpu_label_lookup(const char* name, int64_t* out_value)
{
  return find_symbol(name, out_value);
}

const char* vcpu_label_for_index(int index)
{
  for (int i = 0; i < g_symbol_count; ++i)
    if (g_symbols[i].is_code && g_symbols[i].value == index)
      return g_symbols[i].name;
  return NULL;
}

void vcpu_labels_clear(void)
{
  g_symbol_count = 0;
}

int vcpu_label_define(const char* name, int64_t value, int is_code)
{
  char err[64];
  return add_symbol(name, value, is_code, err, sizeof err);
}

// ---------------------------------------------------------------------------
//  Object assembly: like assemble() but produces a relocatable VObject.
//  Symbolic operands are left as placeholders and recorded as relocations;
//  data is emitted into the object's own image (not into cpu->mem).
// ---------------------------------------------------------------------------
static int is_branch_op(OpCode op)
{
  return op == OP_BEQ || op == OP_BNE || op == OP_BLT || op == OP_J ||
         op == OP_JAL || op == OP_SETHANDLER;
}

int assemble_object(const char* path, VObject* obj, const char* expanded_out,
                     char* err, size_t errsz)
{
  memset(obj, 0, sizeof(*obj));
  g_symbol_count = 0;
  g_code_count   = 0;
  g_const_count   = 0;
  g_struct_active = 0;
  g_proc_active   = 0;
  g_proc_body_count = 0;

  FILE* fp = fopen(path, "r");
  if (!fp)
  {
    snprintf(err, errsz, "cannot open '%s'", path);
    return -1;
  }

  uint8_t* data = malloc(MEM_SIZE);
  if (!data) { snprintf(err, errsz, "out of memory"); fclose(fp); return -1; }
  memset(data, 0, MEM_SIZE);

  // Declared bindings, applied to symbols after pass 1.
  char    globals[MAX_SYMBOLS][64];  int nglobal = 0;
  char    externs[MAX_SYMBOLS][64];  int nextern = 0;

  char    line[512];
  char*   toks[64];
  int     section  = SEC_TEXT;
  int64_t data_ptr = 0;
  int     lineno   = 0;

  // .include: file stack (bottom = top-level file, kept open for fclose(fp)).
  IncState inc;
  if (inc_begin(&inc, fp, path, err, errsz) != 0) goto fail;

  // ---- Pass 1: symbols, data image, bindings, keep code lines -----------
  while (inc_next_line(&inc, line, sizeof(line)))
  {
    lineno += 1;
    char work[512];
    snprintf(work, sizeof(work), "%s", line);

    int n = tokenize(work, toks, 64);
    if (n == 0) continue;

    int hb = handle_proc_body_line(line, toks, n, lineno, err, errsz);
    if (hb < 0) goto fail;
    if (hb == 1) continue;

    int k = 0;
    size_t len = strlen(toks[0]);
    if (len > 1 && toks[0][len - 1] == ':')
    {
      char name[64];
      snprintf(name, sizeof(name), "%.*s", (int) (len - 1), toks[0]);
      int64_t value = (section == SEC_DATA) ? data_ptr : g_code_count;
      if (add_symbol(name, value, section == SEC_TEXT, err, errsz) != 0) goto fail;
      k = 1;
    }
    if (k >= n) continue;

    const char* first = toks[k];
    if (first[0] == '.')
    {
      if (strcmp(first, ".text") == 0) { section = SEC_TEXT; }
      else if (strcmp(first, ".data") == 0) { section = SEC_DATA; }
      else if (strcmp(first, ".include") == 0)
      {
        if (k + 1 >= n) { snprintf(err, errsz, "line %d: .include needs a file", lineno); goto fail; }
        if (inc_push(&inc, toks[k + 1], lineno, err, errsz) < 0) goto fail;
      }
      else if (strcmp(first, ".global") == 0 || strcmp(first, ".globl") == 0)
      {
        for (int i = k + 1; i < n && nglobal < MAX_SYMBOLS; ++i)
          snprintf(globals[nglobal++], 64, "%s", toks[i]);
      }
      else if (strcmp(first, ".extern") == 0)
      {
        for (int i = k + 1; i < n && nextern < MAX_SYMBOLS; ++i)
          snprintf(externs[nextern++], 64, "%s", toks[i]);
      }
      else if (strcmp(first, ".float") == 0)
      {
        for (int i = k + 1; i < n; ++i)
        {
          float val = strtof(toks[i], NULL);
          if ((uint64_t) data_ptr + 4 > MEM_SIZE)
          {
            snprintf(err, errsz, "line %d: data overflow", lineno);
            goto fail;
          }
          memcpy(&data[data_ptr], &val, 4);
          data_ptr += 4;
        }
      }
      else if (strcmp(first, ".word") == 0)
      {
        for (int i = k + 1; i < n; ++i)
        {
          int32_t val = (int32_t) strtoll(toks[i], NULL, 0);
          if ((uint64_t) data_ptr + 4 > MEM_SIZE)
          {
            snprintf(err, errsz, "line %d: data overflow", lineno);
            goto fail;
          }
          memcpy(&data[data_ptr], &val, 4);
          data_ptr += 4;
        }
      }
      else if (strcmp(first, ".space") == 0)
      {
        if (k + 1 >= n) { snprintf(err, errsz, "line %d: .space needs a count", lineno); goto fail; }
        data_ptr += (int64_t) atoll(toks[k + 1]) * 4;
        if ((uint64_t) data_ptr > MEM_SIZE) { snprintf(err, errsz, "line %d: data overflow", lineno); goto fail; }
      }
      else if (strcmp(first, ".res") == 0)
      {
        // .res TYPE : reserve TYPE.size bytes (size-aware .space); the label defines the symbol.
        if (k + 1 >= n) { snprintf(err, errsz, "line %d: .res needs a type name", lineno); goto fail; }
        char fq[80]; snprintf(fq, sizeof fq, "%s.size", toks[k + 1]);
        int64_t sz;
        if (!find_const(fq, &sz)) { snprintf(err, errsz, "line %d: unknown struct '%s'", lineno, toks[k + 1]); goto fail; }
        data_ptr += sz;
        if ((uint64_t) data_ptr > MEM_SIZE) { snprintf(err, errsz, "line %d: data overflow", lineno); goto fail; }
      }
      else
      {
        int hp = handle_proc_directive(first, toks, k, n, section, lineno, err, errsz);
        if (hp < 0) goto fail;
        if (hp == 0)
        {
          int h = handle_const_directive(first, toks, k, n, lineno, err, errsz);
          if (h < 0) goto fail;
          if (h == 0)
          {
            snprintf(err, errsz, "line %d: unknown directive '%s'", lineno, first);
            goto fail;
          }
        }
      }
      continue;
    }

    if (g_code_count >= MAX_INSTR)
    {
      snprintf(err, errsz, "too many instructions");
      goto fail;
    }
    char joined[512];
    join_tokens(toks, k, n, joined, sizeof(joined));
    g_code_lines[g_code_count] = strdup(joined);
    g_code_count += 1;
  }

  // ---- Apply .global / .extern to the symbol table ----------------------
  if (g_struct_active) { snprintf(err, errsz, "unterminated .struct '%s'", g_struct_name); goto fail; }
  if (g_proc_active) { snprintf(err, errsz, "unterminated .proc '%s'", g_proc_name); goto fail; }
  for (int i = 0; i < nglobal; ++i)
  {
    int idx = find_symbol_idx(globals[i]);
    if (idx < 0) { snprintf(err, errsz, "'.global %s' but '%s' is not defined", globals[i], globals[i]); goto fail; }
    g_symbols[idx].binding = BIND_GLOBAL;
  }
  for (int i = 0; i < nextern; ++i)
  {
    int idx = find_symbol_idx(externs[i]);
    if (idx >= 0) { snprintf(err, errsz, "'%s' is both defined and .extern", externs[i]); goto fail; }
    if (add_symbol(externs[i], 0, 0, err, errsz) != 0) goto fail;
    idx = find_symbol_idx(externs[i]);
    g_symbols[idx].section = RSEC_NONE;
    g_symbols[idx].binding = BIND_EXTERN;
    g_symbols[idx].is_code = -1;
  }

  if (expanded_out != NULL && dump_expanded(expanded_out, err, errsz) != 0) goto fail;

  // ---- Pass 2: encode with placeholders, record relocations -------------
  g_obj_mode = 1;
  for (int i = 0; i < g_code_count; ++i)
  {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", g_code_lines[i]);
    char* etoks[64];
    int en = tokenize(buf, etoks, 64);
    if (en == 0) continue;

    g_pending_hit = 0;
    if (encode(etoks, en, &obj->text[i], err, errsz) != 0)
    {
      char tmp[256];
      snprintf(tmp, sizeof(tmp), "instr %d: %s", i, err);
      snprintf(err, errsz, "%s", tmp);
      g_obj_mode = 0;
      goto fail;
    }
    if (g_pending_hit)
    {
      ObjReloc* r = &obj->relocs[obj->reloc_count++];
      r->type   = is_branch_op(obj->text[i].op) ? R_CODE
           : (obj->text[i].op == OP_LI ? R_ADDR : R_DATA);
      r->site   = i;
      r->addend = g_pending_addend;
      snprintf(r->sym, sizeof(r->sym), "%s", g_pending_sym);
    }
  }
  g_obj_mode = 0;

  obj->text_count = g_code_count;

  // Copy symbols into the object.
  for (int i = 0; i < g_symbol_count; ++i)
  {
    ObjSym* s = &obj->syms[obj->sym_count++];
    snprintf(s->name, sizeof(s->name), "%.63s", g_symbols[i].name);
    s->section = g_symbols[i].section;
    s->offset  = g_symbols[i].value;
    s->binding = g_symbols[i].binding;
    s->is_code = g_symbols[i].is_code;
  }

  // Shrink the data image to its used size. Copy into an exact-size block
  // rather than realloc(): the scratch buffer is a full MEM_SIZE, so keeping
  // it on a failed shrink would retain 1 MiB per object, and reusing 'data'
  // after realloc() consumed it is undefined behaviour.
  obj->data_count = data_ptr;
  if (data_ptr > 0)
  {
    obj->data = malloc((size_t) data_ptr);
    if (!obj->data) { snprintf(err, errsz, "out of memory"); goto fail; }
    memcpy(obj->data, data, (size_t) data_ptr);
    free(data);
  }
  else
  {
    free(data);
    obj->data = NULL;
  }

  free_code_lines();
  fclose(fp);
  return 0;

fail:
  free(data);
  free_proc_body();
  free_code_lines();
  inc_cleanup(&inc);   // nested includes still open; fp is closed just below
  fclose(fp);
  return -1;
}

void vobject_free(VObject* obj)
{
  if (!obj) return;
  free(obj->data);
  obj->data = NULL;
}
