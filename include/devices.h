#ifndef DEVICES_H
#define DEVICES_H

// ---------------------------------------------------------------------------
//  devices.h — LE PERIFERICHE: il datasheet, lo stato, il comportamento
//  (29/09/2026, la ristrutturazione pura)
//
//  Fino al 29/09 tutto questo stava in vcpu.h, e lo stato dei device stava
//  DENTRO la struttura della CPU, insieme alla memoria e al tempo. Con una CPU
//  sola non costava niente; con due non si puo': una periferica non e' di una
//  CPU, e' della scheda. Il testo qui sotto, fino a MARCHE_MAX, e' quello di
//  vcpu.h SPOSTATO ALLA LETTERA: dove dice "cycles" intende il tempo della
//  macchina, che dal 29/09 e' il clock master (machine.h) e con i divisori a
//  1 coincide coi cicli della CPU.
//
//  --- IL CONTRATTO DI UN DEVICE VERSO LA SCHEDA ---
//    *_load / *_store    un accesso MMIO dalla CPU. Tornano 1 se l'indirizzo
//                        e' loro, 0 altrimenti: la decodifica la fa il bus
//                        (machine.c), e il device conosce solo i suoi registri
//    *_advance(t)        porta il device fino all'istante t, in tick del clock
//                        master: i caratteri maturati, i campioni presi
//    *_irq / *_pending   la LINEA di interruzione: una domanda, non un evento
//
//  Il tempo arriva SEMPRE da fuori, come argomento: nessun device ha un
//  orologio suo, e nessuno sa quante CPU ci sono. E' cio' che rende il
//  contratto uguale con una CPU e con due.
//
//  Il marcatore e' un device anche lui, ma il suo TIMBRO -- chi girava, a
//  che profondita' di trap -- e' della CPU che lo tocca, e gli arriva come
//  argomento (`timbro`).
// ---------------------------------------------------------------------------

#include "vcpu.h"

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
//  e' il motivo per cui machine_load32 (il bus, machine.c) non prende una
//  scheda const.
#define KBD_STATUS  (MMIO_BASE + 0)
#define KBD_DATA    (MMIO_BASE + 4)

#define KBD_READY    1   // bit 0 di KBD_STATUS
#define KBD_OVERRUN  2   // bit 1: ne e' arrivato un altro prima della lettura

//  KBD_CTRL    lettura/scrittura   bit 0 = l'interrupt della tastiera e' armato
//
//  LA SECONDA SORGENTE DI INTERRUZIONE (14/09/2026, §3.67). Fino a oggi ce n'era
//  una sola, il timer, e la tastiera si interrogava -- ed e' per questo che il
//  marcatore ha un canale riservato al tasto: fra "il carattere e' arrivato" e
//  "il programma se n'e' accorto" c'e' il ritardo del polling, che il programma
//  non puo' vedere.
//
//  NASCE SPENTA, e non e' prudenza: `test_events` interroga la tastiera senza
//  installare nessun vettore, e un interrupt acceso di default lo manderebbe a
//  saltare su un handler che non esiste. Chi la vuole la arma, come si arma il
//  timer con settimer.
//
//  A LIVELLO e non a fronte: la condizione e' `kbd_ready`, che resta alzato
//  finche' qualcuno non legge KBD_DATA. Un carattere arrivato mentre IE e' 0
//  non si perde -- la trap scatta appena gli interrupt riaprono -- e non serve
//  nessun bit di pending separato.
#define KBD_CTRL    (MMIO_BASE + 8)
#define KBD_IE       1   // bit 0 di KBD_CTRL

// ---------------------------------------------------------------------------
//  L'OROLOGIO DI SISTEMA — un free running counter in MILLISECONDI (15/09/2026)
//
//  CLOCK_MS   sola lettura   i millisecondi trascorsi dall'accensione
//
//  --- A CHE COSA SERVE, e non e' una comodita' ---
//  Senza, il tempo che un programma puo' misurare e' il CONTEGGIO DEI RISVEGLI:
//  "sono arrivati N tick". Un timeout che scatta su quel conteggio scatta
//  perche' e' arrivato l'N-esimo tick, non perche' il tempo sia passato -- e i
//  due non sono la stessa cosa, perche' il tick si consegna in ritardo e il
//  ritardo non si recupera (la latenza di consegna di §3.71, e il drift che ne
//  e' la somma). Con un orologio, "ho un timeout di 10 ms e ne sono passati
//  10,5" e' una FRASE SCRIVIBILE, e il ritardo diventa un numero invece che una
//  cosa che si subisce.
//
//  --- PERCHE' NON C'E' UN'ISTRUZIONE NUOVA ---
//  Stesso argomento della tastiera (hal/kbd.vinc): su una macchina vera un
//  registro di periferica si legge con una load, e la ISA qui e' la parte che
//  fa da surrogato del ferro -- si muove il meno possibile. In piu' un opcode
//  nuovo si numera per POSIZIONE nell'enum e finisce cosi' negli oggetti: il
//  14/09/2026 inserirne uno in mezzo ha mosso l'impronta di tutti e quindici i
//  programmi, anche i puliti. Un indirizzo non muove niente.
//
//  --- PERCHE' MILLISECONDI, e l'unita' e' la DECISIONE, non un dettaglio ---
//  L'unita' la fissa il WRAP, perche' la regola con cui si confrontano le
//  scadenze ("scadenza - adesso", mai i valori) regge solo finche' il contatore
//  non gira. A 32 bit: in CICLI a 100 MHz il contatore gira ogni ~43 secondi,
//  in microsecondi ogni ~71 minuti, in MILLISECONDI ogni ~49 giorni. Un timeout
//  di dieci minuti e' una cosa che un sistema vero chiede; a 43 secondi di giro
//  non si puo' nemmeno esprimere.
//
//  Il prezzo e' la RISOLUZIONE, e va saputo: un millisecondo sono 100.000 cicli,
//  cioe' 25 tick della suite di test, che gira con un tick compresso di 25-2000
//  volte (§7 di docs/scheduler-facts.md). Su questa macchina l'intero
//  test_mondo dura 102.055 cicli, cioe' 1,02 ms: questo contatore, durante
//  quella corsa, cambia valore UNA VOLTA. Il contatore non e' sbagliato -- e'
//  la compressione del tick a essere finta -- ma finche' quella compressione
//  c'e', l'orologio e il tick misurano su due scale che non si parlano.
//
//  --- E' UN PRESCALER, non una divisione ---
//  Il valore e' `cycles / (CPU_HZ / 1000)`: il conto dei cicli e' la sorgente, e
//  CPU_HZ e' la meta' del modello di timing che gia' viaggia con la macchina
//  (§3.56). Un solo posto dichiara la frequenza, e l'orologio la segue.
//
//  --- FREE RUNNING: non si azzera e non si scrive ---
//  Non c'e' un registro di controllo e non c'e' un reset. Un contatore che si
//  puo' azzerare e' un contatore di cui bisogna sapere CHI l'ha azzerato e
//  quando, e due clienti che lo azzerassero si romperebbero a vicenda. Chi
//  vuole un intervallo si conserva la lettura precedente e sottrae: e' il
//  modello di mtime su RISC-V, e la sottrazione e' gia' la regola con cui
//  timeout.vinc confronta le scadenze. Scriverci cade sul ramo "read-only
//  register" di mmio_store, che e' esattamente cio' che deve dire.
// ---------------------------------------------------------------------------
#define CLOCK_MS    (MMIO_BASE + 0x0C)

