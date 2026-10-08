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

// Guard word at the bottom of the data segment: no object is ever placed at
// address 0, so 0 is a NULL pointer that cannot collide with a real datum.
// The codebase already assumed this in several places before it was enforced --
// dequeue_testa returns 0 for "empty queue", `current == 0` means "no running
// task", buf_alloc returns 0 for "no block" -- and the queue link invariant
// ("a node in no list has fwd == bwd == 0") makes the assumption load-bearing.
#define NULL_GUARD 4

#define MAX_INSTR   4096
#define MAX_SYMBOLS 512

// Program status word bits (scalar control layer).
#define PSW_IE 0x1ULL   // interrupt enable

// ---------------------------------------------------------------------------
//  PSW_VDIRTY — "i registri vettoriali contengono qualcosa che a qualcuno serve"
//
//  Lo alza la MACCHINA a ogni scrittura di stato architetturale vettoriale
//  (v0..v7, vl, vmask), e serve a una cosa sola: permettere a ctx_save di NON
//  salvare duemila byte per i task che i vettori non li toccano. I float hanno
//  un bit loro, PSW_FDIRTY, qui sotto.
//
//  Salvare sempre costerebbe ~1260 cicli per commutazione contro i ~120 dello
//  scalare -- un fattore dieci, pagato anche da chi non usa l'unita' vettoriale.
//
//  --- PERCHE' STA NELLA PSW E NON IN UN REGISTRO SUO ---
//  Perche' cosi' viaggia con il contesto senza una riga in piu': la trap copia
//  la psw in epsw, ctx_save la mette nel frame, reti la rimette. Il task che
//  riprende ritrova il proprio VDIRTY, e ctx_save non ha bisogno di azzerare
//  niente -- fra un salvataggio e il ripristino successivo nessuno legge la psw
//  viva, che reti sovrascrive comunque.
//
//  Una conseguenza da sapere: RIPRISTINARE uno stato vettoriale rialza VDIRTY
//  (lo fanno mtvl/mtvmask/vload, che sono scritture), ed e' necessario -- un
//  task ripreso e poi preemptato prima di toccare i vettori DEVE essere salvato
//  lo stesso, altrimenti li ritroverebbe sporcati da un altro.
//
//  Quello che questo schema NON risparmia e' il caso del task vettoriale che
//  esce e rientra senza che nessun altro usi i vettori nel frattempo: paga
//  salvataggio e ripristino per niente. Evitarlo e' il salvataggio PIGRO vero
//  (unita' disabilitata alla commutazione, trap alla prima istruzione
//  vettoriale) e vuole una seconda sorgente di trap con una causa leggibile --
//  la stessa decisione che §3.37 ha parcheggiato per l'interrupt della tastiera.
//
//  Nota sul percorso ISR: un'ISR che usasse i vettori alzerebbe VDIRTY nella psw
//  VIVA, che reti sovrascrive. Un'ISR vettoriale e' un problema suo, e oggi non
//  esiste.
//
//  --- I FLOAT HANNO UN BIT LORO (08/10/2026, §3.86) ---
//  Fino a quel giorno VDIRTY lo alzavano anche le istruzioni float, e i float
//  si salvavano solo dentro il blocco vettoriale: un task che faceva un conto
//  in float, senza un'istruzione vettoriale, pagava ~1600 cicli di contesto a
//  commutazione per 64 byte di registri. Adesso VDIRTY dice solo v0..v7, vl e
//  vmask, e PSW_FDIRTY dice f0..f15: ctx_save salva cio' che il task ha
//  toccato, e niente di piu'. Lo stesso schema, la stessa semantica -- lo alza
//  la macchina, viaggia nella psw, non lo azzera nessuno.
// ---------------------------------------------------------------------------
#define PSW_VDIRTY 0x2ULL   // bit 1: stato vettoriale da salvare
#define PSW_FDIRTY 0x4ULL   // bit 2: stato float da salvare

