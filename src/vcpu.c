#include "machine.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vcpu_init(VCpu* cpu)
{
  memset(cpu, 0, sizeof(*cpu));
  cpu->tag = "";
  cpu->vl = 0;
  cpu->r[0] = 0;  // hardwired zero
}

// ---------------------------------------------------------------------------
//  Memory helpers (element size is 4 bytes / one float)
// ---------------------------------------------------------------------------
// Dal 29/09/2026 la memoria e i device sono della SCHEDA, e ogni accesso passa
// dal bus (machine.c): i controlli sui limiti, la decodifica MMIO e il
// guardiano di `current` per il marcatore stanno la'. Qui restano i nomi con
// cui l'esecuzione li chiama.
static float load_f32(const VCpu* cpu, int64_t addr)
{
  return machine_loadf(cpu->m, cpu, addr);
}

static void store_f32(VCpu* cpu, int64_t addr, float value)
{
  machine_storef(cpu->m, cpu, addr, value);
}

// Lo shift aritmetico a destra, scritto come divisione col FLOOR: e' cio' che il
// ferro fa replicando il bit di segno, e non dipende da come il compilatore
// implementa `>>` su un negativo.
static int64_t floor_shift(int64_t v, int n)
{
  int64_t d = (int64_t) 1 << n;
  int64_t q = v / d;
  if (v % d != 0 && v < 0) q -= 1;   // verso meno infinito
  return q;
}

// NON prende un const VCpu*, e la ragione e' concettuale prima che tecnica:
// leggere KBD_DATA consuma il carattere.
static int32_t load_i32(VCpu* cpu, int64_t addr)
{
  return machine_load32(cpu->m, cpu, addr);
}