// ---------------------------------------------------------------------------
//  L'ALTRA BASE DEI TEMPI: I CICLI (29/09/2026, §3.73)
//
//  CLOCK_CYCLES   sola lettura   il clock master, i 32 bit bassi
//
//  --- PERCHE' DUE BASI, E NON TRE ---
//  I microsecondi erano il compromesso fra risoluzione e portata, e con le due
//  estreme il compromesso non serve: i CICLI hanno 0,01 us di risoluzione e
//  girano in 43 s, i MILLISECONDI girano in 49 giorni. Un frame dura ms, un
//  timeout di guardia dura minuti. E i us non sono la scala di NESSUN evento di
//  questo sistema.
//
//  --- E NON E' UN ECCESSO ---
//  Con un comparatore la risoluzione e' DISACCOPPIATA dalla frequenza delle
//  interruzioni: un tick a 10 ns sarebbe assurdo, una scadenza a 10 ns costa
//  quanto qualunque altra. Ed e' l'unica base in cui il JITTER e' esprimibile
//  nella stessa unita' della time line -- in millisecondi lo scarto fra la time
//  line disegnata e quella reale sarebbe 0 sempre, cioe' invisibile.
//
//  --- E' IL CLOCK MASTER, non i cicli della CPU che legge ---
//  La sorgente e' `machine_now`, cioe' l'albero di clock della scheda (§3.78):
//  una CPU con `div_cpu` > 1 legge un contatore che avanza piu' in fretta dei
//  suoi cicli. Oggi `div_cpu` e' 1 e le due cose coincidono; il giorno in cui
//  non lo sara', questo registro resta il tempo della SCHEDA, che e' l'unico su
//  cui due CPU possono mettersi d'accordo.
//
//  I 32 bit bassi, come CLOCK_MS: chi confronta due letture le SOTTRAE.
//
//  --- PERCHE' NON E' ACCANTO A CLOCK_MS ---
//  Perche' 0x08 e' KBD_CTRL. Il primo tentativo l'ha messo la' e la lettura
//  tornava 0 in SILENZIO: `mmio_load` interroga i device in ordine e la tastiera
//  risponde prima, quindi una collisione di indirizzi non e' un errore, e' un
//  valore sbagliato. L'ha scoperto un test che calcolava un numero -- lo scarto
//  veniva negativo, cioe' la trap sarebbe arrivata prima della scadenza.
//  Tenerlo a mente per il prossimo registro: la mappa di questo file e' l'unica
//  dichiarazione, e nessuno la verifica.
// ---------------------------------------------------------------------------
#define CLOCK_CYCLES (MMIO_BASE + 0x30)

