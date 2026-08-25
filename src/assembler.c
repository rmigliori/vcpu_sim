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
    char* dend;
    *disp = (int64_t) strtoll(dbuf, &dend, 0);
    if (*dend != '\0')
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
#define R(prefix, count) do {                                    \
        int _idx = parse_reg(toks[k++], prefix, count, err, errsz); \
        if (_idx < 0) return -1;                                  \
        reg = _idx;                                               \
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

static void join_tokens(char** toks, int start, int n, char* out, size_t outsz)
{
    out[0] = '\0';
    for (int i = start; i < n; ++i)
    {
        strncat(out, toks[i], outsz - strlen(out) - 1);
        if (i + 1 < n) strncat(out, " ", outsz - strlen(out) - 1);
    }
}

int assemble(const char* path, VCpu* cpu, Instr* prog, char* err, size_t errsz)
{
    g_symbol_count = 0;
    g_code_count   = 0;

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
    int64_t data_ptr = 0;
    int     lineno   = 0;

    while (fgets(line, sizeof(line), fp))
    {
        lineno += 1;
        char work[512];
        snprintf(work, sizeof(work), "%s", line);

        int n = tokenize(work, toks, 64);
        if (n == 0) continue;

        int k = 0;
        // Optional leading label "name:"
        size_t len = strlen(toks[0]);
        if (len > 1 && toks[0][len - 1] == ':')
        {
            char name[64];
            snprintf(name, sizeof(name), "%.*s", (int) (len - 1), toks[0]);
            int64_t value = (section == SEC_DATA) ? data_ptr : g_code_count;
            if (add_symbol(name, value, section == SEC_TEXT, err, errsz) != 0) { fclose(fp); return -1; }
            k = 1;
        }
        if (k >= n) continue;  // label-only line

        const char* first = toks[k];
        if (first[0] == '.')
        {
            if (strcmp(first, ".text") == 0) { section = SEC_TEXT; }
            else if (strcmp(first, ".data") == 0) { section = SEC_DATA; }
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
            else if (strcmp(first, ".global") == 0 || strcmp(first, ".globl") == 0 ||
                     strcmp(first, ".extern") == 0)
            {
                // Linkage directives are meaningless in a single-file run; ignore
                // them so the same source assembles both legacy and via the linker.
            }
            else
            {
                snprintf(err, errsz, "line %d: unknown directive '%s'", lineno, first);
                fclose(fp); return -1;
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

int assemble_object(const char* path, VObject* obj, char* err, size_t errsz)
{
    memset(obj, 0, sizeof(*obj));
    g_symbol_count = 0;
    g_code_count   = 0;

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

    // ---- Pass 1: symbols, data image, bindings, keep code lines -----------
    while (fgets(line, sizeof(line), fp))
    {
        lineno += 1;
        char work[512];
        snprintf(work, sizeof(work), "%s", line);

        int n = tokenize(work, toks, 64);
        if (n == 0) continue;

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
            else
            {
                snprintf(err, errsz, "line %d: unknown directive '%s'", lineno, first);
                goto fail;
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

    // Shrink the data image to its used size.
    obj->data_count = data_ptr;
    if (data_ptr > 0)
    {
        obj->data = realloc(data, (size_t) data_ptr);
        if (!obj->data) obj->data = data;  // keep the oversized buffer on failure
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
    free_code_lines();
    fclose(fp);
    return -1;
}

void vobject_free(VObject* obj)
{
    if (!obj) return;
    free(obj->data);
    obj->data = NULL;
}
