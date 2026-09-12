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

// ---------------------------------------------------------------------------
//  MMIO — i registri delle periferiche, e perche' stanno SOPRA la RAM
//
//  Su una macchina vera i registri di periferica si leggono con una load
//  normale, e quello e' il motivo per cui qui non c'e' nessuna istruzione
//  nuova: la ISA resta quella che e'. L'intervallo comincia a MEM_SIZE, cioe'
//  SUBITO DOPO l'ultimo byte di RAM, e non dentro:
//
//    - non sottrae un byte di memoria ai programmi, e nessun linker deve
//      sapere di doverlo evitare (oggi non saprebbe: concatena in command
//      order, senza regioni);
//    - una collisione fra un dato e un registro di periferica e' IMPOSSIBILE
//      invece che improbabile;
//    - l'errore "out of bounds" resta quello che era, per gli indirizzi che
//      non sono ne' RAM ne' device.
//
//  Il prezzo e' un confronto in piu' per ogni load/store scalare, accanto al
//  controllo dei limiti che c'era gia'.
// ---------------------------------------------------------------------------
#define MMIO_BASE   ((int64_t) MEM_SIZE)   // 0x100000
#define MMIO_SIZE   ((int64_t) 0x1000)

// Tastiera: due registri, il modello di ogni UART.
//
//  KBD_STATUS  sola lettura   bit 0 = c'e' un carattere, bit 1 = overrun
//  KBD_DATA    lettura        restituisce il carattere E CONSUMA IL FLAG
//
//  Che la lettura abbia un EFFETTO e' la differenza fra memoria e MMIO, non un
//  dettaglio: e' cio' che rende il protocollo corretto senza un handshake, ed
//  e' il motivo per cui load_i32 non puo' piu' prendere un puntatore const.
#define KBD_STATUS  (MMIO_BASE + 0)
#define KBD_DATA    (MMIO_BASE + 4)

#define KBD_READY    1   // bit 0 di KBD_STATUS
#define KBD_OVERRUN  2   // bit 1: ne e' arrivato un altro prima della lettura

// Un evento della traccia: "al ciclo N arriva il carattere c".
typedef struct
{
  uint64_t      cycle;
  unsigned char ch;
} KbdEvent;

#define KBD_TRACE_MAX 64

// ---------------------------------------------------------------------------
//  MARCATORE — l'oscilloscopio a piu' tracce  (PROTOTIPO, 12/09/2026)
//
//  L'idea e' dell'utente, e la forma e' quella con cui si misura sul ferro: un
//  tag all'inizio della regione che interessa e uno alla fine, con due
//  identificatori -- la CATEGORIA della misura e il PUNTO dentro quella
//  categoria -- letti poi come i canali di un oscilloscopio. E' il toggle di un
//  GPIO guardato con l'analizzatore di stato logico, formalizzato; le versioni
//  industriali sono le stimulus port dell'ITM di CoreSight e SystemView.
//
//  --- COSA MISURA, CHE LA TRACCIA DEL PC NON PUO' ---
//  La traccia dice CHI occupava la CPU. Una finestra aperta in un task e chiusa
//  in un altro dice quanto e' durata una RICHIESTA, attraverso le commutazioni:
//  e' il tempo di RISPOSTA. La differenza fra i due e' l'INTERFERENZA, cioe' il
//  numero che conta in un kernel realtime e che qui non si e' mai misurato.
//
//  --- IL CANALE E' L'INDIRIZZO ---
//  Non una parola con dei campi impacchettati: l'assembler non valuta
//  espressioni (".equ B (A << 16)" non assembla), quindi comporre canale e
//  valore a tempo di compilazione non si puo'. E il rimedio e' migliore del
//  problema -- 32 registri contigui, uno per canale, come l'ITM ne ha 32:
//
//      li  r1, MARCA_RISPOSTA     ; il canale: una costante simbolica
//      li  r2, P_CONSEGNA         ; il valore: != 0 APRE
//      sw  r2, 0(r1)
//      ...
//      sw  r0, 0(r1)              ; 0 CHIUDE, e r0 e' gia' zero: UNA istruzione
//
//  --- PERCHE' NON UN'ISTRUZIONE NUOVA ---
//  Stessa ragione di §3.37: il punto d'innesto esiste gia' (store_i32 ha l'if),
//  ISA e assembler non si toccano, e il costo del tag e' quello di una sw --
//  contabile a mano leggendo il listato. Un probe a costo zero sarebbe comodo e
//  insegnerebbe il falso: sul ferro la strumentazione si paga sempre. E si paga
//  ANCHE quando la registrazione e' spenta: la sw viene eseguita lo stesso, e'
//  solo la macchina che non annota. Il costo sta nel programma, non nell'opzione.
//
//  --- DUE CANALI LI SCRIVE LA MACCHINA, E COSTANO ZERO ---
//    MARCA_ESEC   chi possiede la CPU. Il registratore osserva le scritture a
//                 `current` -- l'indirizzo glielo dice il loader, che ha la
//                 tabella dei simboli -- e annota i CAMBI. Zero istruzioni nel
//                 kernel, e il possesso diventa un DATO invece di un'inferenza
//                 dal pc (che e' come traccia.py lo ricava oggi, con gli
//                 artefatti di attribuzione che §3.34 dichiara).
//    MARCA_TASTO  quando un carattere diventa disponibile. Serve perche' il
//                 PROGRAMMA NON PUO' SAPERLO: sa quando se n'e' accorto, e fra
//                 i due c'e' il ritardo del polling, che e' proprio una delle
//                 cose da misurare. Senza questo canale il tempo di risposta
//                 non e' scrivibile con i soli tag applicativi.
//
//  Quello che il canale MARCA_ESEC NON dice e' il PERCHE' della commutazione
//  (preemption, blocco, cessione, fine turno): lo sa solo il dispatcher, e per
//  averlo serve un tag nel kernel -- che costa cicli sul percorso caldo, cioe'
//  rimisurare ogni EXPECT che dipende dai cicli. E' un passo a se', e prima
//  vuole l'assemblaggio condizionale che l'assembler non ha.
// ---------------------------------------------------------------------------
#define MARCA_BASE     (MMIO_BASE + 0x100)
#define MARCA_CANALI   32
#define MARCA_ESEC     0        // riservato: lo scrive la macchina (current)
#define MARCA_TASTO    1        // riservato: lo scrive il device