// ---------------------------------------------------------------------------
//  I COMPARATORI — le scadenze sull'orologio (15/09/2026, §3.72)
//
//  CMP_CTRL       lettura/scrittura   bit n = il canale n e' armato
//                                     bit 16+n = il canale n confronta sui CICLI
//  CMP_BASE + 4n  lettura/scrittura   la scadenza del canale n, nella SUA base
//
//  Un contatore che sale sempre e N registri confrontati con lui: quando la
//  scadenza e' raggiunta, il canale chiede l'interruzione. E' mtime/mtimecmp di
//  RISC-V, ed e' il capture/compare di qualunque timer general-purpose.
//
//  --- E' UN TIMER, NON UNO SCHEDULER ---
//  Questo file dichiara un MECCANISMO. Il foreground a slot, la tabella, il
//  round-robin sono POLITICA e stanno nel software: e' la stessa linea con cui
//  e' scritto `scheduler` (policy) separato da `dispatcher` (mechanism).
//
//  --- PERCHE' DUE, e non uno ---
//  Il bersaglio e' un ibrido foreground/background (§3.72): il foreground vuole
//  un battito ESATTO, il background scadenze arbitrarie. Sono due regimi, e con
//  un comparatore solo il secondo non e' nemmeno ESPRIMIBILE -- quindi non e'
//  provabile, e un percorso che nessun test esercita marcisce. Indicizzarli da
//  subito costa quanto farne uno; il terzo, se servira', e' una costante.
//
//  --- IL CONFRONTO E' UNA DIFFERENZA, MAI UN ORDINE ---
//  La condizione e' `(int32_t)(adesso - scadenza) >= 0`, in aritmetica
//  wrappante a 32 bit. NON `adesso == scadenza`, e non e' un dettaglio:
//
//    - con `==` la condizione, su un contatore in millisecondi, sarebbe vera
//      per 100.000 cicli di fila invece che in un istante;
//    - e soprattutto un compare scritto NEL PASSATO -- riarmo tardivo, o
//      periodo piu' corto del tempo di servizio -- non si verificherebbe MAI
//      PIU', e il timer morirebbe in silenzio. E' il bug classico di mtimecmp,
//      e capita sotto carico, cioe' quando meno te lo puoi permettere.
//
//  Con `>=` una scadenza gia' passata spara SUBITO: "arriva tardi invece di non
//  arrivare", che e' la stessa scelta che §9.2 e §3.70 hanno gia' fatto due
//  volte per il pool vuoto. Ed e' la stessa regola che timeout.vinc dichiara
//  per le scadenze del gestore -- qui e' la prima volta che un hardware la
//  conferma invece di lasciarla scritta in un posto solo.
//
//  --- A LIVELLO, e si abbassa RIPROGRAMMANDO ---
//  Non c'e' un bit di pending: la condizione si rivaluta a ogni confine
//  d'istruzione, quindi finche' e' vera la trap si ripresenta. La si abbassa
//  scrivendo una scadenza nuova, o disarmando il canale -- non la abbassa la
//  trap. E' esattamente il protocollo della tastiera, dove il flag lo abbassa
//  la LETTURA di KBD_DATA e non la trap: l'azione utile e' quella che chiude
//  l'evento, cosi' un'ISR che non fa il suo lavoro se lo ritrova davanti.
//
//  Ne segue che il riarmo periodico costa UNA sola `sw`: sparare non disarma.
//
//  --- NASCONO SPENTI ---
//  `CMP_CTRL` vale 0 al reset, come `kbd_ie`. Senza l'armamento esplicito un
//  canale a zero sarebbe gia' scaduto all'accensione (`0 - 0 >= 0`) e sparerebbe
//  al primo ciclo.
//
//  --- CHI VINCE SE SONO PRONTI INSIEME ---
//  I canali in ordine (0 prima di 1), poi il timer a periodo, poi la tastiera.
//  Il canale 0 e' il posto del FOREGROUND, che ha la precedenza per lo stesso
//  argomento con cui il timer batte la tastiera dal 14/09.
//
//  E non e' un "battito", nonostante la prima stesura di questo commento lo
//  chiamasse cosi' (§3.73): nel modello bersaglio il canale 0 e' il WATCHDOG
//  DELL'ATTIVITA' CORRENTE. Non batte -- porta la deadline dello slot in corso,
//  e in un caso su due non scatta affatto, perche' l'attivita' cede prima.
//
//  Da cui una proprieta' che vale la pena sapere: UN ARMAMENTO SERVE DUE CASI.
//  Se l'attivita' cede, il comparatore gia' armato e' cio' che prelazionera' il
//  background all'istante del prossimo slot; se sfora, lo stesso comparatore
//  scatta ed e' l'errore. A distinguerli non e' il tempo -- e' CHI STAVA
//  GIRANDO quando e' scattato.
// ---------------------------------------------------------------------------
//  --- LA BASE E' DEL CANALE, NON DEL CHIP (29/09/2026, §3.73) ---
//  Ogni canale dice su QUALE base confronta: millisecondi (il default, bit a 0)
//  oppure cicli. Il bit sta in CMP_CTRL accanto all'armamento e non in un
//  registro globale perche' in hardware il prescaler e' del canale -- ed e'
//  anche cio' che serve, visto che i due regimi convivono: il canale 0 porta la
//  deadline di uno slot del foreground, che si misura in cicli, e il canale 1 le
//  scadenze del background, che durano millisecondi.
//
//  Il confronto resta la DIFFERENZA wrappante a 32 bit su tutte e due: cambia
//  solo con chi si sottrae. Un canale che cambia base mentre e' armato sta
//  confrontando una scadenza vecchia con un contatore nuovo, e nessuno lo vieta:
//  e' la stessa regola di sempre -- riarmare e' politica.
#define CMP_CHANNELS  2
#define CMP_CTRL    (MMIO_BASE + 0x10)
#define CMP_BASE    (MMIO_BASE + 0x20)
#define CMP_ARM(n)   (1u << (n))
#define CMP_CYCLES(n) (1u << (16 + (n)))   // 0 = millisecondi, 1 = cicli
#define CMP_CYCLES_SHIFT 16

//  LA CAUSA: chi ha interrotto. Con due sorgenti il vettore deve saperlo, e
//  questa macchina somiglia a RISC-V (epc, epsw, reti), dove la causa e' un CSR
//  letto dal gestore -- non un vettore per sorgente come il NVIC di un
//  Cortex-M. La differenza si paga in latenza: leggere la causa e diramarsi
//  costa, ed e' esattamente la grandezza che questo progetto misura -- quindi
//  il numero che direbbe quanto varrebbe il vettoriamento si potra' misurare
//  invece che stimare.
//
//  Se le due sorgenti sono pronte insieme vince il TIMER: e' il battito dello
//  scheduler e non deve derivare, mentre un carattere aspetta un tick senza
//  che nessuno se ne accorga.
#define CAUSE_TIMER  0
#define CAUSE_KBD    1

//  I comparatori prendono una causa per canale: CAUSE_CMP + n. Due cause
//  distinte invece di una sola piu' un registro di stato, perche' con quattro
//  sorgenti la catena di `beq` e' gia' la grandezza che §3.67 voleva misurare --
//  quanto costa chiedere «chi e' stato» invece di avere un vettore per sorgente.
//  Con una causa condivisa quel costo si pagherebbe due volte, in `mfcause` e
//  poi in una lettura MMIO.
#define CAUSE_CMP    2   // .. CAUSE_CMP + CMP_CHANNELS - 1

