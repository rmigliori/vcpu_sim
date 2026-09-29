#ifndef MACHINE_H
#define MACHINE_H

// ---------------------------------------------------------------------------
//  machine.h — LA SCHEDA: il clock, la memoria, il bus, le CPU e i device
//  (29/09/2026, la ristrutturazione pura)
//
//  Fino al 29/09 la macchina era la CPU: la memoria, i device e il tempo
//  stavano dentro VCpu, e il tempo della macchina ERA il conto dei cicli della
//  CPU. E' la prima tappa verso una macchina a piu' CPU (AMP, decisa il 29/09):
//  con due CPU il tempo non puo' appartenere a una di loro, e una periferica
//  non e' di nessuna. Questa tappa cambia la FORMA e non il comportamento: con
//  una CPU e i divisori a 1 ogni corsa e' identica al ciclo a quella di prima,
//  e lo prova il confronto delle tracce --trace su tutti i programmi.
//
//  --- IL CLOCK: un master, e un divisore INTERO per dominio ---
//  Come l'albero di clock di un chip vero: un oscillatore e dei prescaler. Il
//  tempo della macchina e' il conto dei tick master, e ogni dominio lo vede
//  attraverso il suo divisore. Solo interi, quindi tutto resta esatto e
//  deterministico; due frequenze non multiple vogliono un master multiplo
//  comune, come farebbe un PLL. I domini sono SINCRONI -- tutti dallo stesso
//  master, in fase all'accensione -- e i problemi di clock domain crossing di
//  due oscillatori indipendenti qui non esistono: e' una semplificazione.
//
//  Il clock NON genera eventi e non e' un processo: il tempo simulato si
//  calcola, non scorre. Il ciclo principale porta i device fino all'istante
//  della CPU e poi la fa avanzare di un'istruzione (machine.c).
// ---------------------------------------------------------------------------

#include "vcpu.h"
#include "devices.h"

// Una CPU, per ora. Quante, e chi possiede quali device, e' la tappa 2.
#define NUM_CPU 1

typedef struct VClock
{
  uint64_t hz;          // il master: CPU_HZ all'accensione
  uint64_t div_cpu;     // tick master per ciclo di CPU
  uint64_t div_periph;  // tick master per tick periferiche (ADC_PERIOD)
  uint64_t div_ms;      // tick master per millisecondo (CLOCK_MS, comparatori)
} VClock;

typedef struct VMachine
{
  VClock   clock;
  VCpu     cpu[NUM_CPU];
  uint8_t  mem[MEM_SIZE];
  VKbd     kbd;
  VCmp     cmp;
  VAdc     adc;
  VMarker  marker;
} VMachine;

// Azzera tutto, collega le CPU alla scheda, e accende il clock: master a
// CPU_HZ, CPU e periferiche a divisore 1, i millisecondi a CPU_HZ/1000.
void machine_init(VMachine* m);

// Il tempo di una CPU in tick master: i suoi cicli per il suo divisore.
uint64_t machine_now(const VMachine* m, const VCpu* cpu);

// --- il bus: ogni accesso di una CPU alla memoria o ai device passa di qui --
int32_t machine_load32(VMachine* m, VCpu* cpu, int64_t addr);
void    machine_store32(VMachine* m, VCpu* cpu, int64_t addr, int32_t value);
float   machine_loadf(const VMachine* m, int64_t addr);
void    machine_storef(VMachine* m, int64_t addr, float value);

// L'istruzione `mark`: una marca su un porto del marcatore, senza registri.
// Torna 0, o -1 se il porto non e' un canale o e' riservato (la CPU si ferma:
// e' un errore di costruzione del programma).
int     machine_mark(VMachine* m, VCpu* cpu, int64_t port, int32_t value);

// Esegue il programma sulla CPU 0 fino a halt o alla fine del programma.
void    machine_run(VMachine* m, const Instr* prog, int prog_len, RunMode mode, int64_t entry);

#endif // MACHINE_H
