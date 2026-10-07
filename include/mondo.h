#ifndef MONDO_H
#define MONDO_H

// ---------------------------------------------------------------------------
//  mondo.h — IL MONDO ESTERNO: un processo, in lockstep (07/10/2026, §3.84)
//
//  Sul ferro i pezzi sono tre e i due apparati non si conoscono:
//
//      TX (macchina)  --antenna-->  il MARE (mondo)  --antenna-->  ADC (macchina)
//
//  Il mondo e' un'APPLICAZIONE LINUX SEPARATA (decisione dell'utente, §3.82),
//  lanciata con `--eco-cmd "<comando>"`: la copia del simulatore di bersaglio da
//  banco. Si sostituisce senza toccare la macchina, ed e' il motivo per cui non
//  e' un modulo C qui dentro.
//
//  --- IL TEMPO E' DEL SIMULATORE ---
//  Il manuale l'aveva gia' scritto per la tastiera: un alimentatore che vivesse
//  nel tempo di parete non sarebbe riproducibile. Quindi LOCKSTEP: il
//  simulatore domanda, aspetta la risposta, e va avanti; il mondo e' una
//  funzione pura delle domande. Il protocollo, testo su stdin/stdout:
//
//      CLOCK <hz>                        una volta, all'avvio
//      TX <istante> <segno>              a ogni TX, segno +1/-1; nessuna risposta
//      BLOCCO <istante_1> <periodo> <n>  all'avvio di un blocco: n righe "I Q"
//
//  Istanti e periodo in tick del clock master. I e Q tornano REALI: arrotondare
//  e saturare e' dell'ADC.
//
//  --- CHI LO COLLEGA ---
//  La SCHEDA (machine.c): quando il trasmettitore parte lo dice al mondo, e
//  quando l'ADC avvia un blocco gli chiede i campioni e li mette nel buffer
//  dell'ADC. Nessun device sa che il mondo e' un processo.
//
//  --- UN MONDO CHE MUORE O RISPONDE MALE E' UN GUASTO DEL SIMULATORE ---
//  Non un errore del programma: si dice, le CPU si fermano, e l'uscita e' 1.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct
{
  int   on;              // c'e' un mondo
  int   guasto;          // e' morto, o ha risposto male: la corsa non vale
  pid_t pid;
  FILE* verso;           // la sua stdin
  FILE* da;              // la sua stdout
} VMondo;

// Lancia `/bin/sh -c cmd` e gli manda CLOCK. Torna 0, o -1 con l'errore in err.
int  mondo_avvia(VMondo* w, const char* cmd, uint64_t hz, char* err, size_t errsz);

// Un TX all'istante t. Torna 0, o -1 (e il mondo e' guasto).
int  mondo_tx(VMondo* w, uint64_t t, int neg);

// I campioni di un blocco: iq[2i] = I, iq[2i+1] = Q. Torna 0 o -1.
int  mondo_blocco(VMondo* w, uint64_t t1, uint64_t periodo, int n, double* iq);

// Chiude le pipe e aspetta il processo. Torna 0, o -1 se e' uscito male.
int  mondo_chiudi(VMondo* w);

#endif // MONDO_H