// ---------------------------------------------------------------------------
//  L'ADC — 12 bit, I e Q, a BLOCCHI e con la DMA  (28/09/2026)
//
//  Il front-end di un radar altimetro: un blocco di N campioni COMPLESSI
//  scritto in RAM dalla periferica, senza la CPU. La CPU vede il blocco, non
//  il campione (§3.74).
//
//  --- L'ACQUISIZIONE LA AVVIA LA TIME LINE, deciso dall'utente ---
//  Non gira da sola: una scrittura ad ADC_CTRL dice «acquisisci ADC_COUNT
//  campioni a ADC_ADDR, uno ogni ADC_PERIOD cicli». E' lo sweep comandato e la
//  finestra di ricezione aperta. Un ADC libero col suo clock scivolerebbe
//  rispetto al comparatore e riaprirebbe «chi possiede il ritmo» (§3.74), che e'
//  chiusa a favore della CPU. Il ping-pong lo fa il programma: arma il blocco B
//  mentre elabora A, e lo scambio e' la scrittura di ADC_ADDR.
//
//  --- NESSUN INTERRUPT, deciso dall'utente ---
//  A svegliare il super task e' il comparatore, e la WAIT ha un ingresso solo
//  (§3.74). Al posto dell'interrupt c'e' ADC_STATUS, con un CONTATORE dei
//  blocchi finiti: chi prende il blocco verifica che sia finito QUELLO che
//  aspetta, non uno qualunque.
//
//  --- IL FORMATO: I e Q alternati, una parola a 32 bit ciascuno ---
//      ADC_ADDR + 8*i      I del campione i
//      ADC_ADDR + 8*i + 4  Q del campione i
//  Col segno GIA' ESTESO dalla DMA, in [-2048, 2047]; fuori scala SATURA, non
//  riparte dall'altro estremo. Un ADC vero spesso impacchetta 16+16 in una
//  parola: qui spacchettare vorrebbe `srai`, che manca, e allargare nella DMA
//  e' la semplificazione DICHIARATA. Per il float c'e' `vcvt`.
//
//  --- UN CAMPIONE OGNI ADC_PERIOD TICK, scritto quando e' preso ---
//  (tick del clock PERIFERICHE dal 29/09/2026: cicli finche' il divisore e' 1)
//  Il campione i e' preso all'istante `avvio + (i+1)*ADC_PERIOD` -- serve un
//  periodo per convertire -- e finisce in RAM al primo confine d'istruzione da
//  li' in poi, come ogni altra sorgente. Il blocco quindi si RIEMPIE mentre il
//  programma gira, ed e' cio' che rende visibile l'errore da cui il contatore
//  protegge: leggere un blocco a meta'. La DMA non ruba cicli alla CPU: nessuna
//  contesa di bus, ed e' una semplificazione.
//
//  --- IL SEGNALE E' IL MONDO, e sta fuori dalla macchina ---
//  `--adc <frequenza_Hz>,<ampiezza>`: un esponenziale complesso,
//  I = A cos(2 pi f t), Q = A sin(2 pi f t), con t il tempo ASSOLUTO
//  dell'istante di campionamento -- il mondo non riparte quando la CPU avvia
//  l'ADC. La frequenza ha un SEGNO, e con I e Q il segno si vede: e' cio' che
//  prova che I e Q non sono scambiati. Senza `--adc` campiona zero.
//
//  --- ADC_STATUS ---
//      bit 0      ADC_BUSY      sta acquisendo
//      bit 1      ADC_OVERRUN   un avvio e' arrivato mentre acquisiva
//      bit 2..31  i blocchi finiti dall'accensione (ADC_STATUS >> ADC_DONE_SHIFT)
//
//  L'AVVIO MENTRE ACQUISISCE E' IGNORATO e alza ADC_OVERRUN, che resta alzato
//  finche' qualcuno legge ADC_STATUS -- come KBD_OVERRUN, che abbassa la
//  lettura del dato. Non e' un errore del simulatore: e' un errore di tempo
//  della time line, e lo deve vedere il programma, non fermarlo.
//
//  ADC_ADDR, ADC_COUNT e ADC_PERIOD si rileggono; ADC_CTRL si scrive e basta.
// ---------------------------------------------------------------------------
// Dal 29/09/2026 la DMA scrive SOLO nella RAM condivisa: ADC_ADDR sta fra
// SHARED_BASE e SHARED_BASE + SHARED_SIZE (qui sotto).
#define ADC_ADDR     (MMIO_BASE + 0x40)
#define ADC_COUNT    (MMIO_BASE + 0x44)
#define ADC_PERIOD   (MMIO_BASE + 0x48)
#define ADC_CTRL     (MMIO_BASE + 0x4C)
#define ADC_STATUS   (MMIO_BASE + 0x50)

#define ADC_START       1   // ADC_CTRL: avvia
#define ADC_BUSY        1   // ADC_STATUS, bit 0
#define ADC_OVERRUN     2   // ADC_STATUS, bit 1
#define ADC_DONE_SHIFT  2   // ADC_STATUS >> 2 = blocchi finiti
#define ADC_MAX         2047
#define ADC_MIN        (-2048)

// ---------------------------------------------------------------------------
//  IL TRASMETTITORE, e l'accecamento del ricevitore  (07/10/2026, §3.83)
//
//  La meta' dell'altimetro che mancava: l'ADC ascolta, questo parla. Il chirp
//  vero non si modella -- e' un impulso lungo TX_LEN -- e l'eco lo fara' il
//  mondo, che sta fuori dalla macchina (§3.82). Qui c'e' solo cio' che e' del
//  FERRO: quando il TX e' acceso, e che cosa fa al ricevitore.
//
//      TX_LEN     lett./scritt.  la durata del TX, in tick PERIFERICHE (come
//                                ADC_PERIOD): 5000 = 50 us oggi. E' un registro
//                                perche' il chirp lo progetta il programma
//      TX_CTRL    scrittura      bit 0 TX_START = trasmetti; bit 1 TX_NEG = il
//                                segno del codice di Barker di questo TX, che
//                                la macchina conserva per il mondo
//      TX_STATUS  lettura        bit 0 TX_ON      sta trasmettendo
//                                bit 1 TX_BLIND   un campione e' uscito accecato
//                                bit 2 TX_OVERRUN un avvio col TX acceso
//                                e la lettura abbassa BLIND e OVERRUN
//
//  --- LA COMMUTAZIONE T/R E' DELLA SCHEDA, non un registro ---
//  Dal TX non si passa all'RX all'istante («riesci a invertire immediatamente
//  la corsa di un autobus?», l'utente, §3.82): il commutatore ha un tempo, ed
//  e' del ferro. TX_TR_US qui sotto, in microsecondi, convertito in tick
//  master all'accensione: non dipende da nessun divisore.
//
//  --- L'ACCECAMENTO ---
//  Un campione preso mentre il TX e' acceso, o entro TX_TR_US dalla fine,
//  esce SATURATO -- ADC_MAX su I e su Q, cio' che fa un ricevitore abbagliato
//  -- e alza TX_BLIND. Il bit sta qui e non in ADC_STATUS di proposito: li'
//  dal bit 2 in su c'e' il contatore dei blocchi, e spostarlo romperebbe i
//  test dell'ADC. Saturato e non zero: zero e' un campione plausibile, il
//  massimo su tutti e due i canali non lo produce nessun eco del modello.
//
//  --- UN AVVIO COL TX ACCESO E' IGNORATO e alza TX_OVERRUN ---
//  La regola dell'ADC, nella stessa forma: un errore di TEMPO della time line,
//  che il programma deve vedere e non il simulatore fermare. Un TX_LEN non
//  positivo invece e' un errore di COSTRUZIONE, e si dice.
//
//  NESSUN INTERRUPT, come l'ADC: chi trasmette e' la time line, e sa quando.
// ---------------------------------------------------------------------------
#define TX_LEN       (MMIO_BASE + 0x54)
#define TX_CTRL      (MMIO_BASE + 0x58)
#define TX_STATUS    (MMIO_BASE + 0x5C)

