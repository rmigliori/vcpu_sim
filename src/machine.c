// ---------------------------------------------------------------------------
//  machine.c — la scheda: clock, memoria, bus, ciclo principale, debugger
//  (29/09/2026, la ristrutturazione pura)
//
//  Il ciclo di esecuzione e il debugger vengono da vcpu.c, dove stavano fino
//  al 29/09, e il comportamento e' lo stesso al ciclo: lo prova il confronto
//  delle tracce --trace su tutti i programmi, fatto contro il simulatore di
//  prima. Il perche' della scheda e del clock e' in cima a machine.h.
// ---------------------------------------------------------------------------
#include "machine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void machine_init(VMachine* m)
{
  memset(m, 0, sizeof(*m));
  m->clock.hz         = CPU_HZ;
  m->clock.div_cpu    = 1;
  m->clock.div_periph = 1;
  m->clock.div_ms     = CPU_HZ / 1000ULL;
  for (int i = 0; i < NUM_CPU; ++i)
  {
    vcpu_init(&m->cpu[i]);
    m->cpu[i].m = m;
  }
}

uint64_t machine_now(const VMachine* m, const VCpu* cpu)
{
  return cpu->cycles * m->clock.div_cpu;
}

// ---------------------------------------------------------------------------
//  Il bus. I device stanno SOPRA la RAM (devices.h spiega perche'), e la
//  decodifica e' qui: il device conosce i suoi registri, la scheda sa quale
//  device risponde a quale indirizzo. Il decoder, l'assembler e la ISA non
//  sanno che esistono: e' questo che rende il MMIO piu' economico di
//  un'istruzione nuova.
// ---------------------------------------------------------------------------
static int is_mmio(int64_t addr)
{
  return addr >= MMIO_BASE && addr < MMIO_BASE + MMIO_SIZE;
}

// L'OROLOGIO DI SISTEMA (15/09/2026). La sorgente e' il clock master, il
// prescaler e' il divisore dei millisecondi: un posto solo dichiara la
// frequenza e l'orologio la segue.
//
// I 32 bit bassi, come qualunque altra lettura: `lw` estende il segno, quindi
// dopo 2^31 ms il valore letto diventa negativo e dopo 2^32 il contatore gira.
// E' il comportamento del ferro e non un limite del simulatore -- chi
// confronta due letture deve sottrarle, mai ordinarle.
static int32_t clock_ms(const VMachine* m, uint64_t t)
{
  return (int32_t) (uint32_t) (t / m->clock.div_ms);
}

static int32_t mmio_load(VMachine* m, VCpu* cpu, int64_t addr)
{
  uint64_t t = machine_now(m, cpu);
  int32_t v;
  if (kbd_load(&m->kbd, addr, &v)) return v;
  if (addr == CLOCK_MS)            return clock_ms(m, t);
  if (cmp_load(&m->cmp, addr, &v)) return v;
  if (adc_load(&m->adc, addr, &v)) return v;

  fprintf(stderr, "runtime error: MMIO load from unmapped register 0x%llx\n",
          (unsigned long long) addr);
  return 0;
}

static void mmio_store(VMachine* m, VCpu* cpu, int64_t addr, int32_t value)
{
  uint64_t t = machine_now(m, cpu);
  if (kbd_store(&m->kbd, addr, value))                    return;
  if (cmp_store(&m->cmp, addr, value))                    return;
  if (adc_store(&m->adc, addr, value, t, &m->clock))      return;
  if (marker_store(&m->marker, cpu, t, addr, value))      return;

  fprintf(stderr, "runtime error: MMIO store to read-only register 0x%llx\n",
          (unsigned long long) addr);
}

float machine_loadf(const VMachine* m, int64_t addr)
{
  float value = 0.0f;
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: float load out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return 0.0f;
  }
  memcpy(&value, &m->mem[addr], sizeof(value));
  return value;
}

void machine_storef(VMachine* m, int64_t addr, float value)
{
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: float store out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return;
  }
  memcpy(&m->mem[addr], &value, sizeof(value));
}

int32_t machine_load32(VMachine* m, VCpu* cpu, int64_t addr)
{
  int32_t value = 0;
  if (is_mmio(addr)) return mmio_load(m, cpu, addr);
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: word load out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return 0;
  }
  memcpy(&value, &m->mem[addr], sizeof(value));
  return value;
}

