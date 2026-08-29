#include "vcpu.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vcpu_init(VCpu* cpu)
{
  memset(cpu, 0, sizeof(*cpu));
  cpu->vl = 0;
  cpu->r[0] = 0;  // hardwired zero
}

// ---------------------------------------------------------------------------
//  Memory helpers (element size is 4 bytes / one float)
// ---------------------------------------------------------------------------
static float load_f32(const VCpu* cpu, int64_t addr)
{
  float value = 0.0f;
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: float load out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return 0.0f;
  }
  memcpy(&value, &cpu->mem[addr], sizeof(value));
  return value;
}

static void store_f32(VCpu* cpu, int64_t addr, float value)
{
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: float store out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return;
  }
  memcpy(&cpu->mem[addr], &value, sizeof(value));
}

static int32_t load_i32(const VCpu* cpu, int64_t addr)
{
  int32_t value = 0;
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: word load out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return 0;
  }
  memcpy(&value, &cpu->mem[addr], sizeof(value));
  return value;
}

static void store_i32(VCpu* cpu, int64_t addr, int32_t value)
{
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: word store out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return;
  }
  memcpy(&cpu->mem[addr], &value, sizeof(value));
}

// Write to a scalar register, honouring the hardwired-zero r0.
static void set_scalar(VCpu* cpu, int rd, int64_t value)
{
  if (rd != 0)
  {
    cpu->r[rd] = value;
  }
}

// ---------------------------------------------------------------------------
//  Timing model: cost (in cycles) of a single instruction. Vector ops depend
//  on the current VL.
// ---------------------------------------------------------------------------
static uint64_t instr_cost(const Instr* in, int vl)
{
  uint64_t lanes_pass = (uint64_t) ((vl + VEC_LANES - 1) / VEC_LANES);

  switch (in->op)
  {
    case OP_LI: case OP_MOV: case OP_ADD: case OP_SUB: case OP_MUL:
    case OP_ADDI: case OP_SLLI: case OP_SRLI:
    case OP_AND: case OP_OR: case OP_XOR:
    case OP_SETVL: case OP_FLI:
    case OP_STI: case OP_CLI: case OP_SETHANDLER: case OP_SETTIMER:
    case OP_MFPSW: case OP_MTPSW: case OP_MFEPC: case OP_MTEPC:
      return CYC_SCALAR_ALU;

    case OP_DIV: case OP_REM:
      return CYC_SCALAR_DIV;

    case OP_BEQ: case OP_BNE: case OP_BLT: case OP_J:
    case OP_JAL: case OP_JALR: case OP_RETI: case OP_HALT:
      return CYC_SCALAR_BR;

    case OP_FLW: case OP_FSW: case OP_LW: case OP_SW:
      return CYC_SCALAR_MEM;
    case OP_FADD: case OP_FMUL: case OP_FMACC: case OP_FMOV:
    case OP_FMIN: case OP_FMAX: case OP_FSUB: case OP_FDIV:
    case OP_FNEG: case OP_FSQRT:
      return CYC_SCALAR_FP;

    case OP_VLOAD: case OP_VLOADS: case OP_VSTORE: case OP_VSTOREM:
      return VEC_MEM_STARTUP + lanes_pass;

    case OP_VLOADX: case OP_VSTOREX:
      // Gather/scatter touch scattered addresses, so they cost an extra
      // pass over the lanes compared to a unit-stride access.
      return VEC_MEM_STARTUP + 2 * lanes_pass;

    case OP_VADD: case OP_VSUB: case OP_VMUL: case OP_VMACC: case OP_VSCALE:
    case OP_VSPLAT: case OP_VADDS: case OP_VMIN: case OP_VMAX:
    case OP_VMSLT: case OP_VMSGT: case OP_VMSEQ: case OP_VMERGE:
    case OP_VADDM: case OP_VSUBM: case OP_VMULM:
      return VEC_ARITH_STARTUP + lanes_pass;

    case OP_VREDSUM:
    {
      // Read all VL elements, then combine them in a reduction tree of
      // depth ceil(log2(VL)) -- the extra cost that makes reductions less
      // efficient than element-wise vector ops.
      uint64_t steps = 0;
      for (int n = vl; n > 1; n = (n + 1) / 2) ++steps;
      return VEC_ARITH_STARTUP + lanes_pass + steps;
    }
    case OP_VREDMAX: case OP_VREDMIN:
    {
      uint64_t steps = 0;
      for (int n = vl; n > 1; n = (n + 1) / 2) ++steps;
      return VEC_ARITH_STARTUP + lanes_pass + steps;
    }

    case OP_DUMPS: case OP_DUMPF: case OP_DUMPV: case OP_DUMPM:
    case OP_DUMPMASK:
      return 0;  // debugging aids are free
  }
  return 1;}