#define TX_START        1   // TX_CTRL, bit 0
#define TX_NEG          2   // TX_CTRL, bit 1: il segno Barker
#define TX_ON           1   // TX_STATUS, bit 0
#define TX_BLIND        2   // TX_STATUS, bit 1
#define TX_OVERRUN      4   // TX_STATUS, bit 2
#define TX_TR_US       10   // la commutazione T/R, in microsecondi

// ---------------------------------------------------------------------------
//  PIU' CPU: la RAM CONDIVISA, la MAILBOX e lo SPINLOCK  (29/09/2026, §3.79)
//
//  Ogni CPU ha la sua RAM LOCALE da 0 a MEM_SIZE, allo stesso indirizzo --
//  fisicamente diverse, ognuna vede la propria -- e i programmi di una CPU
//  sola restano identici. Le CPU si incontrano in un posto solo, esplicito:
//
//  SHARED_BASE .. + SHARED_SIZE   la RAM CONDIVISA, vista da tutte allo
//                                 stesso indirizzo. Sopra i device, per la
//                                 stessa ragione per cui i device stanno
//                                 sopra la RAM: un indirizzo che esce dalla
//                                 RAM locale finisce in un errore esplicito
//                                 o in un device, mai nella memoria comune.
//
//  DICHIARATO, e su un chip vero sono i punti dolenti: NIENTE CACHE (una
//  scrittura e' subito visibile all'altra CPU, nessuna coerenza da gestire)
//  e ORDINE SEQUENZIALE (le scritture arrivano nell'ordine del programma,
//  nessuna barriera). Il codice scritto qui e' corretto sulla macchina che
//  lo garantisce.
//
//  --- LO SPINLOCK HARDWARE: il test-and-set sta nella periferica ---
//  HWLOCK_BASE + 4n, n < HWLOCK_COUNT. LEGGERE e' il test-and-set: 0 = era
//  libero e ADESSO E' TUO, 1 = occupato. SCRIVERE 0 lo rilascia. Il device
//  ricorda chi lo possiede, e rilasciare un lock altrui e' un errore di
//  costruzione. Non un'istruzione atomica: in un'AMP di core eterogenei le
//  esclusive spesso non valgono fra core diversi, ed e' per questo che i SoC
//  hanno il blocco hardware (hwspinlock in Linux). NON RICORSIVO: chi lo
//  tiene e lo rilegge lo trova occupato, e il simulatore lo dice una volta.
//
//  LA REGOLA, che e' del programmatore: chi prende uno spinlock chiude PRIMA
//  gli interrupt sulla propria CPU. Se no un'ISR sulla stessa CPU che cerca
//  lo stesso lock gira per sempre aspettando il task che sta interrompendo.
//
//  --- LA MAILBOX: una FIFO di parole per CPU, e un interrupt ---
//  Registri BANKED: tutte le CPU usano gli stessi indirizzi, e il bus sa chi
//  fa l'accesso.
//    MBOX_SEND    scrittura   spedisce una parola all'ALTRA CPU. A FIFO piena
//                             e' IGNORATA e alza MBOX_OVERRUN su chi spedisce:
//                             un errore di tempo, come l'ADC avviato mentre
//                             acquisisce. Non blocca e non perde in silenzio
//    MBOX_RECV    lettura     la prima parola della PROPRIA FIFO, e la toglie.
//                             Leggere una FIFO vuota e' un errore di
//                             costruzione, e da' 0
//    MBOX_STATUS  lettura     bit 0..7 le parole nella PROPRIA FIFO;
//                             MBOX_PEER_FULL la FIFO dell'altra e' piena;
//                             MBOX_OVERRUN una mia spedizione e' andata persa
//                             -- e la lettura lo abbassa
//    MBOX_CTRL    lett./scr.  bit 0 MBOX_IE: la propria FIFO non vuota
//                             interrompe. NASCE SPENTA, come la tastiera
//  L'interrupt e' A LIVELLO: resta alzato finche' la propria FIFO non e'
//  vuota, e lo abbassa la lettura. Porta una PAROLA e CONTA: dice quale
//  messaggio, e due avvisi non si fondono in uno.
//
//  Con due CPU "l'altra" e' una sola. Con piu' CPU servira' un destinatario,
//  e oggi non si scrive.
// ---------------------------------------------------------------------------
#define SHARED_BASE   ((int64_t) 0x200000)
#define SHARED_SIZE   ((int64_t) 0x10000)     // 64 KiB

#define MBOX_SEND     (MMIO_BASE + 0x60)
#define MBOX_RECV     (MMIO_BASE + 0x64)
#define MBOX_STATUS   (MMIO_BASE + 0x68)
#define MBOX_CTRL     (MMIO_BASE + 0x6C)
#define MBOX_DEPTH      4
#define MBOX_COUNT_MASK 0xFF    // MBOX_STATUS, bit 0..7
#define MBOX_PEER_FULL  0x100   // MBOX_STATUS, bit 8
#define MBOX_OVERRUN    0x200   // MBOX_STATUS, bit 9
#define MBOX_IE         1       // MBOX_CTRL, bit 0

#define HWLOCK_BASE   (MMIO_BASE + 0x80)
#define HWLOCK_COUNT    8

// La causa della mailbox: dopo i comparatori, e ultima nelle priorita'.
#define CAUSE_MBOX   (CAUSE_CMP + CMP_CHANNELS)