static void store_i32(VCpu* cpu, int64_t addr, int32_t value)
{
  machine_store32(cpu->m, cpu, addr, value);
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
//  LA PSW SCRITTA PER INTERO (mtpsw, reti): se cambia PSW_BANK, cambiano i
//  registri. Lo scambio e' fisico -- il banco attivo sta sempre in r[] e f[] --
//  cosi' il resto dell'interprete, il debugger e le dumps non sanno che i banchi
//  esistono. Quali registri, e perche' quelli, in vcpu.h.
// ---------------------------------------------------------------------------
static void scambia_banco(VCpu* cpu)
{
  for (int i = 2; i < NUM_SCALAR; ++i)
  {
    if (i == 14) continue;          // lo stack e' condiviso
    int64_t t = cpu->r[i]; cpu->r[i] = cpu->alt_r[i]; cpu->alt_r[i] = t;
  }
  for (int i = 0; i < NUM_FLOAT; ++i)
  {
    float t = cpu->f[i]; cpu->f[i] = cpu->alt_f[i]; cpu->alt_f[i] = t;
  }
}

static void scrivi_psw(VCpu* cpu, uint64_t v)
{
  if ((cpu->psw ^ v) & PSW_BANK) scambia_banco(cpu);
  cpu->psw = v;
}

// ---------------------------------------------------------------------------
//  Quale stato dell'ESTENSIONE scrive? -> il bit sporco da alzare, o 0.
//    PSW_VDIRTY   v0..v7, vl, vmask
//    PSW_FDIRTY   f0..f15
//
//  Sta in un posto solo e non sparso nei case di execute(), ed e' una scelta di
//  manutenzione: la regola e' "quali istruzioni sporcano quale banco", cioe'
//  UNA cosa, e un giorno che se ne aggiunga una il compilatore non aiuta
//  comunque -- ma almeno l'elenco da rileggere e' questo e non l'interprete
//  intero.
//
//  Fino all'08/10/2026 il bit era uno solo, e un task che toccava solo i float
//  si portava dietro anche i 2 KB dei vettori a ogni commutazione (§3.86).
//
//  Chi NON c'e' e' altrettanto significativo: le store (vstore, vstorex,
//  vstorem, fsw) leggono e basta; mfvl e mfvmask sono letture, ed e' quello che
//  permette a ctx_save di guardare i bit senza falsarli. Le riduzioni
//  (vredsum/vredmax/vredmin) LEGGONO i vettori e scrivono un FLOAT: sporcano
//  il banco float e non quello vettoriale.
// ---------------------------------------------------------------------------
static uint64_t sporca_estensione(int op)
{
  switch (op)
  {
    // --- stato vettoriale: v0..v7, vmask, vl ---
    case OP_VLOAD: case OP_VLOADS: case OP_VLOADX:
    case OP_VADD:  case OP_VSUB:   case OP_VMUL:   case OP_VMACC:
    case OP_VSCALE: case OP_VSPLAT: case OP_VADDS:
    case OP_VMIN:  case OP_VMAX:   case OP_VMERGE:
    case OP_VADDM: case OP_VSUBM:  case OP_VMULM:
    case OP_VCVT:
    case OP_VMSLT: case OP_VMSGT:  case OP_VMSEQ:   // scrivono vmask
    case OP_SETVL:                                  // scrive vl
      return PSW_VDIRTY;

    // --- stato FLOAT: f0..f15 ---
    case OP_FLI:  case OP_FLW:   case OP_FADD: case OP_FMUL:
    case OP_FMACC: case OP_FMOV: case OP_FMIN: case OP_FMAX:
    case OP_FSUB: case OP_FDIV:  case OP_FNEG: case OP_FSQRT:
    case OP_VREDSUM: case OP_VREDMAX: case OP_VREDMIN:  // riducono IN un float
      return PSW_FDIRTY;

    default:
      return 0;                    // mtvl e mtvmask alzano VDIRTY da soli
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
    case OP_ADDI: case OP_SLLI: case OP_SRLI: case OP_SRAI:
    case OP_AND: case OP_OR: case OP_XOR:
    case OP_SETVL: case OP_FLI:
    case OP_STI: case OP_CLI: case OP_SETHANDLER: case OP_SETTIMER: case OP_MARK:
    case OP_MFCAUSE:
    case OP_MFPSW: case OP_MTPSW: case OP_MFEPC: case OP_MTEPC:
    case OP_MFEPSW: case OP_MTEPSW:
    case OP_MFVL: case OP_MTVL: case OP_MFVMASK: case OP_MTVMASK:
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
    case OP_VCVT:
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
      // Lo shift aritmetico: il segno si replica. In C lo shift a destra di un
      // negativo e' implementation-defined, quindi qui e' scritto come la
      // divisione col floor che il ferro realizza -- il compilatore la riconosce
      // e ne fa uno shift, e il comportamento non dipende da lui.
      case OP_SRAI: set_scalar(cpu, in->a, floor_shift(cpu->r[in->b], (int) in->imm)); break;
      case OP_AND:  set_scalar(cpu, in->a, cpu->r[in->b] & cpu->r[in->c]); break;
      case OP_OR:   set_scalar(cpu, in->a, cpu->r[in->b] | cpu->r[in->c]); break;
      case OP_XOR:  set_scalar(cpu, in->a, cpu->r[in->b] ^ cpu->r[in->c]); break;
      case OP_DIV:  set_scalar(cpu, in->a, cpu->r[in->c] ? cpu->r[in->b] / cpu->r[in->c] : 0); break;
      case OP_REM:  set_scalar(cpu, in->a, cpu->r[in->c] ? cpu->r[in->b] % cpu->r[in->c] : 0); break;

      // f0..f15 sono float a 32 bit (vcpu.h): ogni operazione arrotonda al suo
      // risultato, e fsw/flw non perdono niente -- e' cio' che rende esatto il
      // salvataggio del contesto.
      case OP_FLI:  cpu->f[in->a] = (float) in->fimm;                      break;
      case OP_FLW:  cpu->f[in->a] = load_f32(cpu, cpu->r[in->b] + in->imm);          break;
      case OP_FSW:  store_f32(cpu, cpu->r[in->b] + in->imm, cpu->f[in->a]);           break;
      case OP_FADD: cpu->f[in->a] = cpu->f[in->b] + cpu->f[in->c];         break;
      case OP_FMUL: cpu->f[in->a] = cpu->f[in->b] * cpu->f[in->c];         break;
      case OP_FMACC:
      {
        // DUE arrotondamenti, non una FMA fusa: e' la semantica di vmacc, e
        // scalare e vettoriale devono dare lo stesso numero.
        float prod = cpu->f[in->b] * cpu->f[in->c];
        cpu->f[in->a] = cpu->f[in->a] + prod;
        break;
      }
      case OP_FMOV: cpu->f[in->a] = cpu->f[in->b];                         break;
      case OP_FMIN: cpu->f[in->a] = (cpu->f[in->b] < cpu->f[in->c]) ? cpu->f[in->b] : cpu->f[in->c]; break;
      case OP_FMAX: cpu->f[in->a] = (cpu->f[in->b] > cpu->f[in->c]) ? cpu->f[in->b] : cpu->f[in->c]; break;
      case OP_FSUB: cpu->f[in->a] = cpu->f[in->b] - cpu->f[in->c];         break;
      case OP_FDIV: cpu->f[in->a] = (cpu->f[in->c] != 0.0f) ? cpu->f[in->b] / cpu->f[in->c] : 0.0f; break;
      case OP_FNEG: cpu->f[in->a] = -cpu->f[in->b];                        break;
      case OP_FSQRT: cpu->f[in->a] = sqrtf(cpu->f[in->b]);                 break;

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
      case OP_RETI:
        cpu->pc = cpu->epc; scrivi_psw(cpu, cpu->epsw);
        if (cpu->trap_depth > 0) cpu->trap_depth -= 1;
        break;
      case OP_MARK:
      {
        // LA SONDA. Niente registri: e' tutto il punto -- si puo' mettere dove
        // non ce n'e' uno libero, cioe' subito prima della `reti`, che con la
        // forma li+li+sw era impossibile.
        //
        // Il porto e' un indirizzo, lo stesso che la sw usava. Fuori dai porti
        // del marcatore e' un errore di costruzione del programma e va detto,
        // non ignorato: una marca persa in silenzio e' una misura che manca
        // senza che nessuno lo sappia.
        // Il porto lo controlla la scheda, che conosce il marcatore: fuori dai
        // porti, o su un canale riservato, e' un errore di costruzione del
        // programma e la CPU si ferma.
        if (machine_mark(cpu->m, cpu, in->a, (int32_t) in->imm) != 0)
          cpu->halted = 1;
        break;
      }
      case OP_SETHANDLER: cpu->handler = in->target; break;
      case OP_SETTIMER:
      {
        int64_t p = cpu->r[in->b];
        cpu->timer_period = p;
        cpu->timer_next   = cpu->cycles + (uint64_t) (p > 0 ? p : 0);
        break;
      }
      case OP_MFCAUSE: set_scalar(cpu, in->a, (int64_t) cpu->cause); break;
      case OP_MFPSW: set_scalar(cpu, in->a, (int64_t) cpu->psw); break;
      case OP_MTPSW: scrivi_psw(cpu, (uint64_t) cpu->r[in->b]);  break;
      case OP_MFEPC: set_scalar(cpu, in->a, cpu->epc);           break;
      case OP_MFEPSW: set_scalar(cpu, in->a, (int64_t) cpu->epsw); break;
      case OP_MTEPSW: cpu->epsw = (uint64_t) cpu->r[in->b];        break;

      // Lo stato vettoriale che non sta in v0..v7. mfvl e mfvmask sono LETTURE
      // pure: non alzano VDIRTY, perche' leggere non sporca -- ed e' cio' che
      // permette a ctx_save di guardare senza falsare la propria decisione.
      case OP_MFVL:    set_scalar(cpu, in->a, cpu->vl);              break;
      case OP_MFVMASK: set_scalar(cpu, in->a, (int64_t) cpu->vmask); break;
      case OP_MTVL:
      {
        int64_t req = cpu->r[in->b];
        cpu->vl = (req < 0) ? 0 : (req > VLMAX ? VLMAX : (int) req);
        cpu->psw |= PSW_VDIRTY;
        break;
      }
      case OP_MTVMASK:
        cpu->vmask = (uint64_t) cpu->r[in->b];
        cpu->psw |= PSW_VDIRTY;
        break;
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
        // L'accumulatore resta DOUBLE e si arrotonda UNA volta, alla fine: cosi'
        // la somma non dipende dall'ordine degli elementi, e un test che la
        // legge ha un numero derivabile. Il registro di destinazione e' float.
        double acc = 0.0;
        for (int i = 0; i < cpu->vl; ++i) acc += cpu->v[in->b][i];
        cpu->f[in->a] = (float) acc;
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

      // I bit della corsia letti come int32, e convertiti. Il memcpy e' la
      // reinterpretazione senza aliasing; che i bit arrivino intatti fin qui
      // (vload -> registro -> vcvt) lo garantisce il fatto che nel mezzo ci
      // sono solo COPIE di float, mai un'operazione aritmetica. Un intero
      // negativo, letto come float, e' un NaN: su x86-64 e ARM64 una copia lo
      // lascia com'e'. Il valore e' esatto: un int32 fino a 2^24 sta in un
      // float, e i campioni a 12 bit ci stanno con largo margine.
      case OP_VCVT:
        for (int i = 0; i < cpu->vl; ++i)
        {
          int32_t bits;
          memcpy(&bits, &cpu->v[in->b][i], sizeof bits);
          cpu->v[in->a][i] = (float) bits;
        }
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

      // Il prefisso della CPU (vcpu.h, `tag`): vuoto con una CPU sola.
      case OP_DUMPS:
        printf("%sr%d = %lld\n", cpu->tag, in->b, (long long) cpu->r[in->b]);
        break;
      case OP_DUMPF:
        printf("%sf%d = %g\n", cpu->tag, in->b, cpu->f[in->b]);
        break;
      case OP_DUMPV:
        printf("%sv%d [VL=%d] =", cpu->tag, in->a, cpu->vl);
        for (int i = 0; i < cpu->vl; ++i) printf(" %g", cpu->v[in->a][i]);
        printf("\n");
        break;
      case OP_DUMPM:
      {
        int64_t base = cpu->r[in->b];
        printf("%smem[r%d=0x%llx] =", cpu->tag, in->b, (unsigned long long) base);
        for (int i = 0; i < in->imm; ++i)
          printf(" %g", load_f32(cpu, base + (int64_t) i * 4));
        printf("\n");
        break;
      }
      case OP_DUMPMASK:
        printf("%svmask [VL=%d] =", cpu->tag, cpu->vl);
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
    case OP_SRAI:   snprintf(buf, bufsz, "srai r%d, r%d, %lld", in->a, in->b, (long long) in->imm); break;
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
    case OP_MFCAUSE:    snprintf(buf, bufsz, "mfcause r%d", in->a); break;
    case OP_MARK:       snprintf(buf, bufsz, "mark %d, %lld", in->a,
                                  (long long) in->imm); break;
    case OP_SETHANDLER: snprintf(buf, bufsz, "sethandler %d", in->target); break;
    case OP_SETTIMER:   snprintf(buf, bufsz, "settimer r%d", in->b); break;
    case OP_MFPSW:      snprintf(buf, bufsz, "mfpsw r%d", in->a); break;
    case OP_MTPSW:      snprintf(buf, bufsz, "mtpsw r%d", in->b); break;
    case OP_MFEPC:      snprintf(buf, bufsz, "mfepc r%d", in->a); break;
    case OP_MFEPSW:     snprintf(buf, bufsz, "mfepsw r%d", in->a); break;
    case OP_MTEPSW:     snprintf(buf, bufsz, "mtepsw r%d", in->b); break;
    case OP_MFVL:       snprintf(buf, bufsz, "mfvl r%d", in->a); break;
    case OP_MTVL:       snprintf(buf, bufsz, "mtvl r%d", in->b); break;
    case OP_MFVMASK:    snprintf(buf, bufsz, "mfvmask r%d", in->a); break;
    case OP_MTVMASK:    snprintf(buf, bufsz, "mtvmask r%d", in->b); break;
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
    case OP_VCVT:   snprintf(buf, bufsz, "vcvt v%d, v%d", in->a, in->b); break;
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
//  Il passo e la trap -- il resto del ciclo e' della scheda (machine.c)
//
//  Fino al 29/09/2026 qui c'era il ciclo di esecuzione intero, col debugger,
//  i device e l'arbitraggio delle interruzioni. Sono della SCHEDA, e stanno in
//  machine.c: il core esegue un'istruzione quando gli viene detto, e prende la
//  trap che gli viene data.
// ---------------------------------------------------------------------------
void vcpu_step(VCpu* cpu, const Instr* in)
{
  cpu->pc += 1;
  cpu->instr_count += 1;
  cpu->cycles += instr_cost(in, cpu->vl);
  cpu->psw |= sporca_estensione(in->op);
  execute(cpu, in);
}

// Saving the whole status word (epsw) mirrors the exchange-package / mstatus
// model. La trap non costa cicli: il costo della consegna e' tutto nelle
// istruzioni dell'ISR.
void vcpu_trap(VCpu* cpu, int32_t cause)
{
  cpu->epc  = cpu->pc;
  cpu->epsw = cpu->psw;
  cpu->psw &= ~PSW_IE;
  cpu->trap_depth += 1;
  cpu->cause = cause;
  cpu->pc = cpu->handler;
}
