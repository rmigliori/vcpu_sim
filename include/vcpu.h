#ifndef VCPU_H
#define VCPU_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
//  Machine configuration
// ---------------------------------------------------------------------------
#define NUM_SCALAR 16   // integer registers r0..r15 (r0 is hardwired to 0)
#define NUM_FLOAT  16   // scalar float registers f0..f15
#define NUM_VECTOR 8    // vector registers v0..v7
#define VLMAX      64   // elements per vector register
#define MEM_SIZE   (1u << 20)  // 1 MiB byte-addressable memory

#define MAX_INSTR   4096
#define MAX_SYMBOLS 512

// Program status word bits (scalar control layer).
#define PSW_IE 0x1ULL   // interrupt enable

// ---------------------------------------------------------------------------
//  Timing model (first-order, in-order, no chaining/overlap)
//    - scalar op        : fixed latency
//    - vector op         : startup (pipeline fill) + ceil(VL / VEC_LANES)
//  The vector startup is paid once per instruction, so it amortizes over long
//  vectors -- the key reason vector machines win on regular data-parallel work.
// ---------------------------------------------------------------------------
#define CYC_SCALAR_ALU     1   // integer ALU / li / mov / setvl
#define CYC_SCALAR_BR      1   // branch / jump
#define CYC_SCALAR_DIV     20  // integer divide / remainder (multi-cycle)
#define CYC_SCALAR_FP      4   // scalar floating-point ALU
#define CYC_SCALAR_MEM     4   // scalar load / store
#define VEC_LANES          1   // parallel element lanes in the vector unit
#define VEC_ARITH_STARTUP  6   // pipeline fill for a vector arithmetic op
#define VEC_MEM_STARTUP    12  // pipeline fill for a vector memory op