// ---------------------------------------------------------------------------
//  IL DISTRIBUTORE DELLE INTERRUZIONI  (29/09/2026, §3.79)
//
//  Le periferiche sono di tutte le CPU; le loro LINEE di interruzione vanno
//  dove le si manda. Un registro di DESTINAZIONE per ogni sorgente condivisa,
//  sul modello del distributore del GIC ARM:
//
//    INTD_TARGET + 4n   lettura/scrittura   la CPU a cui va la linea n
//
//  All'accensione tutte vanno alla CPU 0, quindi un programma di una CPU sola
//  non se ne accorge. Una destinazione che non esiste e' un errore di
//  costruzione, e non cambia niente.
//
//  MINIMO DI PROPOSITO: niente priorita' programmabili e niente maschere
//  centrali. L'abilitazione resta nel device (KBD_CTRL), le priorita' restano
//  l'ordine fisso dell'arbitraggio. Comparatori, timer e mailbox non passano
//  di qui: sono gia' per CPU, come le interruzioni private dei core ARM. E
//  l'ADC non ha una linea (§3.76): chi lo usa lo interroga.
//
//  Oggi le linee condivise sono una: la tastiera.
// ---------------------------------------------------------------------------
#define INTD_TARGET   (MMIO_BASE + 0xA0)
#define INTD_KBD        0      // la linea della tastiera
#define INTD_LINES      1

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
//      li  r1, MARK_RESPONSE     ; il canale: una costante simbolica
//      li  r2, P_CONSEGNA         ; il valore: != 0 APRE
//      sw  r2, 0(r1)
//      ...
//      sw  r0, 0(r1)              ; 0 CHIUDE, e r0 e' gia' zero: UNA istruzione
//
//  --- PERCHE' NON UN'ISTRUZIONE NUOVA ---
//  Stessa ragione di §3.37: il punto d'innesto esiste gia' (il bus ha l'if),
//  ISA e assembler non si toccano, e il costo del tag e' quello di una sw --
//  contabile a mano leggendo il listato. Un probe a costo zero sarebbe comodo e
//  insegnerebbe il falso: sul ferro la strumentazione si paga sempre. E si paga
//  ANCHE quando la registrazione e' spenta: la sw viene eseguita lo stesso, e'
//  solo la macchina che non annota. Il costo sta nel programma, non nell'opzione.
//
//  --- TRE CANALI LI SCRIVE LA MACCHINA, E COSTANO ZERO ---
//    MARK_EXEC   chi possiede la CPU. Il registratore osserva le scritture a
//                 `current` -- l'indirizzo glielo dice il loader, che ha la
//                 tabella dei simboli -- e annota i CAMBI. Zero istruzioni nel
//                 kernel, e il possesso diventa un DATO invece di un'inferenza
//                 dal pc (che e' come trace.py lo ricava oggi, con gli
//                 artefatti di attribuzione che §3.34 dichiara).
//    MARK_KEY  quando un carattere diventa disponibile. Serve perche' il
//                 PROGRAMMA NON PUO' SAPERLO: sa quando se n'e' accorto, e fra
//                 i due c'e' il ritardo del polling, che e' proprio una delle
//                 cose da misurare. Senza questo canale il tempo di risposta
//                 non e' scrivibile con i soli tag applicativi.
//    MARK_TIMER  quando il TIMER alza la richiesta di interruzione, che non e'
//                 quando la trap viene consegnata (15/09/2026). E' il gemello
//                 esatto di MARK_KEY -- tutti e due dicono «questa sorgente ha
//                 bussato» -- e vale per la stessa ragione: la consegna aspetta
//                 PSW_IE, quindi il programma vede il secondo istante e non il
//                 primo. La distanza fra i due E' la latenza di consegna, e
//                 prima di questo canale non era una grandezza scrivibile: di
//                 ogni sorgente si osservava un estremo solo, e per giunta
//                 estremi INCROCIATI (del timer la consegna, del tasto
//                 l'arrivo).
//
//  IL BATTITO SLITTA, ED E' UN DIFETTO -- corretta il 15/09/2026 (§3.72) una
//  riga che qui diceva il contrario. `timer_next` si riarma dalla CONSEGNA
//  (`cycles + period`) e non dalla scadenza, quindi ogni ritardo si somma e non
//  viene mai recuperato -- misurato su test_mondo: 276 cicli persi in 24
//  battiti, tutti presi nei due tick in cui la tastiera ha interrotto.
//
//  Fino al 15/09 questo commento diceva «e NON e' un difetto da correggere:
//  e' la semantica di un timeout SOFTWARE, che slitta per definizione». Sbagliato
//  di categoria: `timer_next` non e' un timeout software, e' il TIMER HARDWARE
//  di questa macchina. Un auto-reload vero non slitta -- un SysTick, un PIT,
//  l'ARR di uno STM32 si ricaricano NEL FERRO all'istante del wrap, e il
//  software non partecipa alla cadenza. La forma giusta e' `timer_next +=
//  period`, che somma alla scadenza NOMINALE.
//
//  Il codice non e' stato corretto, e la scelta e' dichiarata: `settimer` e' il
//  percorso che i comparatori sul free running counter rendono legacy, e muovere
//  gli EXPECT di sei test per riparare una cosa in via di dismissione e' lavoro
//  pagato due volte. E' il COMMENTO a fare danno, perche' istruisce chi legge --
//  e infatti ha mandato la formula di ripresa a cercare il rimedio giusto per il
//  problema sbagliato.
//
//  DOVE LO SLITTAMENTO E' FATALE, e non e' teoria: in un foreground a slot
//  (cyclic executive) il frame che riparte dalla consegna sfalda la tabella, e
//  dopo N frame il sistema e' fuori fase con il mondo -- che in un satellite e'
//  la finestra di visibilita'.
//
//  L'ALTRA META' DI QUEL COMMENTO REGGE, ed e' la diagnosi di `tmo_now` e non di
//  `timer_next`: un timeout espresso in TICK non e' un timeout espresso in
//  TEMPO, e sbaglia di piu' proprio quando il sistema ha piu' eventi da servire.
//  Il canale serve a rendere visibile quella distanza invece che a nasconderla.
//
//  Quello che il canale MARK_EXEC NON dice e' il PERCHE' della commutazione
//  (preemption, blocco, cessione, fine turno): lo sa solo il dispatcher, e per
//  averlo serve un tag nel kernel -- che costa cicli sul percorso caldo, cioe'
//  rimisurare ogni EXPECT che dipende dai cicli. E' un passo a se', e prima
//  vuole l'assemblaggio condizionale che l'assembler non ha.
// ---------------------------------------------------------------------------
#define MARK_BASE     (MMIO_BASE + 0x100)
#define MARK_CHANNELS   32
#define MARK_EXEC     0        // riservato: lo scrive la macchina (current)
#define MARK_KEY      1        // riservato: lo scrive il device
#define MARK_TIMER    2        // riservato: lo scrive il timer (la RICHIESTA)
#define MARK_RISERVATI 3       // i canali dell'applicazione partono da qui