// ---------------------------------------------------------------------------
//  PSW_BANK — IL BANCO ALTERNATIVO DEI REGISTRI (08/10/2026, §3.86)
//
//  Il bit E' il banco, come in MODE1 dello SHARC: scriverlo cambia i registri.
//  Lo scrivono solo mtpsw e reti -- le due istruzioni che scrivono la psw per
//  intero -- e la trap NON lo tocca: copia la psw in epsw e abbassa IE, quindi
//  il vettore gira nel banco di chi ha interrotto, e da epsw sa quale fosse.
//  E' la cosa che lo Z80 non aveva bisogno di sapere: li' EXX si usava a
//  interrupt chiusi, qui il super task gira nel banco alternativo a interrupt
//  aperti, e una trap che lo interrompe (lo sforamento) non deve scambiare.
//
//  NIENTE OPCODE: con il banco nella psw, `mtpsw` fa cio' che faceva EXX, e
//  `reti` rimette da sola il banco di chi riprende. Deciso dall'utente.
//
//  COSA SI SCAMBIA: r2..r13, r15 e f0..f15. Restano CONDIVISI
//    r0    cablato a zero
//    r1    il registro che passa da un banco all'altro, come AF nello Z80 con
//          EXX: e' il contesto opaco che l'HAL consegna all'ISR
//    r14   lo stack: nell'HAL e' il puntatore al contesto, e ctx_restore lo
//          reimposta a ogni ripresa; bancato, il banco alternativo ne terrebbe
//          uno vecchio. Lo Z80 non scambiava SP
//    v0..v7, vl, vmask   2 KB di ferro: chi interrompe coi vettori vivi passa
//          dal salvataggio canonico
//    psw, epc, epsw      di controllo, non di dato
//
//  Il costo nel modello e' quello di mtpsw e di reti: nessun ciclo in piu'.
// ---------------------------------------------------------------------------
#define PSW_BANK   0x8ULL   // bit 3: il banco alternativo e' quello attivo

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
//  LA FREQUENZA: la meta' mancante del modello, ed e' quella che trasforma un
//  ciclo in un TEMPO. Sta qui e non nel catalogo delle marche perche' col
//  marcatore non c'entra -- e' una proprieta' della macchina, come VEC_LANES --
//  e perche' chi mostra dei cicli e' molto piu' di chi legge marks.conf.
//  Da qui VIAGGIA NELLA REGISTRAZIONE ("# frequenza") e nelle statistiche di
//  fine corsa, come gia' fanno i canali riservati: chi produce dichiara, chi
//  legge legge, e nessuno tiene una seconda copia.
//
//  100 MHz e' il GR712RC, il LEON3-FT doppio che l'ESA ha volato di piu': il
//  numero piu' rappresentativo per una macchina come questa -- VEC_LANES 1 con
//  startup 6, cioe' una pipeline vettoriale alla Cray e non una SIMD larga,
//  1 MiB on-chip e nessuna cache. E un ciclo e' 10 ns tondi, quindi cicli -> us
//  e' una divisione per cento.
//
//  ATTENZIONE, E CONTA PIU' DEL NUMERO: i cicli di questo progetto non sono una
//  misura, sono l'USCITA DI UN MODELLO -- li decide vcpu.c, e non c'e' un
//  sistema di memoria (niente cache miss, niente contesa DMA, niente conflitti
//  di banco). Moltiplicarli per una frequenza da' un tempo che SEMBRA piu' reale
//  dei cicli da cui viene: "118 cicli" si legge come un numero di modello,
//  "1,18 us" si legge come una misura. Da cui la regola in tutti gli strumenti:
//  mai il tempo da solo -- sempre accanto ai cicli, che restano la cosa
//  misurata -- e la frequenza sempre visibile accanto alla conversione.
//
//  Dal 29/09/2026 e' la frequenza del CLOCK MASTER (machine.h), da cui ogni
//  dominio -- CPU, periferiche, millisecondi -- prende la sua con un divisore
//  intero. Con il divisore della CPU a 1, un ciclo di CPU e' ancora 10 ns.
// ---------------------------------------------------------------------------
#define CPU_HZ    100000000ULL   // 100 MHz: un tick master = 10 ns

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
  OP_DUMPMASK, // (no operands)                -> print first VL bits of vmask

  // --- Aggiunte in coda, non accanto ai loro fratelli, e non è una svista ---
  // Il formato .vo serializza l'opcode come NUMERO (toolchain.c): inserire un
  // opcode in mezzo all'enum rinumera tutti quelli dopo, e ogni .vo del progetto
  // cambierebbe contenuto pur restando equivalente. In coda, invece, nessuna
  // codifica esistente si muove. Il posto logico di queste due sarebbe accanto a
  // OP_MFEPC/OP_MTEPC, e il manuale le documenta lì.
  //
  // Completano l'accesso ai CSR di trap: epc era leggibile e scrivibile, epsw
  // non era né l'uno né l'altro, quindi ogni `reti` riportava per forza il
  // regime di interruzione del task interrotto. Servono per far ritornare una
  // ISR verso il kernel a interrupt DISABILITATI — §12.5 della proposta.
  OP_MFEPSW,  // a=rd                          -> r[rd] = epsw
  OP_MTEPSW,  // b=rs1                         -> epsw = r[rs1]

  // Accesso allo stato architetturale vettoriale che NON e' in v0..v7.
  //
  // Servono a una cosa sola: rendere salvabile il contesto. Senza, il context
  // switch di un task vettoriale non e' scrivibile — vmask era leggibile solo
  // da vmerge e dalle operazioni mascherate, e vl solo IMPOSTABILE (setvl
  // scrive e restituisce il valore nuovo, quindi leggerlo lo distrugge).
  //
  // mtvl esiste accanto a setvl e non al suo posto: setvl e' la richiesta di un
  // CALCOLO ("dammene fino a n"), mtvl e' il ripristino di uno stato. Usare
  // setvl per ripristinare funzionerebbe per caso, perche' il valore salvato e'
  // gia' <= VLMAX, e confonderebbe due intenzioni diverse.
  //
  // NOTA DI DISEGNO, che si scopre solo provando a fermare la macchina: in
  // RISC-V "V" la maschera E' v0, un registro vettoriale ordinario, e non per
  // economia di codifica — perche' il context switch non abbia un caso
  // speciale. Qui vmask e' un registro a se', e quella divergenza costa
  // esattamente queste due istruzioni.
  OP_MFVL,    // a=rd                          -> r[rd] = vl (NON distruttivo)
  OP_MTVL,    // b=rs1                         -> vl = min(r[rs1], VLMAX)
  OP_MFVMASK, // a=rd                          -> r[rd] = vmask (64 bit)
  OP_MTVMASK, // b=rs1                         -> vmask = r[rs1]

  // IN FONDO, E NON IN MEZZO. Gli opcode sono numerati per posizione e finiscono
  // cosi' negli oggetti: inserirne uno nel mezzo rinumera tutti quelli dopo, e
  // il 14/09/2026 questo ha mosso l'impronta di TUTTI E QUINDICI i programmi --
  // anche quelli puliti, che `mark` non la contengono nemmeno. Non era
  // strumentazione che filtrava in produzione, era il formato che cambiava
  // sotto. In coda invece non tocca niente di esistente.
  OP_MARK,   // a=porto, imm=valore           -> annota una marca (strumentazione)
  OP_MFCAUSE, // a=rd                         -> r[rd] = cause (chi ha interrotto)

  // vcvt (28/09/2026): la PRIMA conversione intero -> float dell'ISA, e non ce
  // n'era nessuna. Nasce con l'ADC a 12 bit: i campioni sono interi, la FFT e'
  // in float. Il registro vettoriale porta BIT, non un tipo -- vload copia la
  // parola cosi' com'e' -- e vcvt la reinterpreta come int32 e la converte. E'
  // il modello di RVV (vfcvt.f.x.v). Solo vettoriale: e' l'unica che serve.
  OP_VCVT,   // a=vd, b=vs                    -> v[vd][i] = (float) (int32) bits(v[vs][i])

  // srai (29/09/2026, §3.73): lo shift aritmetico a destra, che mancava. `srli`
  // fa scorrere il pattern a 64 bit in complemento a due, quindi su un negativo
  // non e' una divisione: `srli` di -64 per 3 da' 2305843009213693944. Con `div`
  // il quoziente e' giusto ma costa 20 cicli, e questo 1 -- e il posto dove
  // serve e' la time line, che divide istanti in cicli e ha differenze negative
  // per costruzione (uno scarto in ritardo e' negativo).
  //
  // NON E' `div` PER UNA POTENZA DI DUE, e la differenza e' l'arrotondamento:
  // `srai` va verso meno infinito, `div` tronca verso zero. Coincidono sui
  // multipli esatti (-64>>3 == -64/8 == -8) e non sugli altri (-65>>3 e' -9,
  // -65/8 e' -8). Chi vuole il troncamento usa `div`; chi vuole il floor --
  // che per un tempo e' quasi sempre la cosa giusta -- usa questa.
  OP_SRAI    // a=rd, b=rs1, imm=n            -> r[rd] = r[rs1] >> n, col segno
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
//  Lo stato di UNA CPU  (dal 29/09/2026: solo la CPU)
//
//  Fino al 29/09 questa struttura conteneva la macchina intera: la memoria, i
//  device e il tempo. Adesso contiene cio' che un core ha di suo -- registri,
//  pc, parola di stato, trap, il timer PRIVATO, le statistiche -- e un
//  puntatore alla SCHEDA a cui e' collegato (machine.h), attraverso cui passa
//  ogni accesso alla memoria e ai device.
//
//  Il timer resta qui perche' si programma con un'istruzione (settimer), non
//  via MMIO: e' il timer privato di ogni core, come il generic timer dei core
//  ARM. `cycles` sono i cicli DI QUESTA CPU; il tempo della macchina e' il
//  clock master, e i due coincidono finche' il divisore della CPU e' 1.
// ---------------------------------------------------------------------------
struct VMachine;