// Un evento: "al ciclo N il canale C prende il valore V, mentre girava T".
//
// UN SOLO FORMATO per tutto, ed e' il motivo per cui non ci sono due tipi di
// evento: aprire e' "V != 0", chiudere e' "V == 0", e il cambio di task e' il
// canale 0 che prende un valore nuovo -- la chiusura del turno precedente e'
// implicita nell'apertura del successivo. E' come si comporta un LIVELLO su un
// oscilloscopio, dove un impulso non e' altro che un livello che va su e torna
// giu'. Il prezzo, dichiarato: dentro una categoria non si annida, perche' un
// canale ha un valore per volta. Due misure annidate vogliono due canali.
typedef struct
{
  uint64_t cycle;
  int      canale;
  int32_t  valore;
  int32_t  current;    // chi girava: lo timbra la macchina, non il programma
} Marca;

#define MARCHE_MAX 8192

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
//  salvare duemila byte per i task che i vettori non li toccano.
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
// ---------------------------------------------------------------------------
#define PSW_VDIRTY 0x2ULL   // bit 1: stato vettoriale da salvare

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
  OP_MTVMASK  // b=rs1                         -> vmask = r[rs1]
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

  // Tastiera (MMIO, vedi KBD_STATUS/KBD_DATA). Lo STATO del device e CHI LO
  // RIEMPIE sono separati di proposito: questi tre campi sono cio' che il
  // programma vede, e sotto puo' starci una traccia a cicli (deterministica,
  // rigiocabile in ctest) oppure -- domani -- un thread che legge stdin. Il
  // .vasm non distingue i due casi, ed e' la ragione per cui la stessa
  // applicazione puo' essere insieme la demo e il test.
  unsigned char kbd_data;      // l'ultimo carattere arrivato
  unsigned char kbd_ready;     // 1 = non ancora letto
  unsigned char kbd_overrun;   // 1 = ne e' arrivato un altro prima della lettura

  // L'alimentatore deterministico: eventi ordinati per ciclo, consumati dal
  // loop mentre il tempo SIMULATO avanza. Niente tempo di parete qui dentro.
  KbdEvent kbd_trace[KBD_TRACE_MAX];
  int      kbd_trace_len;
  int      kbd_trace_pos;

  // Marcatore (vedi MARCA_BASE). La registrazione si accende da riga di
  // comando; le sw dei tag costano i loro cicli comunque, ed e' voluto.
  Marca    marche[MARCHE_MAX];
  int      marche_len;
  uint64_t marche_perse;       // oltre il tetto: DICHIARATE, non perse in silenzio
  int      marca_on;           // 1 = annota (--marche)
  int64_t  marca_current_addr; // indirizzo di `current`, 0 = non noto al loader
  int32_t  marca_current;      // ultimo valore visto: e' il timbro di ogni marca

  // statistics
  uint64_t instr_count;    // total executed instructions
  uint64_t vec_elem_ops;   // total per-element vector operations (measure of SIMD work)
  uint64_t cycles;         // total cycles under the timing model
} VCpu;

// ---------------------------------------------------------------------------
//  API
// ---------------------------------------------------------------------------
void vcpu_init(VCpu* cpu);

// Carica la traccia della tastiera da una specifica testuale:
//
//     "1200:a,3000:b,3000:c"      ciclo:carattere, separati da virgola
//
// I cicli devono essere non decrescenti (la traccia si consuma in ordine).
// Ritorna 0, o -1 con il messaggio in 'err'. Va chiamata dopo vcpu_init.
int vcpu_kbd_trace(VCpu* cpu, const char* spec, char* err, size_t errsz);

// Annota una marca. La chiamano il device (MARCA_TASTO), lo store watcher
// (MARCA_ESEC) e le sw dei tag applicativi.
void vcpu_marca(VCpu* cpu, int canale, int32_t valore);

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