// ---------------------------------------------------------------------------
//  IL PORTO DATI: una marca puo' portarsi dietro un VALORE DEL PROGRAMMA.
//
//  Una sw porta UNA parola, e quella e' gia' il marker (quale punto). Il
//  secondo valore -- il codice del messaggio appena ricevuto, la profondita' di
//  una coda -- vuole quindi un secondo porto:
//
//      sw  rDato, 0(rPortoDati)    ARMA il canale
//      sw  rMarker, 0(rCanale)     la marca, e se lo porta via
//
//  UNO PER CANALE, e non uno globale: con un porto solo, un tick fra
//  l'armamento e la marca -- e l'ISR che ne emette una sua -- mangerebbe il
//  dato. Sarebbe un guasto raro, non riproducibile e silenzioso. Per canale,
//  l'ISR marca sui propri e non puo' toccare il tuo.
//
//  E si ricorda SE era armato, non solo cosa c'era: zero e' un valore
//  legittimo (un codice di messaggio puo' valere 0), quindi senza quel bit
//  "il dato era 0" e "mi sono dimenticato di armare" avrebbero lo stesso
//  aspetto -- un numero plausibile al posto di un errore. Col bit, una marca
//  che doveva portare un dato e non ce l'ha si DICHIARA.
//
//  Ogni armamento serve UNA marca: dopo, il canale torna disarmato.
// ---------------------------------------------------------------------------
#define MARKD_BASE    (MARK_BASE + MARK_CHANNELS * 4)

// ---------------------------------------------------------------------------
//  IL PORTO DEI NOMI: dare un nome ai VALORI di un canale, a runtime.
//
//  Il canale 0 porta indirizzi di TCB, e un indirizzo non ha un nome finche'
//  qualcuno non glielo da'. Le due strade scartate, e perche':
//
//    .global sui TCB   il nome lo darebbe la tabella dei simboli, che esiste
//                      solo se la toolchain la pubblica e il tuo strumento sa
//                      leggerla. Su un bersaglio vero con una toolchain
//                      incompleta puo' non esserci.
//    un campo nel TCB  dentro .ifdef, TCB.size varrebbe 24 in una
//                      configurazione e 28 nell'altra, e un oggetto assemblato
//                      in una e linkato con l'altra leggerebbe i campi agli
//                      offset sbagliati SENZA CHE IL LINKER SE NE ACCORGA.
//                      Fuori da .ifdef, ogni programma con task paga i byte
//                      anche quando non si strumenta.
//
//  Qui invece il nome si REGISTRA, come fa SystemView: una scrittura, una volta
//  sola, dentro .ifdef MARKS. Nella build pulita non esiste, quindi non costa
//  un byte e non muove un'impronta.
//
//      li  r1, MARKN_EXEC
//      li  r2, N_EXEC_A        ; l'id, dal catalogo
//      sw  r2, 0(r1)           ; "chiunque io sia adesso, mi chiamo A"
//
//  QUALE valore si sta nominando, e sono due regole sole:
//    canale 0   il valore e' `current`, che la macchina ha gia'
//    gli altri  il valore dev'essere ARMATO sul porto dati, e se non lo e' si
//               dice: nominare un valore che non si e' detto quale sia e'
//               un errore, non un caso da indovinare.
// ---------------------------------------------------------------------------
#define MARKN_BASE    (MARKD_BASE + MARK_CHANNELS * 4)
#define MARK_NAMES_MAX 64

// Una registrazione: "sul canale C, il valore V si chiama <id>".
typedef struct
{
  int      canale;
  int32_t  valore;
  int32_t  id;
} MarcaNome;

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
  int32_t  dato;       // il valore armato sul porto dati, 0 se non armato
  int32_t  ha_dato;    // ...e se lo era: 0 non e' distinguibile da "nessuno"
  int32_t  in_trap;    // 0 = contesto di TASK, >0 = dentro una trap
} Marca;

#define MARCHE_MAX 8192

// ---------------------------------------------------------------------------
//  Lo stato dei device. I campi sono quelli che stavano in VCpu, col prefisso
//  tolto: il prefisso adesso e' il nome della struttura.
// ---------------------------------------------------------------------------

// La tastiera. Lo STATO del device e CHI LO RIEMPIE sono separati di
// proposito: data/ready/overrun/ie sono cio' che il programma vede, e sotto
// c'e' una traccia a cicli, deterministica e rigiocabile in ctest.
typedef struct
{
  unsigned char data;      // l'ultimo carattere arrivato
  unsigned char ready;     // 1 = non ancora letto
  unsigned char overrun;   // 1 = ne e' arrivato un altro prima della lettura
  unsigned char ie;        // 1 = la tastiera puo' interrompere (KBD_CTRL)
  KbdEvent trace[KBD_TRACE_MAX];   // eventi ordinati per istante (tick master)
  int      trace_len;
  int      trace_pos;
} VKbd;

// I comparatori. Nessun bit di pending: la richiesta e' la CONDIZIONE
// `(int32_t)(ms - val[n]) >= 0`, rivalutata a ogni confine d'istruzione.
typedef struct
{
  int32_t  val[CMP_CHANNELS];  // la scadenza, nella base del canale
  uint32_t armed;              // bitmask: bit n = canale n armato
  uint32_t cycles;             // bitmask: bit n = canale n confronta sui CICLI
} VCmp;

// L'ADC: i tre registri di configurazione, e l'acquisizione in corso -- che
// si CONGELA all'avvio. ADC_PERIOD e' in tick del clock PERIFERICHE (deciso
// il 29/09): l'ADC non sa quanto va veloce la CPU.
typedef struct
{
  int32_t  addr, count, period;
  int64_t  cur_addr;         // l'acquisizione in corso
  int32_t  cur_count;
  int32_t  cur_period;
  int32_t  i;                // il prossimo campione da prendere
  uint64_t next;             // l'istante in cui e' preso, in tick master
  unsigned char busy;
  unsigned char overrun;
  uint32_t done;             // i blocchi finiti
  double   freq;             // il mondo (--adc): Hz col segno
  double   amp;
  // Il mondo ESTERNO (--eco-cmd, §3.84): il segnale all'antenna per il blocco
  // in corso, I e Q alternati, che la scheda chiede al mondo all'avvio. Un
  // blocco non e' piu' grande della RAM condivisa, quindi l'array e' fisso.
  unsigned char eco_on;
  double   eco[2 * (SHARED_SIZE / 8)];
} VAdc;