void machine_store32(VMachine* m, VCpu* cpu, int64_t addr, int32_t value)
{
  if (is_mmio(addr)) { mmio_store(m, cpu, addr, value); return; }
  if (addr < 0 || (uint64_t) addr + sizeof(value) > MEM_SIZE)
  {
    fprintf(stderr, "runtime error: word store out of bounds at 0x%llx\n",
            (unsigned long long) addr);
    return;
  }
  memcpy(&m->mem[addr], &value, sizeof(value));

  // Il canale MARK_EXEC: il possesso della CPU letto invece che dedotto.
  // Si annota il CAMBIO e non la scrittura, perche' il dispatcher riscrive
  // `current` a ogni uscita da ISR anche senza commutare -- la scrittura e'
  // dichiaratamente idempotente (scheduler.vasm), e annotarla darebbe una
  // marca per tick che non significa niente. Costa un confronto per store, e
  // zero istruzioni nel kernel. `current` e' quello del programma di QUESTA
  // CPU: il guardiano e' per CPU.
  if (m->marker.on && addr == cpu->mark_current_addr && value != cpu->mark_current)
  {
    cpu->mark_current = value;
    marker_mark(&m->marker, cpu, machine_now(m, cpu), MARK_EXEC, value);
  }
}

// LA SONDA (`mark`). Il porto e' un indirizzo, lo stesso che la sw usava.
// Fuori dai porti del marcatore e' un errore di costruzione del programma e va
// detto, non ignorato: una marca persa in silenzio e' una misura che manca
// senza che nessuno lo sappia.
int machine_mark(VMachine* m, VCpu* cpu, int64_t port, int32_t value)
{
  if (!marker_is_channel(port))
  {
    fprintf(stderr, "mark: il porto %lld non e' un canale del marcatore\n",
            (long long) port);
    return -1;
  }
  int canale = (int) ((port - MARK_BASE) / 4);
  if (canale < MARK_RISERVATI)
  {
    fprintf(stderr, "mark: il canale %d e' RISERVATO alla macchina\n", canale);
    return -1;
  }
  marker_mark(&m->marker, cpu, machine_now(m, cpu), canale, value);
  return 0;
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
      for (int i = 0; i < cnt; ++i) printf(" %g", machine_loadf(cpu->m, addr + (int64_t) i * 4));
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
//  Il ciclo principale
//
//  A ogni confine d'istruzione: i device avanzano fino all'istante della CPU,
//  poi l'arbitraggio delle interruzioni, poi UNA istruzione. L'istruzione e'
//  indivisibile, ed e' tutto cio' che serve perche' un evento che matura
//  DENTRO un'istruzione si veda al confine dopo.
//
//  Una CPU, per ora. Con piu' CPU si fa avanzare sempre quella piu' indietro
//  nel tempo, e i device si portano al suo istante: e' la tappa 2.
// ---------------------------------------------------------------------------
void machine_run(VMachine* m, const Instr* prog, int prog_len, RunMode mode, int64_t entry)
{
  VCpu* cpu = &m->cpu[0];
  cpu->pc     = entry;
  cpu->halted = 0;

  DbgState dbg;
  memset(&dbg, 0, sizeof dbg);
  dbg.stepping = (mode == RUN_DEBUG);   // stop on the very first instruction

  if (mode == RUN_DEBUG)
    printf("vcpu debugger: 'h' for help, 's' to step, 'c' to continue.\n");

  while (!cpu->halted && cpu->pc >= 0 && cpu->pc < prog_len)
  {
    uint64_t t = machine_now(m, cpu);

    // I device avanzano al confine d'istruzione come tutto il resto. Questo
    // NON e' una trap: aggiorna solo lo stato visibile in MMIO, quindi non
    // guarda PSW_IE e vale anche per un programma che gli interrupt non li
    // abilita mai -- che e' precisamente il caso del polling.
    kbd_advance(&m->kbd, t, &m->marker, cpu);
    adc_advance(&m->adc, t, &m->clock, m->mem);

    // LA RICHIESTA, PRIMA DELLA CONSEGNA (15/09/2026). Il timer matura quando
    // il conto dei cicli raggiunge la scadenza, e questo non ha niente a che
    // vedere con PSW_IE: a interrupt chiusi la richiesta resta alzata e la trap
    // arriva quando puo'. Finche' la condizione di consegna qui sotto era una
    // sola -- con PSW_IE per primo -- quell'istante non veniva valutato
    // affatto, e la latenza di consegna non era una grandezza osservabile.
    //
    // Si marca UNA VOLTA per richiesta (timer_pending), o si rimarcherebbe a
    // ogni istruzione per tutto il tempo che la trap aspetta. Stesso modello di
    // kbd_advance, che marca una volta per carattere avanzando nella traccia.
    //
    // L'istante osservabile e' il confine d'istruzione, non il ciclo esatto
    // della scadenza: una richiesta che matura DENTRO un'istruzione lunga si
    // vede quando quella finisce. Vale per la tastiera allo stesso modo (un
    // carattere dato per 24000 si marca a 24001), ed e' una proprieta' della
    // macchina, non dello strumento: qui non esiste un osservatore piu' fine
    // del confine d'istruzione.
    //
    // Il timer e' PRIVATO della CPU (settimer), e conta i suoi cicli.
    if (cpu->timer_period > 0 && cpu->cycles >= cpu->timer_next && !cpu->timer_pending)
    {
      cpu->timer_pending = 1;
      cpu->timer_seq += 1;
      // IL VALORE E' IL NUMERO DELLA RICHIESTA, non l'istante: l'istante e' gia'
      // la marca. Serve a dire se una richiesta e' andata PERSA -- due marche
      // consecutive che saltano un numero -- che con la sola sequenza di
      // istanti si potrebbe solo sospettare guardando le distanze.
      marker_mark(&m->marker, cpu, t, MARK_TIMER, (int32_t) cpu->timer_seq);
    }

    // I COMPARATORI, PRIMI FRA LE SORGENTI (15/09/2026, §3.72). Il canale 0 e'
    // il posto del battito del foreground, e un battito non deve derivare: e' lo
    // stesso argomento con cui il timer batte la tastiera.
    //
    // Nessun bit di pending da azzerare qui: la condizione resta vera finche'
    // l'ISR non riprograma la scadenza o non disarma il canale. E' il protocollo
    // della tastiera -- l'azione utile chiude l'evento, non la trap.
    if (cpu->psw & PSW_IE)
    {
      int scattato = cmp_pending(&m->cmp, clock_ms(m, t));
      if (scattato >= 0)
      {
        if (mode == RUN_TRACE)
          printf("[pc=%3lld cyc=%6llu] -- cmp%d trap -> handler %lld\n",
                 (long long) cpu->pc, (unsigned long long) cpu->cycles,
                 scattato, (long long) cpu->handler);
        vcpu_trap(cpu, CAUSE_CMP + scattato);
        continue;
      }
    }

    // Timer interrupt: delivered at an instruction boundary.
    if ((cpu->psw & PSW_IE) && cpu->timer_period > 0 && cpu->cycles >= cpu->timer_next)
    {
      cpu->timer_pending = 0;
      cpu->timer_next = cpu->cycles + (uint64_t) cpu->timer_period;
      if (mode == RUN_TRACE)
        printf("[pc=%3lld cyc=%6llu] -- timer trap -> handler %lld\n",
               (long long) cpu->pc, (unsigned long long) cpu->cycles,
               (long long) cpu->handler);
      vcpu_trap(cpu, CAUSE_TIMER);
      continue;
    }

    // LA TASTIERA, seconda sorgente (14/09/2026, §3.67). DOPO il timer e non
    // prima: se sono pronte insieme vince il battito dello scheduler, che non
    // deve derivare -- un carattere aspetta un tick senza che se ne accorga
    // nessuno, un tick perso si vede su ogni scadenza.
    //
    // A LIVELLO (kbd_irq): un'ISR che torna SENZA aver letto KBD_DATA ritrova
    // la trap subito, perche' il flag lo abbassa la lettura, non la trap. E' il
    // protocollo del device, non un caso limite.
    if ((cpu->psw & PSW_IE) && kbd_irq(&m->kbd))
    {
      if (mode == RUN_TRACE)
        printf("[pc=%3lld cyc=%6llu] -- kbd trap -> handler %lld\n",
               (long long) cpu->pc, (unsigned long long) cpu->cycles,
               (long long) cpu->handler);
      vcpu_trap(cpu, CAUSE_KBD);
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

    vcpu_step(cpu, in);
  }
}