// ---------------------------------------------------------------------------
//  Instruction set
// ---------------------------------------------------------------------------
typedef enum
{
    OP_LI,      // a=rd,             imm         -> r[rd] = imm
    OP_MOV,     // a=rd, b=rs1                   -> r[rd] = r[rs1]
    OP_ADD,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] + r[rs2]
    OP_SUB,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] - r[rs2]
    OP_MUL,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] * r[rs2]
    OP_ADDI,    // a=rd, b=rs1,     imm          -> r[rd] = r[rs1] + imm
    OP_SLLI,    // a=rd, b=rs1,     imm          -> r[rd] = r[rs1] << imm
    OP_SRLI,    // a=rd, b=rs1,     imm          -> r[rd] = (uint64) r[rs1] >> imm
    OP_AND,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] & r[rs2]
    OP_OR,      // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] | r[rs2]
    OP_XOR,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] ^ r[rs2]
    OP_DIV,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] / r[rs2]  (0 if rs2==0)
    OP_REM,     // a=rd, b=rs1, c=rs2            -> r[rd] = r[rs1] % r[rs2]  (0 if rs2==0)

    OP_FLI,     // a=fd,            fimm         -> f[fd] = fimm
    OP_FLW,     // a=fd, b=rs1, imm=disp        -> f[fd] = mem_float[r[rs1] + disp]
    OP_FSW,     // a=fs, b=rs1, imm=disp        -> mem_float[r[rs1] + disp] = f[fs]
    OP_FADD,    // a=fd, b=fs1, c=fs2            -> f[fd] = f[fs1] + f[fs2]
    OP_FMUL,    // a=fd, b=fs1, c=fs2            -> f[fd] = f[fs1] * f[fs2]
    OP_FMACC,   // a=fd, b=fs1, c=fs2            -> f[fd] += f[fs1] * f[fs2]
    OP_FMOV,    // a=fd, b=fs1                   -> f[fd] = f[fs1]
    OP_FMIN,    // a=fd, b=fs1, c=fs2            -> f[fd] = min(f[fs1], f[fs2])
    OP_FMAX,    // a=fd, b=fs1, c=fs2            -> f[fd] = max(f[fs1], f[fs2])
    OP_FSUB,    // a=fd, b=fs1, c=fs2            -> f[fd] = f[fs1] - f[fs2]
    OP_FDIV,    // a=fd, b=fs1, c=fs2            -> f[fd] = f[fs1] / f[fs2]  (0 if fs2==0)
    OP_FNEG,    // a=fd, b=fs1                   -> f[fd] = -f[fs1]
    OP_FSQRT,   // a=fd, b=fs1                   -> f[fd] = sqrt(f[fs1])

    OP_LW,      // a=rd, b=rs1, imm=disp        -> r[rd] = mem_int32[r[rs1] + disp]  (sign-extended)
    OP_SW,      // a=rs, b=rs1, imm=disp        -> mem_int32[r[rs1] + disp] = (int32) r[rs]

    OP_SETVL,   // a=rd, b=rs1                   -> VL = min(r[rs1], VLMAX); r[rd] = VL

    OP_BEQ,     // b=rs1, c=rs2, target          -> if r[rs1]==r[rs2] pc=target
    OP_BNE,     // b=rs1, c=rs2, target          -> if r[rs1]!=r[rs2] pc=target
    OP_BLT,     // b=rs1, c=rs2, target          -> if r[rs1]< r[rs2] pc=target
    OP_J,       //               target          -> pc=target
    OP_JAL,     // a=rd,         target          -> r[rd]=pc(next); pc=target   (jump-and-link / call)
    OP_JALR,    // a=rd, b=rs1                    -> r[rd]=pc(next); pc=r[rs1]   (indirect / ret)

    OP_STI,        //                            -> psw |= IE   (enable interrupts)
    OP_CLI,        //                            -> psw &= ~IE  (disable interrupts)
    OP_RETI,       //                            -> pc = epc; psw = epsw       (return from interrupt)
    OP_SETHANDLER, //             target          -> handler = target (instruction index)
    OP_SETTIMER,   // b=rs1                       -> timer_period = r[rs1]; arm at cycles+r[rs1]
    OP_MFPSW,      // a=rd                        -> r[rd] = psw
    OP_MTPSW,      // b=rs1                       -> psw = r[rs1]
    OP_MFEPC,      // a=rd                        -> r[rd] = epc
    OP_MTEPC,      // b=rs1                       -> epc = r[rs1]

    OP_HALT,    //                               -> stop

    OP_VLOAD,   // a=vd, b=rs1                   -> unit-stride load VL floats from r[rs1]
    OP_VLOADS,  // a=vd, b=rs1, c=rs2            -> strided load, stride bytes = r[rs2]
    OP_VSTORE,  // a=vs, b=rs1                   -> unit-stride store VL floats to r[rs1]
    OP_VLOADX,  // a=vd, b=rs1, c=vidx           -> gather:  v[vd][i] = mem_float[r[rs1] + v[vidx][i]*4]
    OP_VSTOREX, // a=vs, b=rs1, c=vidx           -> scatter: mem_float[r[rs1] + v[vidx][i]*4] = v[vs][i]
    OP_VADD,    // a=vd, b=vs1, c=vs2            -> v[vd][i] = v[vs1][i] + v[vs2][i]
    OP_VSUB,    // a=vd, b=vs1, c=vs2            -> v[vd][i] = v[vs1][i] - v[vs2][i]
    OP_VMUL,    // a=vd, b=vs1, c=vs2            -> v[vd][i] = v[vs1][i] * v[vs2][i]
    OP_VMACC,   // a=vd, b=fs,  c=vs1            -> v[vd][i] += f[fs] * v[vs1][i]
    OP_VSCALE,  // a=vd, b=vs1, c=fs             -> v[vd][i] = v[vs1][i] * f[fs]
    OP_VREDSUM, // a=fd, b=vs                    -> f[fd] = sum of first VL elements of v[vs]
    OP_VREDMAX, // a=fd, b=vs                    -> f[fd] = max of first VL elements of v[vs]
    OP_VREDMIN, // a=fd, b=vs                    -> f[fd] = min of first VL elements of v[vs]
    OP_VSPLAT,  // a=vd, b=fs                    -> v[vd][i] = f[fs]  (broadcast)
    OP_VADDS,   // a=vd, b=vs1, c=fs             -> v[vd][i] = v[vs1][i] + f[fs]  (add scalar)
    OP_VMIN,    // a=vd, b=vs1, c=vs2            -> v[vd][i] = min(v[vs1][i], v[vs2][i])
    OP_VMAX,    // a=vd, b=vs1, c=vs2            -> v[vd][i] = max(v[vs1][i], v[vs2][i])

    OP_VMSLT,   // b=vs1, c=vs2                  -> vmask bit i = v[vs1][i] <  v[vs2][i]
    OP_VMSGT,   // b=vs1, c=vs2                  -> vmask bit i = v[vs1][i] >  v[vs2][i]
    OP_VMSEQ,   // b=vs1, c=vs2                  -> vmask bit i = v[vs1][i] == v[vs2][i]
    OP_VMERGE,  // a=vd, b=vs1, c=vs2            -> v[vd][i] = (vmask bit i) ? v[vs1][i] : v[vs2][i]

    OP_VADDM,   // a=vd, b=vs1, c=vs2            -> masked: v[vd][i] = mask[i] ? v[vs1][i]+v[vs2][i] : v[vd][i]
    OP_VSUBM,   // a=vd, b=vs1, c=vs2            -> masked: v[vd][i] = mask[i] ? v[vs1][i]-v[vs2][i] : v[vd][i]
    OP_VMULM,   // a=vd, b=vs1, c=vs2            -> masked: v[vd][i] = mask[i] ? v[vs1][i]*v[vs2][i] : v[vd][i]
    OP_VSTOREM, // a=vs, b=rs1                   -> masked unit-stride store: writes lane i only if mask[i]

    OP_DUMPS,   // b=rs1                         -> print scalar register
    OP_DUMPF,   // b=fs                          -> print float register
    OP_DUMPV,   // a=vs                          -> print first VL elements of a vector
    OP_DUMPM,   // b=rs1,           imm=count    -> print 'count' floats from memory at r[rs1]
    OP_DUMPMASK // (no operands)                 -> print first VL bits of vmask
} OpCode;