// Il trasmettitore. Si ricorda gli ULTIMI DUE TX, non uno: il tempo di una
// store e' il confine d'istruzione PIU' il costo (vcpu_step conta i cicli
// prima di eseguire), quindi un campione preso fra il confine e l'avvio arriva
// alla DMA DOPO l'avvio -- e puo' cadere nella coda del TX di prima. Un'altra
// store non puo' esserci in mezzo, quindi due bastano.
typedef struct
{
  int32_t  len;              // TX_LEN, in tick periferiche
  uint64_t start[2], end[2]; // [0] l'ultimo TX, [1] quello prima: tick master
  uint32_t n;                // i TX partiti dall'accensione
  uint64_t tsw;              // la commutazione T/R, in tick master
  unsigned char neg;         // il segno Barker dell'ultimo TX
  unsigned char blind;       // un campione e' uscito accecato
  unsigned char overrun;     // un avvio col TX acceso
} VTx;

// La mailbox di UNA CPU: la sua FIFO d'ingresso, e i suoi due bit. Chi
// spedisce scrive nella FIFO dell'altra; l'overrun e' di chi ha spedito.
typedef struct
{
  uint32_t fifo[MBOX_DEPTH];
  int      head;             // la prossima da leggere
  int      count;            // quante ce ne sono
  unsigned char overrun;     // una MIA spedizione e' andata persa
  unsigned char ie;          // la mia FIFO non vuota interrompe
} VMbox;

// Il distributore: per ogni linea condivisa, la CPU a cui va.
typedef struct
{
  int target[INTD_LINES];
} VIntd;

// Lo spinlock hardware: per ogni lock, chi lo tiene (id + 1), 0 = libero, e
// se la richiesta ricorsiva di chi lo tiene e' gia' stata detta.
typedef struct
{
  int owner[HWLOCK_COUNT];
  unsigned char detto[HWLOCK_COUNT];
} VHwLock;

// Il marcatore. La registrazione si accende da riga di comando; le sw dei tag
// costano i loro cicli comunque, ed e' voluto. Uno PER CPU (§3.79): il bus sa
// chi fa l'accesso, e ogni CPU ha la sua registrazione.
typedef struct
{
  Marca     marche[MARCHE_MAX];
  int       len;
  uint64_t  lost;             // oltre il tetto: DICHIARATE, non perse in silenzio
  int       on;               // 1 = annota (--marks)
  int32_t   pend[MARK_CHANNELS];    // il porto dati, per canale
  int32_t   armed[MARK_CHANNELS];   // ...e se e' stato scritto
  MarcaNome names[MARK_NAMES_MAX];
  int       names_len;
  int       names_lost;
} VMarker;

// ---------------------------------------------------------------------------
//  Il contratto (vedi in cima). `t` e' sempre in tick del clock master.
// ---------------------------------------------------------------------------

// Il marcatore: annota "all'istante t il canale C prende il valore V", col
// timbro della CPU che lo tocca.
void marker_mark(VMarker* mk, const VCpu* timbro, uint64_t t, int canale, int32_t valore);
int  marker_is_channel(int64_t addr);                 // un porto MARK_BASE..
int  marker_store(VMarker* mk, const VCpu* timbro, uint64_t t, int64_t addr, int32_t value);

int  kbd_load(VKbd* k, int64_t addr, int32_t* out);
int  kbd_store(VKbd* k, int64_t addr, int32_t value);
void kbd_advance(VKbd* k, uint64_t t, VMarker* mk, const VCpu* timbro);
int  kbd_irq(const VKbd* k);
int  kbd_set_trace(VKbd* k, const char* spec, char* err, size_t errsz);

int  cmp_load(const VCmp* c, int64_t addr, int32_t* out);
int  cmp_store(VCmp* c, int64_t addr, int32_t value);
// Il canale scaduto, o -1. Vuole ENTRAMBE le basi perche' ogni canale sceglie
// la sua (CMP_CYCLES): chi chiama non sa quale serve, il canale sì.
int  cmp_pending(const VCmp* c, int32_t ms, int32_t cycles);

// L'avvio di un'acquisizione e i campioni vogliono il CLOCK: il periodo e' in
// tick periferiche, e l'istante del campione diventa un tempo in secondi.
// La DMA scrive SOLO nella RAM CONDIVISA (29/09/2026): `shared` e' la sua
// base, cioe' l'indirizzo SHARED_BASE. Nessuna periferica scrive nella RAM
// privata di una CPU, e l'ADC non appartiene a nessuna: lo programma chi lo
// usa, e il blocco lo vede chiunque.
//
// Il campione chiede al TRASMETTITORE se e' accecato: e' il trasmettitore a
// sapere quando e' stato acceso, e il bit TX_BLIND e' suo.
struct VClock;
int  adc_load(VAdc* a, int64_t addr, int32_t* out);
int  adc_store(VAdc* a, int64_t addr, int32_t value, uint64_t t, const struct VClock* ck);
void adc_advance(VAdc* a, uint64_t t, const struct VClock* ck, uint8_t* shared, VTx* tx);
int  adc_set_signal(VAdc* a, const char* spec, char* err, size_t errsz);

// Il trasmettitore: `t` serve a sapere se e' acceso, il clock a convertire
// TX_LEN da tick periferiche a tick master.
int  tx_load(VTx* x, int64_t addr, uint64_t t, int32_t* out);
int  tx_store(VTx* x, int64_t addr, int32_t value, uint64_t t, const struct VClock* ck);
int  tx_blinds(const VTx* x, uint64_t ts);   // il campione preso a ts e' accecato

// La mailbox: `mine` e' quella della CPU che fa l'accesso, `peer` l'altra.
int  mbox_load(VMbox* mine, const VMbox* peer, int64_t addr, int32_t* out);
int  mbox_store(VMbox* mine, VMbox* peer, int64_t addr, int32_t value);
int  mbox_irq(const VMbox* mine);

// Il distributore: `ncpu` e' quante CPU ha la scheda, per rifiutare una
// destinazione che non esiste.
int  intd_load(const VIntd* d, int64_t addr, int32_t* out);
int  intd_store(VIntd* d, int ncpu, int64_t addr, int32_t value);

// Lo spinlock: `id` e' la CPU che fa l'accesso.
int  hwlock_load(VHwLock* h, int id, int64_t addr, int32_t* out);
int  hwlock_store(VHwLock* h, int id, int64_t addr, int32_t value);

#endif // DEVICES_H