// ---------------------------------------------------------------------------
//  Single-instruction execution. The caller has already advanced cpu->pc, so
//  branches/jumps simply overwrite it.
// ---------------------------------------------------------------------------
static void execute(VCpu* cpu, const Instr* in)
{
    switch (in->op)
    {
      case OP_LI:   set_scalar(cpu, in->a, in->imm);                       break;
      case OP_MOV:  set_scalar(cpu, in->a, cpu->r[in->b]);                 break;
      case OP_ADD:  set_scalar(cpu, in->a, cpu->r[in->b] + cpu->r[in->c]); break;
      case OP_SUB:  set_scalar(cpu, in->a, cpu->r[in->b] - cpu->r[in->c]); break;
      case OP_MUL:  set_scalar(cpu, in->a, cpu->r[in->b] * cpu->r[in->c]); break;
      case OP_ADDI: set_scalar(cpu, in->a, cpu->r[in->b] + in->imm);       break;
      case OP_SLLI: set_scalar(cpu, in->a, cpu->r[in->b] << in->imm);      break;
      case OP_SRLI: set_scalar(cpu, in->a, (int64_t) ((uint64_t) cpu->r[in->b] >> in->imm)); break;
      case OP_AND:  set_scalar(cpu, in->a, cpu->r[in->b] & cpu->r[in->c]); break;
      case OP_OR:   set_scalar(cpu, in->a, cpu->r[in->b] | cpu->r[in->c]); break;
      case OP_XOR:  set_scalar(cpu, in->a, cpu->r[in->b] ^ cpu->r[in->c]); break;
      case OP_DIV:  set_scalar(cpu, in->a, cpu->r[in->c] ? cpu->r[in->b] / cpu->r[in->c] : 0); break;
      case OP_REM:  set_scalar(cpu, in->a, cpu->r[in->c] ? cpu->r[in->b] % cpu->r[in->c] : 0); break;

      case OP_FLI:  cpu->f[in->a] = in->fimm;                              break;
      case OP_FLW:  cpu->f[in->a] = load_f32(cpu, cpu->r[in->b] + in->imm);          break;
      case OP_FSW:  store_f32(cpu, cpu->r[in->b] + in->imm, (float) cpu->f[in->a]);   break;
      case OP_FADD: cpu->f[in->a] = cpu->f[in->b] + cpu->f[in->c];         break;
      case OP_FMUL: cpu->f[in->a] = cpu->f[in->b] * cpu->f[in->c];         break;
      case OP_FMACC: cpu->f[in->a] += cpu->f[in->b] * cpu->f[in->c];       break;
      case OP_FMOV: cpu->f[in->a] = cpu->f[in->b];                         break;
      case OP_FMIN: cpu->f[in->a] = (cpu->f[in->b] < cpu->f[in->c]) ? cpu->f[in->b] : cpu->f[in->c]; break;
      case OP_FMAX: cpu->f[in->a] = (cpu->f[in->b] > cpu->f[in->c]) ? cpu->f[in->b] : cpu->f[in->c]; break;
      case OP_FSUB: cpu->f[in->a] = cpu->f[in->b] - cpu->f[in->c];         break;
      case OP_FDIV: cpu->f[in->a] = (cpu->f[in->c] != 0.0) ? cpu->f[in->b] / cpu->f[in->c] : 0.0; break;
      case OP_FNEG: cpu->f[in->a] = -cpu->f[in->b];                        break;
      case OP_FSQRT: cpu->f[in->a] = sqrt(cpu->f[in->b]);                  break;

      case OP_LW:   set_scalar(cpu, in->a, load_i32(cpu, cpu->r[in->b] + in->imm));  break;
      case OP_SW:   store_i32(cpu, cpu->r[in->b] + in->imm, (int32_t) cpu->r[in->a]); break;

      case OP_SETVL:
      {
        int64_t req = cpu->r[in->b];
        int vl = (req < 0) ? 0 : (req > VLMAX ? VLMAX : (int) req);
        cpu->vl = vl;
        set_scalar(cpu, in->a, vl);
        break;
      }

      case OP_BEQ: if (cpu->r[in->b] == cpu->r[in->c]) cpu->pc = in->target; break;
      case OP_BNE: if (cpu->r[in->b] != cpu->r[in->c]) cpu->pc = in->target; break;
      case OP_BLT: if (cpu->r[in->b] <  cpu->r[in->c]) cpu->pc = in->target; break;
      case OP_J:   cpu->pc = in->target;                                     break;
      case OP_JAL: set_scalar(cpu, in->a, cpu->pc); cpu->pc = in->target;     break;
      case OP_JALR:
      {
        int64_t tgt = cpu->r[in->b];   // read rs1 before writing rd (may alias)
        set_scalar(cpu, in->a, cpu->pc);
        cpu->pc = tgt;
        break;
      }
      case OP_STI:  cpu->psw |=  PSW_IE; break;
      case OP_CLI:  cpu->psw &= ~PSW_IE; break;
      case OP_RETI: cpu->pc = cpu->epc; cpu->psw = cpu->epsw; break;
      case OP_SETHANDLER: cpu->handler = in->target; break;
      case OP_SETTIMER:
      {
        int64_t p = cpu->r[in->b];
        cpu->timer_period = p;
        cpu->timer_next   = cpu->cycles + (uint64_t) (p > 0 ? p : 0);
        break;
      }
      case OP_MFPSW: set_scalar(cpu, in->a, (int64_t) cpu->psw); break;
      case OP_MTPSW: cpu->psw = (uint64_t) cpu->r[in->b];        break;
      case OP_MFEPC: set_scalar(cpu, in->a, cpu->epc);           break;
      case OP_MTEPC: cpu->epc = cpu->r[in->b];                   break;
      case OP_HALT: cpu->halted = 1;                                         break;

      case OP_VLOAD:
      {
        int64_t base = cpu->r[in->b];
        for (int i = 0; i < cpu->vl; ++i)
        {
          cpu->v[in->a][i] = load_f32(cpu, base + (int64_t) i * 4);
        }
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VLOADS:
      {
        int64_t base   = cpu->r[in->b];
        int64_t stride = cpu->r[in->c];
        for (int i = 0; i < cpu->vl; ++i)
        {
          cpu->v[in->a][i] = load_f32(cpu, base + (int64_t) i * stride);
        }
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VSTORE:
      {
        int64_t base = cpu->r[in->b];
        for (int i = 0; i < cpu->vl; ++i)
        {
          store_f32(cpu, base + (int64_t) i * 4, cpu->v[in->a][i]);
        }
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VLOADX:
      {
        int64_t base = cpu->r[in->b];
        for (int i = 0; i < cpu->vl; ++i)
        {
          int64_t off = (int64_t) ((int) cpu->v[in->c][i]) * 4;
          cpu->v[in->a][i] = load_f32(cpu, base + off);
        }
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VSTOREX:
      {
        int64_t base = cpu->r[in->b];
        for (int i = 0; i < cpu->vl; ++i)
        {
          int64_t off = (int64_t) ((int) cpu->v[in->c][i]) * 4;
          store_f32(cpu, base + off, cpu->v[in->a][i]);
        }
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VADD:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = cpu->v[in->b][i] + cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VSUB:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = cpu->v[in->b][i] - cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMUL:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = cpu->v[in->b][i] * cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMACC:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] += (float) (cpu->f[in->b] * cpu->v[in->c][i]);
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VSCALE:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = (float) (cpu->v[in->b][i] * cpu->f[in->c]);
        cpu->vec_elem_ops += cpu->vl;
        break;

      case OP_VREDSUM:
      {
        double acc = 0.0;
        for (int i = 0; i < cpu->vl; ++i) acc += cpu->v[in->b][i];
        cpu->f[in->a] = acc;
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VREDMAX:
      {
        double acc = (cpu->vl > 0) ? cpu->v[in->b][0] : 0.0;
        for (int i = 1; i < cpu->vl; ++i)
          if (cpu->v[in->b][i] > acc) acc = cpu->v[in->b][i];
        cpu->f[in->a] = acc;
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VREDMIN:
      {
        double acc = (cpu->vl > 0) ? cpu->v[in->b][0] : 0.0;
        for (int i = 1; i < cpu->vl; ++i)
          if (cpu->v[in->b][i] < acc) acc = cpu->v[in->b][i];
        cpu->f[in->a] = acc;
        cpu->vec_elem_ops += cpu->vl;
        break;
      }
      case OP_VSPLAT:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = (float) cpu->f[in->b];
        cpu->vec_elem_ops += cpu->vl;
        break;

      case OP_VADDS:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = (float) (cpu->v[in->b][i] + cpu->f[in->c]);
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMIN:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = (cpu->v[in->b][i] < cpu->v[in->c][i]) ? cpu->v[in->b][i] : cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMAX:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = (cpu->v[in->b][i] > cpu->v[in->c][i]) ? cpu->v[in->b][i] : cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;

      case OP_VMSLT:
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->v[in->b][i] < cpu->v[in->c][i]) cpu->vmask |= (1ULL << i);
          else                                     cpu->vmask &= ~(1ULL << i);
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMSGT:
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->v[in->b][i] > cpu->v[in->c][i]) cpu->vmask |= (1ULL << i);
          else                                     cpu->vmask &= ~(1ULL << i);
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMSEQ:
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->v[in->b][i] == cpu->v[in->c][i]) cpu->vmask |= (1ULL << i);
          else                                      cpu->vmask &= ~(1ULL << i);
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMERGE:
        for (int i = 0; i < cpu->vl; ++i)
          cpu->v[in->a][i] = (cpu->vmask & (1ULL << i)) ? cpu->v[in->b][i] : cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;

      case OP_VADDM:
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->vmask & (1ULL << i))
            cpu->v[in->a][i] = cpu->v[in->b][i] + cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VSUBM:
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->vmask & (1ULL << i))
            cpu->v[in->a][i] = cpu->v[in->b][i] - cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VMULM:
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->vmask & (1ULL << i))
            cpu->v[in->a][i] = cpu->v[in->b][i] * cpu->v[in->c][i];
        cpu->vec_elem_ops += cpu->vl;
        break;
      case OP_VSTOREM:
      {
        int64_t base = cpu->r[in->b];
        for (int i = 0; i < cpu->vl; ++i)
          if (cpu->vmask & (1ULL << i))
            store_f32(cpu, base + (int64_t) i * 4, cpu->v[in->a][i]);
        cpu->vec_elem_ops += cpu->vl;
        break;
      }

      case OP_DUMPS:
        printf("r%d = %lld\n", in->b, (long long) cpu->r[in->b]);
        break;
      case OP_DUMPF:
        printf("f%d = %g\n", in->b, cpu->f[in->b]);
        break;
      case OP_DUMPV:
        printf("v%d [VL=%d] =", in->a, cpu->vl);
        for (int i = 0; i < cpu->vl; ++i) printf(" %g", cpu->v[in->a][i]);
        printf("\n");
        break;
      case OP_DUMPM:
      {
        int64_t base = cpu->r[in->b];
        printf("mem[r%d=0x%llx] =", in->b, (unsigned long long) base);
        for (int i = 0; i < in->imm; ++i)
          printf(" %g", load_f32(cpu, base + (int64_t) i * 4));
        printf("\n");
        break;
      }
      case OP_DUMPMASK:
        printf("vmask [VL=%d] =", cpu->vl);
        for (int i = 0; i < cpu->vl; ++i)
          printf(" %d", (int) ((cpu->vmask >> i) & 1));
        printf("\n");
        break;
    }
}