typedef struct
{
    OpCode  op;
    int     a, b, c;   // register indices; meaning depends on op (see comments above)
    int64_t imm;       // integer immediate
    double  fimm;      // float immediate
    int     target;    // branch/jump target (instruction index)
} Instr;

// ---------------------------------------------------------------------------
//  Machine state
// ---------------------------------------------------------------------------
typedef struct
{
    int64_t r[NUM_SCALAR];
    double  f[NUM_FLOAT];
    float   v[NUM_VECTOR][VLMAX];
    int     vl;
    uint64_t vmask;   // per-element predicate written by vms* compares (bit i = element i)
    uint8_t mem[MEM_SIZE];

    int64_t pc;
    int     halted;

    // Interrupt / status word (scalar control layer). All zero at reset, so a
    // program that never enables interrupts runs exactly as before.
    uint64_t psw;          // program status word: bit 0 = IE (interrupt enable)
    int64_t  epc;          // PC saved on trap
    uint64_t epsw;         // PSW saved on trap (the shadow copy)
    int64_t  handler;      // trap vector (instruction index)
    int64_t  timer_period; // cycles between timer interrupts (0 = disabled)
    uint64_t timer_next;   // cycle count at which the next timer interrupt is due

    // statistics
    uint64_t instr_count;    // total executed instructions
    uint64_t vec_elem_ops;   // total per-element vector operations (measure of SIMD work)
    uint64_t cycles;         // total cycles under the timing model
} VCpu;

// ---------------------------------------------------------------------------
//  API
// ---------------------------------------------------------------------------
void vcpu_init(VCpu* cpu);

// Assemble a .vasm file into 'prog'. Returns number of instructions, or -1 on
// error (message written to 'err'). Data directives are written into cpu->mem.
int assemble(const char* path, VCpu* cpu, Instr* prog, char* err, size_t errsz);

// Execute the program until HALT or end of program.
void vcpu_run(VCpu* cpu, const Instr* prog, int prog_len);

// Run modes: normal, per-instruction trace, or interactive step debugger.
typedef enum { RUN_NORMAL, RUN_TRACE, RUN_DEBUG } RunMode;
void vcpu_run_ex(VCpu* cpu, const Instr* prog, int prog_len, RunMode mode);

// Like vcpu_run_ex but starts execution at 'entry' (instruction index).
void vcpu_run_from(VCpu* cpu, const Instr* prog, int prog_len, RunMode mode, int64_t entry);

// Render one instruction to human-readable assembly text. Returns 'buf'.
const char* vcpu_disasm(const Instr* in, char* buf, size_t bufsz);

// Label table access (populated by the last assemble()). Code labels map to
// instruction indices, data labels to memory addresses.
int         vcpu_label_lookup(const char* name, int64_t* out_value);
const char* vcpu_label_for_index(int index);

// Populate the label table directly (used when loading a linked .vx image so
// that --trace/--debug can annotate and break on global symbols).
void        vcpu_labels_clear(void);
int         vcpu_label_define(const char* name, int64_t value, int is_code);

#endif // VCPU_H