typedef struct
{
  int64_t r[NUM_SCALAR];
  // f0..f15 sono float a 32 BIT, come la memoria e come v0..v7 (28/09/2026).
  // Fino ad allora erano double: ctx_save li salva con fsw, a 32 bit, e un task
  // interrotto riprendeva coi suoi float ARROTONDATI -- il risultato dipendeva
  // da dove cadevano gli interrupt. L'excess precision dell'8087, dentro il
  // contesto. tests/test_fctx.vasm e' la prova, vista rossa prima.
  float   f[NUM_FLOAT];
  // Il banco NON attivo (PSW_BANK): r2..r13, r15 e f0..f15 di chi non gira in
  // quel banco. Si usano solo quegli indici; gli altri restano a zero.
  int64_t alt_r[NUM_SCALAR];
  float   alt_f[NUM_FLOAT];
  float   v[NUM_VECTOR][VLMAX];
  int     vl;
  uint64_t vmask;   // per-element predicate written by vms* compares (bit i = element i)

  int64_t pc;
  int     halted;

  // Interrupt / status word (scalar control layer). All zero at reset, so a
  // program that never enables interrupts runs exactly as before.
  uint64_t psw;          // program status word: bit 0 = IE (interrupt enable)
  int64_t  epc;          // PC saved on trap
  uint64_t epsw;         // PSW saved on trap (the shadow copy)
  int32_t  cause;        // CHI ha interrotto: CAUSE_TIMER o CAUSE_KBD. La
                         // scrive la macchina prima di saltare al vettore, e
                         // resta valida finche' non arriva la trap successiva
                         // -- come mcause, e come mcause va letta SUBITO se
                         // l'ISR riabilita gli interrupt.
  int64_t  handler;      // trap vector (instruction index)
  int64_t  timer_period; // cycles between timer interrupts (0 = disabled)
  uint64_t timer_next;   // cycle count at which the next timer interrupt is due

  // LA RICHIESTA ALZATA, che non e' la consegna. Il timer matura quando
  // `cycles >= timer_next`, e questo NON dipende da PSW_IE: a interrupt chiusi
  // la richiesta resta alzata e la trap arriva dopo. Fra i due istanti c'e' la
  // latenza di consegna, che dal 15/09/2026 si misura sul canale MARK_TIMER --
  // e che senza questo campo non sarebbe osservabile, perche' la condizione di
  // consegna metteva PSW_IE per primo e quindi a interrupt chiusi non valutava
  // niente. Serve a marcare la richiesta UNA VOLTA: senza, si rimarcherebbe a
  // ogni istruzione per tutto il tempo che la trap aspetta.
  unsigned char timer_pending;  // 1 = scaduto e non ancora consegnato
  int64_t  timer_seq;           // quante richieste ha alzato, dalla prima

  // IL TIMBRO DELLE MARCHE, che e' di questa CPU e non del marcatore: quale
  // task gira (il valore di `current` del SUO programma) e a che profondita'
  // di trap. Il marcatore lo legge da qui (devices.h, `timbro`).
  int64_t  mark_current_addr; // indirizzo di `current`, 0 = non noto al loader
  int32_t  mark_current;      // ultimo valore visto: e' il timbro di ogni marca

  // Profondita' di trap: la macchina la sa esattamente, perche' e' lei che
  // prende la trap e lei che esegue reti. Serve a VERIFICARE l'`owner`
  // dichiarato nel catalogo -- un canale che dice "isr" e marca in contesto di
  // task e' un tag nel posto sbagliato -- e non a dedurlo: chi scrive il tag sa
  // dov'e', il catalogo lo dichiara, e questo lo controlla. Non e' la stessa
  // cosa di PSW_IE: IE = 0 vale anche in una sezione critica di task.
  int32_t  trap_depth;

  // statistics
  uint64_t instr_count;    // total executed instructions
  uint64_t vec_elem_ops;   // total per-element vector operations (measure of SIMD work)
  uint64_t cycles;         // total cycles under the timing model

  struct VMachine* m;      // la scheda: memoria, device, clock

  // Dal 29/09/2026 (tappa 2, §3.79): QUALE CPU, il SUO programma, e il
  // prefisso delle sue righe d'uscita -- "" con una CPU sola, cosi' l'uscita
  // resta identica a prima, "[cpuN] " su tutte quando le CPU sono piu' d'una.
  int          id;
  const char*  tag;
  const Instr* prog;
  int          prog_len;
} VCpu;