// ---------------------------------------------------------------------------
//  Disassembler: render an Instr back into readable assembly text.
// ---------------------------------------------------------------------------
const char* vcpu_disasm(const Instr* in, char* buf, size_t bufsz)
{
  switch (in->op)
  {
    case OP_LI:     snprintf(buf, bufsz, "li r%d, %lld", in->a, (long long) in->imm); break;
    case OP_MOV:    snprintf(buf, bufsz, "mov r%d, r%d", in->a, in->b); break;
    case OP_ADD:    snprintf(buf, bufsz, "add r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_SUB:    snprintf(buf, bufsz, "sub r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_MUL:    snprintf(buf, bufsz, "mul r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_ADDI:   snprintf(buf, bufsz, "addi r%d, r%d, %lld", in->a, in->b, (long long) in->imm); break;
    case OP_SLLI:   snprintf(buf, bufsz, "slli r%d, r%d, %lld", in->a, in->b, (long long) in->imm); break;
    case OP_SRLI:   snprintf(buf, bufsz, "srli r%d, r%d, %lld", in->a, in->b, (long long) in->imm); break;
    case OP_AND:    snprintf(buf, bufsz, "and r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_OR:     snprintf(buf, bufsz, "or r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_XOR:    snprintf(buf, bufsz, "xor r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_DIV:    snprintf(buf, bufsz, "div r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_REM:    snprintf(buf, bufsz, "rem r%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_FLI:    snprintf(buf, bufsz, "fli f%d, %g", in->a, in->fimm); break;
    case OP_FLW:
      if (in->imm) snprintf(buf, bufsz, "flw f%d, %lld(r%d)", in->a, (long long) in->imm, in->b);
      else         snprintf(buf, bufsz, "flw f%d, r%d", in->a, in->b);
      break;
    case OP_FSW:
      if (in->imm) snprintf(buf, bufsz, "fsw f%d, %lld(r%d)", in->a, (long long) in->imm, in->b);
      else         snprintf(buf, bufsz, "fsw f%d, r%d", in->a, in->b);
      break;
    case OP_FADD:   snprintf(buf, bufsz, "fadd f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FMUL:   snprintf(buf, bufsz, "fmul f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FMACC:  snprintf(buf, bufsz, "fmacc f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FMOV:   snprintf(buf, bufsz, "fmov f%d, f%d", in->a, in->b); break;
    case OP_FMIN:   snprintf(buf, bufsz, "fmin f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FMAX:   snprintf(buf, bufsz, "fmax f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FSUB:   snprintf(buf, bufsz, "fsub f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FDIV:   snprintf(buf, bufsz, "fdiv f%d, f%d, f%d", in->a, in->b, in->c); break;
    case OP_FNEG:   snprintf(buf, bufsz, "fneg f%d, f%d", in->a, in->b); break;
    case OP_FSQRT:  snprintf(buf, bufsz, "fsqrt f%d, f%d", in->a, in->b); break;
    case OP_LW:
      if (in->imm) snprintf(buf, bufsz, "lw r%d, %lld(r%d)", in->a, (long long) in->imm, in->b);
      else         snprintf(buf, bufsz, "lw r%d, r%d", in->a, in->b);
      break;
    case OP_SW:
      if (in->imm) snprintf(buf, bufsz, "sw r%d, %lld(r%d)", in->a, (long long) in->imm, in->b);
      else         snprintf(buf, bufsz, "sw r%d, r%d", in->a, in->b);
      break;
    case OP_SETVL:  snprintf(buf, bufsz, "setvl r%d, r%d", in->a, in->b); break;
    case OP_BEQ:    snprintf(buf, bufsz, "beq r%d, r%d, %d", in->b, in->c, in->target); break;
    case OP_BNE:    snprintf(buf, bufsz, "bne r%d, r%d, %d", in->b, in->c, in->target); break;
    case OP_BLT:    snprintf(buf, bufsz, "blt r%d, r%d, %d", in->b, in->c, in->target); break;
    case OP_J:      snprintf(buf, bufsz, "j %d", in->target); break;
    case OP_JAL:    snprintf(buf, bufsz, "jal r%d, %d", in->a, in->target); break;
    case OP_JALR:   snprintf(buf, bufsz, "jalr r%d, r%d", in->a, in->b); break;
    case OP_STI:        snprintf(buf, bufsz, "sti"); break;
    case OP_CLI:        snprintf(buf, bufsz, "cli"); break;
    case OP_RETI:       snprintf(buf, bufsz, "reti"); break;
    case OP_SETHANDLER: snprintf(buf, bufsz, "sethandler %d", in->target); break;
    case OP_SETTIMER:   snprintf(buf, bufsz, "settimer r%d", in->b); break;
    case OP_MFPSW:      snprintf(buf, bufsz, "mfpsw r%d", in->a); break;
    case OP_MTPSW:      snprintf(buf, bufsz, "mtpsw r%d", in->b); break;
    case OP_MFEPC:      snprintf(buf, bufsz, "mfepc r%d", in->a); break;
    case OP_MTEPC:      snprintf(buf, bufsz, "mtepc r%d", in->b); break;
    case OP_HALT:   snprintf(buf, bufsz, "halt"); break;
    case OP_VLOAD:  snprintf(buf, bufsz, "vload v%d, r%d", in->a, in->b); break;
    case OP_VLOADS: snprintf(buf, bufsz, "vload v%d, r%d, r%d", in->a, in->b, in->c); break;
    case OP_VSTORE: snprintf(buf, bufsz, "vstore v%d, r%d", in->a, in->b); break;
    case OP_VLOADX: snprintf(buf, bufsz, "vloadx v%d, r%d, v%d", in->a, in->b, in->c); break;
    case OP_VSTOREX: snprintf(buf, bufsz, "vstorex v%d, r%d, v%d", in->a, in->b, in->c); break;
    case OP_VADD:   snprintf(buf, bufsz, "vadd v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VSUB:   snprintf(buf, bufsz, "vsub v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VMUL:   snprintf(buf, bufsz, "vmul v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VMACC:  snprintf(buf, bufsz, "vmacc v%d, f%d, v%d", in->a, in->b, in->c); break;
    case OP_VSCALE: snprintf(buf, bufsz, "vscale v%d, v%d, f%d", in->a, in->b, in->c); break;
    case OP_VREDSUM: snprintf(buf, bufsz, "vredsum f%d, v%d", in->a, in->b); break;
    case OP_VREDMAX: snprintf(buf, bufsz, "vredmax f%d, v%d", in->a, in->b); break;
    case OP_VREDMIN: snprintf(buf, bufsz, "vredmin f%d, v%d", in->a, in->b); break;
    case OP_VSPLAT: snprintf(buf, bufsz, "vsplat v%d, f%d", in->a, in->b); break;
    case OP_VADDS:  snprintf(buf, bufsz, "vadds v%d, v%d, f%d", in->a, in->b, in->c); break;
    case OP_VMIN:   snprintf(buf, bufsz, "vmin v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VMAX:   snprintf(buf, bufsz, "vmax v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VMSLT:  snprintf(buf, bufsz, "vmslt v%d, v%d", in->b, in->c); break;
    case OP_VMSGT:  snprintf(buf, bufsz, "vmsgt v%d, v%d", in->b, in->c); break;
    case OP_VMSEQ:  snprintf(buf, bufsz, "vmseq v%d, v%d", in->b, in->c); break;
    case OP_VMERGE: snprintf(buf, bufsz, "vmerge v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VADDM:  snprintf(buf, bufsz, "vaddm v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VSUBM:  snprintf(buf, bufsz, "vsubm v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VMULM:  snprintf(buf, bufsz, "vmulm v%d, v%d, v%d", in->a, in->b, in->c); break;
    case OP_VSTOREM: snprintf(buf, bufsz, "vstorem v%d, r%d", in->a, in->b); break;
    case OP_DUMPS:  snprintf(buf, bufsz, "dumps r%d", in->b); break;
    case OP_DUMPF:  snprintf(buf, bufsz, "dumpf f%d", in->b); break;
    case OP_DUMPV:  snprintf(buf, bufsz, "dumpv v%d", in->a); break;
    case OP_DUMPM:  snprintf(buf, bufsz, "dumpm r%d, %lld", in->b, (long long) in->imm); break;
    case OP_DUMPMASK: snprintf(buf, bufsz, "dumpmask"); break;
    default:        snprintf(buf, bufsz, "???"); break;
  }
  return buf;
}

// ---------------------------------------------------------------------------
//  Interactive debugger
// ---------------------------------------------------------------------------
#define DBG_MAX_BREAKS 64

typedef struct
{
  int stepping;                 // stop before every instruction
  int breaks[DBG_MAX_BREAKS];   // instruction indices to break on
  int nbreaks;
} DbgState;

static int dbg_has_break(const DbgState* d, int idx)
{
  for (int i = 0; i < d->nbreaks; ++i)
    if (d->breaks[i] == idx) return 1;
  return 0;
}

static void dbg_add_break(DbgState* d, int idx)
{
  if (dbg_has_break(d, idx)) return;
  if (d->nbreaks < DBG_MAX_BREAKS) d->breaks[d->nbreaks++] = idx;
}

static void dbg_del_break(DbgState* d, int idx)
{
  for (int i = 0; i < d->nbreaks; ++i)
    if (d->breaks[i] == idx) { d->breaks[i] = d->breaks[--d->nbreaks]; return; }
}

// Resolve a breakpoint/address argument: a code/data label name or a number.
static int dbg_resolve(const char* tok, int* out)
{
  if (isalpha((unsigned char) tok[0]) || tok[0] == '_')
  {
    int64_t v;
    if (vcpu_label_lookup(tok, &v) == 0) { *out = (int) v; return 0; }
    return -1;
  }
  char* end = NULL;
  long v = strtol(tok, &end, 0);
  if (end == tok || *end != '\0') return -1;
  *out = (int) v;
  return 0;
}

static void dbg_print(const VCpu* cpu, const char* what)
{
  if (strcmp(what, "pc") == 0) { printf("pc = %lld\n", (long long) cpu->pc); return; }
  if (strcmp(what, "vl") == 0) { printf("vl = %d\n", cpu->vl); return; }
  if (strcmp(what, "psw") == 0) { printf("psw = 0x%llx (IE=%d)\n", (unsigned long long) cpu->psw, (int) (cpu->psw & PSW_IE)); return; }
  if (strcmp(what, "epc") == 0) { printf("epc = %lld\n", (long long) cpu->epc); return; }
  if (strcmp(what, "vmask") == 0)
  {
    printf("vmask [VL=%d] =", cpu->vl);
    for (int e = 0; e < cpu->vl; ++e) printf(" %d", (int) ((cpu->vmask >> e) & 1));
    printf("\n");
    return;
  }

  int idx = isdigit((unsigned char) what[1]) ? atoi(what + 1) : -1;
  if (what[0] == 'r' && idx >= 0 && idx < NUM_SCALAR)
  {
    printf("r%d = %lld\n", idx, (long long) cpu->r[idx]);
    return;
  }
  if (what[0] == 'f' && idx >= 0 && idx < NUM_FLOAT)
  {
    printf("f%d = %g\n", idx, cpu->f[idx]);
    return;
  }
  if (what[0] == 'v' && idx >= 0 && idx < NUM_VECTOR)
  {
    printf("v%d [VL=%d] =", idx, cpu->vl);
    for (int e = 0; e < cpu->vl; ++e) printf(" %g", cpu->v[idx][e]);
    printf("\n");
    return;
  }
  printf("unknown location '%s'\n", what);
}

static void dbg_list(const Instr* prog, int prog_len, int center)
{
  int lo = center - 2, hi = center + 3;
  if (lo < 0) lo = 0;
  if (hi > prog_len) hi = prog_len;
  for (int i = lo; i < hi; ++i)
  {
    char dis[128];
    const char* lbl = vcpu_label_for_index(i);
    printf("%c %3d  %-24s%s%s\n",
           (i == center) ? '>' : ' ', i,
           vcpu_disasm(&prog[i], dis, sizeof dis),
           lbl ? "  ; " : "", lbl ? lbl : "");
  }
}

static void dbg_help(void)
{
  printf(
      "commands:\n"
      "  s, step            execute one instruction\n"
      "  c, continue        run until next breakpoint or halt\n"
      "  b [target]         set breakpoint (label or index); no arg lists them\n"
      "  d <target>         delete a breakpoint\n"
      "  p <loc>            print r0..r15 / f0..f15 / v0..v7 / pc / vl / vmask / psw / epc\n"
      "  r, regs            print all scalar registers and vl\n"
      "  m <addr> [count]   print 'count' floats from memory (label or number)\n"
      "  l, list            show instructions around pc\n"
      "  h, help            this help\n"
      "  q, quit            stop the simulation\n");
}

// Returns 1 to resume execution (step/continue), 0 to abort the run (quit/EOF).
static int dbg_prompt(VCpu* cpu, const Instr* prog, int prog_len, DbgState* dbg)
{
  char dis[128];
  const char* lbl = vcpu_label_for_index((int) cpu->pc);
  printf("\n-> %3lld  %s%s%s\n", (long long) cpu->pc,
         vcpu_disasm(&prog[cpu->pc], dis, sizeof dis),
         lbl ? "   ; " : "", lbl ? lbl : "");

  char line[256];
  for (;;)
  {
    printf("(vdb) ");
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) { printf("\n"); return 0; }

    char* save = NULL;
    char* cmd = strtok_r(line, " \t\r\n", &save);
    if (!cmd) continue;

    if (!strcmp(cmd, "s") || !strcmp(cmd, "step"))     { dbg->stepping = 1; return 1; }
    if (!strcmp(cmd, "c") || !strcmp(cmd, "cont") || !strcmp(cmd, "continue"))
                             { dbg->stepping = 0; return 1; }
    if (!strcmp(cmd, "q") || !strcmp(cmd, "quit"))     return 0;
    if (!strcmp(cmd, "h") || !strcmp(cmd, "help") || !strcmp(cmd, "?")) { dbg_help(); continue; }
    if (!strcmp(cmd, "l") || !strcmp(cmd, "list"))     { dbg_list(prog, prog_len, (int) cpu->pc); continue; }

    if (!strcmp(cmd, "r") || !strcmp(cmd, "regs"))
    {
      for (int i = 0; i < NUM_SCALAR; ++i)
        printf("r%-2d = %-12lld%s", i, (long long) cpu->r[i], (i % 4 == 3) ? "\n" : " ");
      printf("vl = %d\n", cpu->vl);
      continue;
    }
    if (!strcmp(cmd, "p") || !strcmp(cmd, "print"))
    {
      char* w = strtok_r(NULL, " \t\r\n", &save);
      if (w) dbg_print(cpu, w); else printf("usage: p <r0|f0|v0|pc|vl|vmask|psw|epc>\n");
      continue;
    }
    if (!strcmp(cmd, "m") || !strcmp(cmd, "mem"))
    {
      char* a = strtok_r(NULL, " \t\r\n", &save);
      char* c = strtok_r(NULL, " \t\r\n", &save);
      if (!a) { printf("usage: m <addr> [count]\n"); continue; }
      int addr;
      if (dbg_resolve(a, &addr) != 0) { printf("bad address '%s'\n", a); continue; }
      int cnt = c ? atoi(c) : 8;
      printf("mem[0x%x] =", addr);
      for (int i = 0; i < cnt; ++i) printf(" %g", load_f32(cpu, addr + (int64_t) i * 4));
      printf("\n");
      continue;
    }
    if (!strcmp(cmd, "b") || !strcmp(cmd, "break"))
    {
      char* a = strtok_r(NULL, " \t\r\n", &save);
      if (!a)
      {
        if (dbg->nbreaks == 0) printf("no breakpoints\n");
        for (int i = 0; i < dbg->nbreaks; ++i)
        {
          const char* nm = vcpu_label_for_index(dbg->breaks[i]);
          printf("  #%d at %d%s%s\n", i, dbg->breaks[i], nm ? "  " : "", nm ? nm : "");
        }
        continue;
      }
      int idx;
      if (dbg_resolve(a, &idx) != 0) { printf("bad target '%s'\n", a); continue; }
      dbg_add_break(dbg, idx);
      printf("breakpoint at %d\n", idx);
      continue;
    }
    if (!strcmp(cmd, "d") || !strcmp(cmd, "delete"))
    {
      char* a = strtok_r(NULL, " \t\r\n", &save);
      if (!a) { printf("usage: d <target>\n"); continue; }
      int idx;
      if (dbg_resolve(a, &idx) != 0) { printf("bad target '%s'\n", a); continue; }
      dbg_del_break(dbg, idx);
      continue;
    }
    printf("unknown command '%s' (h for help)\n", cmd);
  }
}

// ---------------------------------------------------------------------------
//  Execution driver
// ---------------------------------------------------------------------------
void vcpu_run_from(VCpu* cpu, const Instr* prog, int prog_len, RunMode mode, int64_t entry)
{
  cpu->pc     = entry;
  cpu->halted = 0;

  DbgState dbg;
  memset(&dbg, 0, sizeof dbg);
  dbg.stepping = (mode == RUN_DEBUG);   // stop on the very first instruction

  if (mode == RUN_DEBUG)
    printf("vcpu debugger: 'h' for help, 's' to step, 'c' to continue.\n");

  while (!cpu->halted && cpu->pc >= 0 && cpu->pc < prog_len)
  {
    // Timer interrupt: delivered at an instruction boundary. Saving the
    // whole status word (epsw) mirrors the exchange-package / mstatus model.
    if ((cpu->psw & PSW_IE) && cpu->timer_period > 0 && cpu->cycles >= cpu->timer_next)
    {
      cpu->epc  = cpu->pc;
      cpu->epsw = cpu->psw;
      cpu->psw &= ~PSW_IE;
      cpu->timer_next = cpu->cycles + (uint64_t) cpu->timer_period;
      if (mode == RUN_TRACE)
        printf("[pc=%3lld cyc=%6llu] -- timer trap -> handler %lld\n",
               (long long) cpu->pc, (unsigned long long) cpu->cycles,
               (long long) cpu->handler);
      cpu->pc = cpu->handler;
      continue;
    }

    const Instr* in = &prog[cpu->pc];

    if (mode == RUN_DEBUG && (dbg.stepping || dbg_has_break(&dbg, (int) cpu->pc)))
    {
      if (!dbg_prompt(cpu, prog, prog_len, &dbg)) return;
    }
    else if (mode == RUN_TRACE)
    {
      char dis[128];
      printf("[pc=%3lld cyc=%6llu] %s\n",
             (long long) cpu->pc, (unsigned long long) cpu->cycles,
             vcpu_disasm(in, dis, sizeof dis));
    }

    cpu->pc += 1;
    cpu->instr_count += 1;
    cpu->cycles += instr_cost(in, cpu->vl);
    execute(cpu, in);
  }
}

void vcpu_run_ex(VCpu* cpu, const Instr* prog, int prog_len, RunMode mode)
{
  vcpu_run_from(cpu, prog, prog_len, mode, 0);
}

void vcpu_run(VCpu* cpu, const Instr* prog, int prog_len)
{
  vcpu_run_ex(cpu, prog, prog_len, RUN_NORMAL);
}