// ---------------------------------------------------------------------------
//  API del core. Il ciclo di esecuzione, le trap e il debugger sono della
//  scheda (machine.h): il core esegue istruzioni e prende trap, e non sa chi
//  gliele manda.
// ---------------------------------------------------------------------------
void vcpu_init(VCpu* cpu);

// UNA istruzione: pc avanti, costo sul conto dei cicli, i bit sporchi
// (VDIRTY, FDIRTY), esecuzione.
void vcpu_step(VCpu* cpu, const Instr* in);

// Prende una trap con la causa data: epc, epsw, IE giu', pc al vettore.
void vcpu_trap(VCpu* cpu, int32_t cause);

// Assemble a .vasm file into 'prog'. Returns number of instructions, or -1 on
// error (message written to 'err'). Data directives are written into 'mem',
// la memoria della scheda: l'assembler non ha bisogno di conoscere la CPU.
int assemble(const char* path, uint8_t* mem, Instr* prog, char* err, size_t errsz);

// Un simbolo DI DATO dell'ultimo assemble(), per nome. Torna 1 se c'e'.
//
// Serve al percorso a file singolo, che non produce una VImage e quindi non ha
// una tabella dei simboli da consegnare a chi gli serve. Oggi lo chiama uno
// solo: il registratore delle marche, che per il canale 0 deve sapere dove sta
// `current` -- senza, quel canale resterebbe vuoto anche su un programma che
// uno scheduler ce l'ha.
int assemble_symbol(const char* name, int64_t* out);

// Run modes: normal, per-instruction trace, or interactive step debugger.
typedef enum { RUN_NORMAL, RUN_TRACE, RUN_DEBUG } RunMode;

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
