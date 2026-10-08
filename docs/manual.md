# Manuale del simulatore di CPU vettoriale (`vcpu_sim`)

Simulatore didattico di una piccola **CPU vettoriale** (ispirata a Cray e a
RISC-V "V"), con assembler testuale, interprete funzionale e modello di timing
per confrontare esecuzione scalare e vettoriale.

---

## Indice

1. [Panoramica e architettura](#1-panoramica-e-architettura)
2. [Il simulatore](#2-il-simulatore)
   - [2.1 Requisiti](#21-requisiti)
   - [2.2 Compilare il progetto](#22-compilare-il-progetto)
   - [2.3 Eseguire un programma](#23-eseguire-un-programma)
   - [2.4 Debug](#24-debug)
   - [2.5 Compilazione separata: assembler, linker e librerie](#25-compilazione-separata-assembler-linker-e-librerie)
3. [Modello della macchina](#3-modello-della-macchina)
4. [Il linguaggio assembly](#4-il-linguaggio-assembly)
   - [4.1 Sintassi](#41-sintassi)
   - [4.2 Direttive](#42-direttive)
   - [4.3 Manuale delle istruzioni](#43-manuale-delle-istruzioni)
5. [L'assembler](#5-lassembler)
6. [Il modello di timing](#6-il-modello-di-timing)
   - [6.1 La frequenza](#61-la-frequenza-e-perché-i-cicli-si-possono-leggere-come-tempo)
7. [Esempi completi](#7-esempi-completi)
8. [Tutorial: il tuo primo programma](#8-tutorial-il-tuo-primo-programma)

---

## 1. Panoramica e architettura

Il progetto si trova in `vcpu_sim/` ed è composto da:

| File | Ruolo |
|---|---|
| `include/vcpu.h`, `src/vcpu.c` | **il core**: ISA, registri, modello di timing, esecuzione di un'istruzione, presa di una trap, timer privato |
| `include/devices.h`, `src/devices.c` | **le periferiche**: la mappa MMIO (il «datasheet»), tastiera, comparatori, ADC, trasmettitore, marcatore |
| `include/machine.h`, `src/machine.c` | **la scheda**: il clock master coi suoi divisori, la memoria, il bus, il ciclo principale, l'arbitraggio delle interruzioni, il debugger |
| `src/assembler.c` | Assembler a due passi con symbol table |
| `include/toolchain.h` | Modello oggetti/eseguibili/archivi (`.vo`/`.vx`/`.va`) |
| `src/toolchain.c` | Formati, linker e loader della compilazione separata (§2.5) |
| `src/main.c` | Driver da riga di comando |
| `standalone/*.vasm`, `linked/**/*.vasm` | Programmi di esempio |
| `Makefile` | Build |

Flusso di esecuzione:

```
 sorgente .vasm ──▶ assembler (2 passi) ──▶ array di Instr ──▶ interprete ──▶ stampa + statistiche
                     symbol table                              registri + memoria + VL
```

**Core, periferiche e scheda sono separati dal 29/09/2026.** Prima la
struttura della CPU conteneva tutto: memoria, device e tempo, che era il
conto dei cicli della CPU. Adesso la scheda (`VMachine`) possiede un clock
master (100 MHz) con un divisore intero per dominio — CPU, periferiche,
millisecondi — la memoria, i device e **due CPU** (§3.3). Ogni accesso di
una CPU alla memoria o ai device passa dal bus della scheda, e i device
ricevono il tempo come argomento, in tick master. Con i divisori a 1 e un
programma solo la macchina è identica al ciclo a quella di prima.

```
 VMachine ── clock master ─┬─ /1 CPU ─────────── VCpu 0, VCpu 1: registri, pc, psw, timer privato
                           ├─ /1 periferiche ─── ADC_PERIOD, TX_LEN
                           ├─ /1 (la radice) ─── CLOCK_CYCLES, comparatori su base CICLI
                           └─ /100000 ms ─────── CLOCK_MS,      comparatori su base MS
          ── bus ─┬─ RAM locale (1 MiB da 0, una per CPU)
                  ├─ MMIO: tastiera, ADC (DMA), TX, CLOCK_MS, CLOCK_CYCLES, della scheda
                  │        spinlock, distributore delle interruzioni
                  │        comparatori, mailbox, marcatore                 uno per CPU
                  └─ RAM condivisa (64 KiB a 0x200000)
```

---

## 2. Il simulatore

### 2.1 Requisiti

- Un compilatore C con supporto **gnu11** (GCC o Clang).
  Serve `gnu11` (non `c11`) perché l'assembler usa funzioni POSIX
  (`strtok_r`, `strdup`).
- `make` è opzionale: se non è installato si può compilare direttamente con GCC.

### 2.2 Compilare il progetto

**Con make:**

```bash
cd vcpu_sim
make            # produce build/vcpu_sim
```

**Senza make (GCC diretto):**

```bash
cd vcpu_sim
mkdir -p build
gcc -std=gnu11 -Wall -Wextra -O2 -Iinclude src/*.c -o build/vcpu_sim -lm
```

Target utili del `Makefile`:

| Comando | Effetto |
|---|---|
| `make` / `make all` | Compila `build/vcpu_sim` |
| `make run` | Compila ed esegue `standalone/saxpy.vasm` |
| `make clean` | Rimuove la cartella `build/` |

**Con CMake:** costruisce la stessa cosa *più* i programmi `.vasm` del repo, e
rende eseguibile la suite di regressione.

```bash
cmake -B out -S .      # configura (out/ e' ignorato da git)
cmake --build out -j   # vcpu_sim + tutti i .vo/.va/.vx del repo
ctest --test-dir out   # le invarianti di regressione, §4 di docs/stato-lavori.md
```

I due build **convivono** e non si pestano i piedi finché stanno in cartelle
diverse: il `Makefile` scrive in `build/`, CMake in quella passata a `-B` (nella
documentazione `out/`). Il `Makefile` resta la via breve quando serve solo
l'eseguibile C; CMake serve quando si vuole costruire o verificare anche il
software scritto in `.vasm`, perché lì i `.vinc` sono librerie di interfaccia e
le pipeline di `asm`/`ld` non sono più scritte a mano. I dettagli dei target
stanno in [`cmake/vasm.cmake`](../cmake/vasm.cmake).

### 2.3 Eseguire un programma

```bash
./build/vcpu_sim [--trace|--debug] [--kbd <ciclo:car,...>] [--adc <Hz>,<ampiezza> | --eco-cmd <comando>] <programma.vasm>
```

Senza flag esegue il programma normalmente. `--trace` e `--debug` sono descritti
in §2.4; `--kbd` alimenta la tastiera con una traccia a cicli ed è descritto in
§3.1; `--adc` dà all'ADC il suo segnale ed è descritto in §3.2, come
`--eco-cmd`, che al posto del segnale mette un mondo esterno.

C'è una quarta opzione, disponibile solo su `run`: **`--marche <file>`** scrive
la registrazione del *marcatore* — i tag che il programma piazza scrivendo nei
registri di `hal/marker.vinc`, più due canali che la macchina riempie da sola (chi
possiede la CPU, e quando arriva un tasto). Il catalogo dei nomi è `marks.conf`,
e `tools/marks.py leggi` traduce la registrazione in finestre e durate. Serve a
misurare il **tempo di risposta**, che la traccia del `pc` non può dare: una
finestra aperta in un task e chiusa in un altro attraversa le commutazioni.

Esempio:

```bash
./build/vcpu_sim standalone/saxpy.vasm
```

Output tipico:

```
mem[r6=0x28] = 12.5 25 37.5 50 62.5 75 87.5 100 112.5 125
---- stats ----
instructions executed : 17
vector element ops     : 40
cycles (timing model)  : 94
clock (Hz)             : 100000000
```

Le quattro statistiche finali:

| Statistica | Significato |
|---|---|
| `instructions executed` | numero totale di istruzioni eseguite |
| `vector element ops` | somma degli elementi processati da tutte le op vettoriali (misura del lavoro SIMD) |
| `cycles (timing model)` | cicli stimati dal modello di timing (vedi §6) |
| `clock (Hz)` | la frequenza della macchina, **dichiarata accanto ai cicli** perché è ciò che li rende leggibili come tempo (§6.1). Qui: 94 cicli = 0,94 µs |

**Due CPU, due programmi** (dal 29/09/2026, §3.3). `run` accetta un `.vx`
per CPU, in ordine:

```bash
./build/vcpu_sim run cpu0.vx cpu1.vx [--trace] [--marks rec.txt] ...
```

Con un solo programma la CPU 1 resta ferma e l'uscita è esattamente quella di
sempre. Con due, **ogni riga porta il prefisso della sua CPU** — `[cpu0]` e
`[cpu1]`, le righe dei `dumps`, di `--trace` e le statistiche, che escono una
volta per CPU — e le righe appaiono nell'ordine del tempo simulato. Con
`--marks` le registrazioni sono una per CPU, col nome della CPU prima
dell'estensione: `rec.cpu0.txt`, `rec.cpu1.txt`. `--debug` vuole un programma
solo. I messaggi d'errore su `stderr` il prefisso ancora non ce l'hanno.

**Codici di uscita:**

| Codice | Significato |
|---|---|
| `0` | esecuzione completata |
| `1` | errore di assemblaggio (messaggio su `stderr`) |
| `2` | uso errato (manca l'argomento file) |

### 2.4 Debug

Il simulatore offre quattro livelli di debug, dal più semplice al più potente.

#### a) Istruzioni di dump (debug dal programma)

Sono istruzioni che stampano lo stato **senza costare cicli**:

```asm
    dumps r3        ; stampa un registro scalare      -> "r3 = 10"
    dumpf f0        ; stampa un registro float          -> "f0 = 2.5"
    dumpv v1        ; stampa i primi VL elementi di v1  -> "v1 [VL=10] = ..."
    dumpm r6, 10    ; stampa 10 float dalla memoria     -> "mem[r6=0x28] = ..."
```

Inseriscile nel punto del programma che vuoi ispezionare. Sono statiche: per
cambiarle devi modificare il sorgente e rieseguire.

#### b) Trace (`--trace`)

Stampa ogni istruzione **man mano che viene eseguita**, con il `pc` e i cicli
accumulati fino a quel punto. Non richiede di modificare il sorgente:

```bash
./build/vcpu_sim --trace standalone/tutorial.vasm
```

```
[pc=  5 cyc=     5] vload v0, r1
[pc=  6 cyc=    22] vload v1, r2
[pc=  7 cyc=    39] vadd v2, v0, v1
[pc=  8 cyc=    50] vstore v2, r3
```

Ogni riga è ricostruita da un **disassemblatore** interno; l'incremento di
`cyc` tra due righe è il costo dell'istruzione secondo il modello di timing
(§6). Utile per capire dove si concentrano i cicli.

#### c) Debugger interattivo (`--debug`)

Avvia un debugger a riga di comando che si ferma sulle **righe del tuo
assembly** (non sul C del simulatore):

```bash
./build/vcpu_sim --debug standalone/saxpy.vasm
```

All'avvio l'esecuzione è ferma sulla prima istruzione e compare il prompt
`(vdb)`. Comandi disponibili:

| Comando | Azione |
|---|---|
| `s`, `step` | esegue una sola istruzione |
| `c`, `continue` | prosegue fino al prossimo breakpoint o a `halt` |
| `b [target]` | imposta un breakpoint su una **label** o un indice di istruzione; senza argomento elenca quelli attivi |
| `d <target>` | rimuove un breakpoint |
| `p <loc>` | stampa una locazione: `r0`..`r15`, `f0`..`f15`, `v0`..`v7`, `pc`, `vl`, `vmask`, `psw`, `epc` |
| `r`, `regs` | stampa tutti i registri scalari e `vl` |
| `m <addr> [count]` | stampa `count` float dalla memoria (indirizzo come label o numero) |
| `l`, `list` | mostra le istruzioni intorno al `pc`, con le label |
| `h`, `help` | elenco dei comandi |
| `q`, `quit` | termina la simulazione |

Esempio di sessione (breakpoint per nome di label):

```
(vdb) b loop
breakpoint at 4
(vdb) c

->   4  setvl r4, r3   ; loop
(vdb) p vl
vl = 0
(vdb) c
```

Le label vengono risolte automaticamente: `b loop` mette il breakpoint
sull'indice dell'istruzione etichettata `loop`.

#### d) Debug con GDB (debug del simulatore in C)

Ricompila con simboli e senza ottimizzazioni:

```bash
gcc -std=gnu11 -Wall -Wextra -g -O0 -Iinclude src/*.c -o build/vcpu_sim_dbg -lm
gdb --args build/vcpu_sim_dbg standalone/saxpy.vasm
```

Breakpoint utili:

```gdb
(gdb) break vcpu_run          # entra nel loop di esecuzione
(gdb) break encode            # ispeziona l'assemblaggio di un'istruzione
(gdb) run
(gdb) print cpu->r            # registri scalari
(gdb) print cpu->vl           # vector length corrente
(gdb) print prog[cpu->pc]     # istruzione corrente
```

#### e) Errori comuni e messaggi

L'assembler segnala gli errori con la forma `instr N: <messaggio>`:

| Messaggio | Causa |
|---|---|
| `unknown mnemonic 'xyz'` | istruzione non riconosciuta |
| `expected r-register, got '...'` | prefisso di registro errato (`r`/`f`/`v`) |
| `register '...' out of range` | indice registro fuori intervallo |
| `'op' expects N operand(s), got M` | numero di operandi sbagliato |
| `unknown symbol '...'` | label/simbolo non definito |
| `duplicate label '...'` | label definita due volte |
| `line N: data overflow` | i dati superano la memoria |

Errori a runtime (su `stderr`):

| Messaggio | Causa |
|---|---|
| `float load/store out of bounds` | accesso in memoria fuori dai 1 MiB |

---

## 2.5 Compilazione separata: assembler, linker e librerie

Oltre al percorso classico "un file `.vasm` → esecuzione" (§2.3), il simulatore
offre una vera **toolchain multi-file**: puoi assemblare più moduli
separatamente, raccoglierli in **librerie** e **linkarli** in un eseguibile.
È lo stesso schema di `gcc`/`ld`, in miniatura e a scopo didattico.

### La pipeline

```
 sorgente .vasm ──asm──▶ oggetto .vo ──┐
 sorgente .vasm ──asm──▶ oggetto .vo ──┼─ld──▶ eseguibile .vx ──run──▶ esecuzione
 libreria .va (oggetti) ───────────────┘
```

Tutti i formati intermedi (`.vo`, `.vx`, `.va`) sono in **testo** e si possono
ispezionare con `cat`. I dettagli del formato sono in
[`docs/toolchain-spec.md`](toolchain-spec.md); qui vediamo come si usano.

### I sottocomandi

| Comando | Effetto |
|---|---|
| `vcpu_sim asm <in.vasm> -o <out.vo> [-I <dir>]... [-D <nome>]...` | assembla un modulo in un **oggetto rilocabile**; `-I` aggiunge una cartella alla ricerca di `.include` (§4.2.2), `-D` definisce un nome per `.ifdef` (§4.2.3) |
| `vcpu_sim ar <lib.va> <o1.vo> ...` | raccoglie oggetti in una **libreria** |
| `vcpu_sim ld <a.vo\|lib.va> ... [-e <sym>] [-T <board.vld>] [-p <cpu>] [-M <out.map>] -o <out.vx>` | **linka** oggetti e librerie in un eseguibile. `-T` è il **file di collocazione** (le regioni di memoria e dove va ogni sezione), `-p` il blocco `PROCESSOR` da usare, `-M` scrive la **mappa** |
| `vcpu_sim ld --verbose` | stampa il file di collocazione **incorporato**, cioè quello che `ld` usa senza `-T` |
| `vcpu_sim run <prog.vx> [<prog_cpu1.vx>] [--trace\|--debug]` | **carica ed esegue** un eseguibile (uno per CPU) |
| `vcpu_sim nm [-n\|-p] [-r] <file.vo\|file.vx>` | elenca i **simboli** di un oggetto o eseguibile: `T` codice, `D` dati, `S` RAM condivisa, `U` indefinito (maiuscolo = globale) |

> **Compatibilità:** il percorso classico `vcpu_sim <programma.vasm>` (§2.3) resta
> invariato e continua a fare assemblaggio + esecuzione in memoria. La toolchain
> è un'aggiunta, non lo sostituisce.

### Simboli globali, esterni e rilocazioni

Assemblando un solo file, l'assembler risolve subito ogni riferimento a una
label (indirizzo o indice noto). Con più moduli questo non è più possibile: un
modulo può riferirsi a un simbolo definito **altrove**, e al momento
dell'assemblaggio quel valore non esiste ancora.

Per questo l'oggetto `.vo` non contiene indirizzi già risolti ma delle
**rilocazioni**: annotazioni del tipo *"qui va messo il valore del simbolo
`saxpy`"*. È il **linker** che, mettendo insieme i moduli, assegna gli indirizzi
finali e completa le rilocazioni.

Due direttive controllano la visibilità dei simboli:

- `.global sym` — **esporta** un simbolo definito in questo modulo, rendendolo
  visibile agli altri.
- `.extern sym` — dichiara che `sym` è **definito altrove**; questo modulo lo
  usa e basta. Se nessun modulo lo esporta, il linker segnala
  `undefined reference to 'sym'`.

I simboli senza direttiva sono **locali** al modulo.

### Esempio a due moduli

Il modulo `main` prepara i dati e salta nella routine `saxpy`, definita in un
secondo modulo (vedi `linked/multi/`):

`linked/multi/main.vasm`
```asm
.data
.global x
.global y
x:  .float 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
y:  .float 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
.text
.extern saxpy
.global main
main:
  li    r1, x        ; riferimento a un dato (rilocazione)
  li    r2, y
  li    r3, 10
  fli   f0, 2.5
  j     saxpy        ; riferimento a codice esterno (rilocazione)
```

`linked/multi/saxpy.vasm`
```asm
.text
.global saxpy
.extern y
saxpy:
loop:   setvl r4, r3
  vload v0, r1
  vload v1, r2
  vmacc v1, f0, v0
  vstore v1, r2
  slli  r5, r4, 2
  add   r1, r1, r5
  add   r2, r2, r5
  sub   r3, r3, r4
  bne   r3, r0, loop
  li    r6, y        ; usa 'y' importato
  dumpm r6, 10
  halt
```

Assembla, linka ed esegui:

```bash
./build/vcpu_sim asm linked/multi/main.vasm  -o build/main.vo
./build/vcpu_sim asm linked/multi/saxpy.vasm -o build/saxpy.vo
./build/vcpu_sim ld  build/main.vo build/saxpy.vo -o build/prog.vx
./build/vcpu_sim run build/prog.vx
```

```
mem[r6=0x28] = 12.5 25 37.5 50 62.5 75 87.5 100 112.5 125
```

L'**entry point** è il simbolo globale `main` (se assente, il linker parte
dall'istruzione 0 con un avviso). Con l'opzione `-e <sym>` puoi scegliere un
entry point diverso (deve essere un simbolo di **codice globale**, altrimenti il
link fallisce con errore):

```bash
./build/vcpu_sim ld build/main.vo build/saxpy.vo -e saxpy -o build/prog.vx
```

### Ispezionare i simboli con `nm`

```bash
./build/vcpu_sim nm build/main.vo
```

```
       0 T main
         U saxpy
       0 D x
      40 D y
```

Le lettere seguono la convenzione di `nm`: **maiuscola = globale**,
`T`/`t` = codice, `D`/`d` = dati, `U` = simbolo indefinito (da importare).
Su un `.vx` linkato, `nm` mostra l'entry point e i simboli globali con il loro
**indirizzo finale**.

I simboli sono ordinati **alfabeticamente per nome** (come `nm` di Unix). Le
opzioni cambiano l'ordinamento: `-n` ordina per **valore** (indirizzo/indice),
`-p` **preserva** l'ordine della tabella dei simboli, `-r` **inverte** l'ordine.

### Librerie e inclusione selettiva

Una **libreria** `.va` è un semplice bundle di oggetti. Si crea con `ar`:

```bash
./build/vcpu_sim ar build/libm.va build/saxpy.vo build/unused.vo
```

Quando linki contro una libreria, il linker **non** include tutti i suoi membri:
tira dentro **solo gli oggetti che servono** a risolvere simboli ancora
indefiniti (e ripete finché ce ne sono, quindi anche in modo **transitivo**). Un
oggetto della libreria che nessuno referenzia resta fuori dall'eseguibile:

```bash
./build/vcpu_sim ld build/main.vo build/libm.va -o build/prog.vx
./build/vcpu_sim nm build/prog.vx        # 'dead' di unused.vo NON compare
```

Gli oggetti passati **esplicitamente** a `ld` sono invece sempre inclusi.

### Debug di un eseguibile

`run` supporta `--trace` e `--debug` come il percorso classico. Poiché il `.vx`
porta con sé la mappa dei simboli globali, nel debugger puoi mettere breakpoint
**per nome** anche attraverso i moduli:

```bash
./build/vcpu_sim run build/prog.vx --debug
```
```
(vdb) b saxpy
breakpoint at 5
(vdb) c
```

---

## 3. Modello della macchina

| Risorsa | Quantità | Note |
|---|---|---|
| Registri scalari interi | `r0`..`r15` | interi a 64 bit; **`r0` è cablato a 0** (le scritture sono ignorate) |
| Registri scalari float | `f0`..`f15` | `float` a **32 bit**, come la memoria e i registri vettoriali: ogni operazione arrotonda al suo risultato, e `fsw`/`flw` non perdono niente. Fino al 28/09/2026 erano a doppia precisione, e il salvataggio del contesto (che usa `fsw`) arrotondava i float di un task interrotto: il risultato di un calcolo dipendeva da dove cadevano gli interrupt |
| Registri vettoriali | `v0`..`v7` | ciascuno contiene fino a `VLMAX = 64` elementi `float` (32 bit) |
| Vector Length (`VL`) | 1 registro | numero di elementi processati dalle op vettoriali; impostato con `setvl` |
| Registro di maschera (`vmask`) | 1 registro | 64 bit, un bit per corsia; scritto dai confronti `vms*` e usato da `vmerge` |
| Parola di stato (`psw`) | 1 registro | strato di controllo scalare; bit 0 = `IE` (interrupt enable) |
| Registri di trap | `epc`, `epsw` | copie ombra di `pc` e `psw` salvate all'interruzione, ripristinate da `reti` |
| Memoria | 1 MiB | **indirizzabile a byte**; elemento = 4 byte (float32) |
| Device (MMIO) | `0x100000`+ | registri di periferica, **sopra** la RAM: vedi §3.1 |

**Dettagli importanti:**

- Le operazioni vettoriali processano esattamente **`VL` elementi**. Devi
  impostare `VL` con `setvl` **prima** di usarle.
- La dimensione dell'elemento è fissa a **4 byte**: per avanzare un puntatore di
  `VL` elementi servono `VL * 4` byte (vedi `slli rX, rVL, 2`).
- `setvl` satura a `VLMAX`: questo abilita lo **strip-mining** (elaborazione a
  blocchi di vettori più lunghi di `VLMAX`).
- **L'indirizzo 0 è riservato: nessun dato ci viene mai collocato.** Il segmento
  dati parte da 4, sia nel percorso a file singolo sia nel linker (`NULL_GUARD`
  in [`include/vcpu.h`](../include/vcpu.h)), così **0 è un puntatore nullo** che
  non può coincidere con nessun oggetto reale. Serve a tutto il codice che usa
  0 come «niente»: `dequeue_head` restituisce 0 per coda vuota, `current == 0`
  significa «nessun task in esecuzione», e le liste del kernel riconoscono un
  nodo fuori da ogni coda dai link nulli. Conseguenza pratica: la prima
  etichetta dichiarata in `.data` vale 4, non 0.

### 3.1 Device in memoria (MMIO) e la tastiera

Dall'11/09/2026 la macchina ha un **ingresso**. I registri di periferica si
leggono con una `lw` normale, come su qualunque macchina vera: **non c'è nessuna
istruzione nuova**, e l'ISA resta quella che era.

L'intervallo comincia a `0x100000`, cioè **subito dopo l'ultimo byte di RAM**, e
non dentro: così non sottrae memoria ai programmi, nessun linker deve sapere di
doverlo evitare, e una collisione fra un dato e un registro di periferica è
impossibile invece che improbabile. L'errore *out of bounds* resta per gli
indirizzi che non sono né RAM né device.

| Registro | Indirizzo | Accesso | Significato |
|---|---|---|---|
| `KBD_STATUS` | `0x100000` | lettura | bit 0 `KBD_READY` = c'è un carattere; bit 1 `KBD_OVERRUN` = ne è arrivato un altro prima che leggessi |
| `KBD_DATA` | `0x100004` | lettura | il carattere — **e la lettura abbassa il flag** |
| `KBD_CTRL` | `0x100008` | lettura/scrittura | bit 0 `KBD_IE` = l'interrupt della tastiera è armato. **Nasce spento** |

Che leggere `KBD_DATA` abbia un **effetto** è la differenza fra memoria e MMIO,
e il protocollo sta tutto lì: non serve nessun handshake, e un carattere perso
non sparisce in silenzio perché `KBD_OVERRUN` lo dice. È il modello di ogni
UART. Le costanti stanno in
[`hal/interface/hal/kbd.vinc`](../hal/interface/hal/kbd.vinc), e il polling si
scrive con le istruzioni che già ci sono — quello che il PDP-8 chiamava *skip on
flag*:

```asm
  li   r1, KBD_STATUS
  lw   r2, 0(r1)
  beq  r2, r0, niente        ; il flag non è alzato
  li   r1, KBD_DATA
  lw   r3, 0(r1)             ; r3 = il carattere, e il flag si abbassa qui
```

**Dal 14/09/2026 il polling non è più l'unica strada**: la tastiera può
**interrompere**, e si arma scrivendo `KBD_IE` in `KBD_CTRL`. Nasce spenta, e
non è prudenza — un programma che la interroga senza installare un vettore
(`test_events` fa esattamente così) finirebbe a saltare su un gestore che non
c'è. Chi la vuole la arma, come si arma il timer con `settimer`.

L'interrupt è **a livello** e non a fronte: la condizione è il flag `ready`, che
si abbassa leggendo `KBD_DATA`. Ne seguono due cose che vanno sapute. Un
carattere arrivato mentre `IE` è 0 **non si perde**, perché la trap scatta appena
gli interrupt riaprono — non serve nessun bit di *pending*. E un'ISR che torna
**senza aver letto** `KBD_DATA` si ritrova la trap subito: il flag lo abbassa la
lettura, non la trap.

Con due sorgenti il gestore scopre chi è stato con [`mfcause`](#43-manuale-delle-istruzioni).
`tests/test_due_irq.vasm` le esercita entrambe, ed è anche il primo programma
del progetto la cui ISR torna con una **`reti` secca**, senza commutare niente.

**Chi alimenta il device** è separato dal device stesso. Oggi c'è un
alimentatore solo, una traccia a cicli passata al simulatore:

```bash
vcpu_sim run prog.vx --kbd "1200:a,3000:b,3000:c"
```

cioè *«al ciclo 1200 arriva `a`, al 3000 arrivano `b` e `c`»* — i cicli devono
essere non decrescenti, e due eventi nello stesso ciclo producono un overrun.
La traccia vive nel **tempo simulato**, quindi rigiocarla dà sempre gli stessi
numeri: è il motivo per cui un programma che dipende dal mondo esterno può stare
in `ctest`, e `tests/test_kbd.vasm` ci sta. Un secondo alimentatore che legga
`stdin` da un thread vivrebbe nel tempo di parete e non sarebbe riproducibile —
ma il programma vedrebbe gli stessi due registri, senza cambiare una riga.

### 3.2 L'ADC: 12 bit, I e Q, a blocchi

Dal 28/09/2026 la macchina ha il front-end di un radar: un convertitore che
scrive in RAM, **con la DMA e senza la CPU**, un blocco di N campioni
**complessi**. La CPU vede il blocco, non il campione.

| Registro | Indirizzo | Accesso | Significato |
|---|---|---|---|
| `ADC_ADDR` | `0x100040` | lettura/scrittura | dove scrivere il blocco: **solo nella RAM condivisa** (§3.3) |
| `ADC_COUNT` | `0x100044` | lettura/scrittura | N, i campioni complessi del blocco |
| `ADC_PERIOD` | `0x100048` | lettura/scrittura | i tick del clock periferiche fra un campione e il successivo (oggi divisore 1: cicli) |
| `ADC_CTRL` | `0x10004C` | scrittura | bit 0 `ADC_START` = avvia l'acquisizione |
| `ADC_STATUS` | `0x100050` | lettura | bit 0 `ADC_BUSY`; bit 1 `ADC_OVERRUN`; bit 2..31 i blocchi finiti dall'accensione — **e la lettura abbassa l'overrun** |

**L'acquisizione la avvia il programma**, non gira da sola: una scrittura ad
`ADC_CTRL` dice *«acquisisci `ADC_COUNT` campioni a `ADC_ADDR`, uno ogni
`ADC_PERIOD` tick»*, ed è lo sweep comandato con la finestra di ricezione
aperta. Un ADC libero col suo clock scivolerebbe rispetto alla time line. Il
ping-pong lo fa il programma: avvia il blocco B mentre elabora A.

**Il formato: I e Q alternati, una parola a 32 bit ciascuno**, a `ADC_ADDR + 8i`
e `ADC_ADDR + 8i + 4`, col segno già esteso e i valori in `[-2048, 2047]`; fuori
scala **satura**. Un ADC vero spesso impacchetta I e Q in 16+16 bit, e
spacchettarli vorrebbe uno shift con segno: `srai` c'è dal 29/09/2026, ma
allargare nella DMA resta la semplificazione dichiarata — la larghezza la decide
il ferro, non chi legge. Per portarli in float c'è
[`vcvt`](#43-manuale-delle-istruzioni), e un canale si carica con un `vload` a
passo 8.

**Il blocco si riempie nel tempo.** Il campione i è preso all'istante
`avvio + (i+1)·ADC_PERIOD` e arriva in RAM al primo confine d'istruzione dopo.
Leggere il blocco prima che sia finito dà un blocco **a metà**, e per questo lo
stato ha un **contatore** dei blocchi finiti invece di un solo bit: chi prende
un blocco verifica che sia finito quello che aspetta. La DMA non ruba cicli
alla CPU.

**Nessun interrupt**: a dire quando è il momento è la time line, cioè il
comparatore. **Un avvio mentre acquisisce è ignorato** e alza `ADC_OVERRUN`: è
un errore di tempo che il programma deve vedere, non un errore del simulatore.
Una configurazione impossibile (N o periodo non positivi, un blocco che esce
dalla RAM) invece è un errore di costruzione, e il simulatore lo dice.

**Il segnale è il mondo**, e si dà al simulatore come la traccia della tastiera:

```bash
vcpu_sim run prog.vx --adc 250000,1000
```

cioè un esponenziale complesso di **250 kHz** e ampiezza **1000**:
`I = A·cos(2πft)`, `Q = A·sin(2πft)`, con `t` il tempo **assoluto** del
campione (il mondo non riparte quando la CPU avvia l'ADC). La frequenza ha un
segno, e con I e Q si vede: una frequenza negativa fa girare il fasore al
contrario. Senza `--adc` si campiona zero. `tests/test_adc.vasm` gira tre volte,
con tre segnali.

I registri e i bit si prendono da `hal/adc.vinc` (dal 29/09/2026, in
`vinc_hal`); `test_adc` se li ricopia perché gira a file singolo. Il primo
consumatore del blocco è la FFT di §7.11.

**La DMA scrive solo nella RAM condivisa** (dal 29/09/2026, §3.3): `ADC_ADDR`
deve puntare fra `0x200000` e `0x20FFFF`, e un blocco che non ci sta tutto è
una configurazione impossibile. Nessuna periferica scrive nella RAM privata di
una CPU, e l'ADC non appartiene a nessuna: lo programma chi lo usa, e il blocco
lo vede chiunque.

#### Il trasmettitore, e l'accecamento del ricevitore

Dal 07/10/2026 l'altimetro ha anche la metà che parla. Il chirp vero non si
modella: il TX è un impulso lungo `TX_LEN`, e l'eco lo produrrà il mondo,
fuori dalla macchina. Qui c'è solo ciò che è del ferro: quando il TX è acceso,
e cosa fa al ricevitore.

| Registro | Indirizzo | Accesso | Significato |
|---|---|---|---|
| `TX_LEN` | `0x100054` | lettura/scrittura | la durata del TX in tick del clock periferiche (oggi cicli: 5000 = 50 µs) |
| `TX_CTRL` | `0x100058` | scrittura | bit 0 `TX_START` = trasmetti; bit 1 `TX_NEG` = il segno del codice di Barker di questo TX |
| `TX_STATUS` | `0x10005C` | lettura | bit 0 `TX_ON`; bit 1 `TX_BLIND`; bit 2 `TX_OVERRUN` — **e la lettura abbassa BLIND e OVERRUN** |

**La commutazione T/R è della scheda, 10 µs.** Dal trasmettere al ricevere non
si passa all'istante, e il tempo del commutatore è del ferro: non è un
registro. La durata del TX invece lo è, perché il chirp lo progetta il
programma.

**L'accecamento.** Un campione preso col TX acceso, o entro 10 µs dalla sua
fine, esce **saturato**, 2047 su I e su Q, e alza `TX_BLIND`. Il bit sta nel
trasmettitore e non in `ADC_STATUS`, dove dal bit 2 in su c'è il contatore dei
blocchi. Saturato e non zero, perché zero è un campione plausibile.

**Un avvio col TX acceso è ignorato** e alza `TX_OVERRUN`, come l'ADC. Un
`TX_LEN` non positivo è un errore di costruzione, e il simulatore lo dice.
Nessun interrupt: chi trasmette è la time line, e sa quando.

`TX_NEG` oggi la macchina lo conserva e basta: è per il mondo esterno, che lo
userà per il segno dell'eco. `tests/test_tx.vasm` prova lo stato, l'overrun e
l'accecamento, compresa la commutazione. I registri non sono ancora in
`hal/`: ci andranno col primo programma che li usa con le interfacce.

#### Il mondo esterno: l'eco da un altro processo

Dal 07/10/2026 il segnale all'antenna può venire da **un'applicazione
separata**, al posto della sinusoide di `--adc` (i due insieme sono un errore):

```bash
vcpu_sim --eco-cmd "python3 tools/mare.py tests/eco_piatto.scena" tests/test_eco.vasm
```

Il simulatore lancia il comando con `/bin/sh -c` e ci parla su stdin/stdout, in
**lockstep**: domanda, aspetta la risposta, va avanti. Il tempo resta del
simulatore, e il mondo è una funzione pura delle domande, quindi la corsa è
riproducibile come con `--kbd`. Il protocollo, con gli istanti in tick del
clock master:

| Riga | Quando | Risposta |
|---|---|---|
| `CLOCK <hz>` | una volta, all'avvio | nessuna |
| `TX <istante> <segno>` | a ogni TX, segno `+1` o `-1` (`TX_NEG`) | nessuna |
| `BLOCCO <istante_1> <periodo> <n>` | all'avvio di un blocco dell'ADC | `n` righe `I Q` |

I e Q tornano **reali**: arrotondare e saturare è dell'ADC. I campioni accecati
dal trasmettitore escono saturati comunque. Un mondo che muore, o risponde
male, è un **guasto del simulatore**: lo dice, le CPU si fermano e l'uscita è 1.

Il mondo che c'è è [`tools/mare.py`](../tools/mare.py), l'eco dell'acqua per un
altimetro: il ritardo di ogni TX, un fronte (`erf`) e una coda esponenziale, la
somma su tutti i TX, il clutter, le perdite e il rumore seminato. La scena sta
in un file, e il modello è descritto in testa al sorgente.

### 3.3 Due CPU: RAM condivisa, spinlock e mailbox

Dal 29/09/2026 la scheda ha **due CPU**, asimmetriche: ognuna col suo
programma (`run cpu0.vx cpu1.vx`, §2.3) e il suo kernel, se ne ha uno. Con un
programma solo la CPU 1 resta ferma e la macchina è identica a quella di una
CPU.

**La mappa.** Ogni CPU ha la sua **RAM locale** di 1 MiB **da 0**, allo stesso
indirizzo: sono fisicamente due, e ognuna vede la propria, quindi un programma
si linka uguale per l'una e per l'altra. Le CPU si incontrano nella **RAM
condivisa**, 64 KiB a `0x200000`, sopra i device.

| Registro | Indirizzo | Accesso | Significato |
|---|---|---|---|
| `MBOX_SEND` | `0x100060` | scrittura | una parola all'**altra** CPU; a FIFO piena è ignorata e alza `MBOX_OVERRUN` |
| `MBOX_RECV` | `0x100064` | lettura | la prima parola della **propria** FIFO, e la toglie |
| `MBOX_STATUS` | `0x100068` | lettura | bit 0..7 le parole nella propria FIFO; bit 8 `MBOX_PEER_FULL`; bit 9 `MBOX_OVERRUN` — **la lettura lo abbassa** |
| `MBOX_CTRL` | `0x10006C` | lettura/scrittura | bit 0 `MBOX_IE`: la propria FIFO non vuota interrompe (causa 4) |
| `HWLOCK_BASE + 4n` | `0x100080`.. | lettura/scrittura | lo spinlock `n` (0..7): **leggere** è il test-and-set (0 = era libero, ora è tuo; 1 = occupato), **scrivere 0** lo rilascia |

**Tutti i device sono sul bus e visibili da tutte le CPU**, come in un SoC.
Alcuni esistono una volta **per CPU** agli stessi indirizzi, e ognuna vede il
suo: i **comparatori** (lo schema del generic timer ARM: un orologio di sistema
comune, `CLOCK_MS`, e un comparatore per core), la **mailbox**, il **timer** di
`settimer` (che è del core) e il **marcatore**, che fa una registrazione per
CPU. Tastiera e ADC sono uno solo. **L'ADC scrive solo nella RAM condivisa**,
quindi non appartiene a nessuna CPU: lo programma chi lo usa, e siccome non ha
una linea di interruzione (si interroga il contatore) non serve altro per
darlo alla CPU 1. **Un device, un proprietario** è una regola del
programmatore, non della macchina: due CPU che leggono tutte e due `KBD_DATA`
si rubano i caratteri, come su un chip vero.

**Il distributore delle interruzioni** manda la linea di una periferica
condivisa alla CPU scritta nel suo registro di destinazione, sul modello del
distributore del GIC ARM. All'accensione tutte vanno alla CPU 0. Oggi la linea
condivisa è una, la tastiera:

| Registro | Indirizzo | Accesso | Significato |
|---|---|---|---|
| `INTD_TARGET + 4n` | `0x1000A0`.. | lettura/scrittura | la CPU a cui va la linea `n`; `INTD_KBD = 0` è la tastiera. Una CPU che non esiste è un errore |

È minimo di proposito: l'abilitazione resta nel device (`KBD_CTRL`) e le
priorità restano l'ordine fisso dell'arbitraggio. Comparatori, timer e mailbox
non passano di lì, perché sono già per CPU. La marca del tasto va nella
registrazione della CPU a cui la linea è mandata.

**Lo spinlock hardware** mette l'atomicità nella periferica, non nell'ISA: in
un'AMP di core eterogenei le istruzioni esclusive spesso non valgono fra core
diversi, ed è per questo che i SoC hanno un blocco di spinlock. Il device
ricorda chi tiene un lock: rilasciarne uno altrui è un errore, e il simulatore
lo dice. **Non è ricorsivo**: chi lo tiene e lo rilegge lo trova occupato, e
il simulatore avvisa una volta, perché girare ad aspettarlo vorrebbe dire
aspettare se stessi per sempre. **Chi prende uno spinlock chiude prima gli interrupt
sulla propria CPU**, altrimenti un'ISR sulla stessa CPU che cerca lo stesso lock
gira per sempre.

```asm
spin:
  lw   r9, 0(r7)          ; r7 = HWLOCK_BASE + 4n: test-and-set
  bne  r9, r0, spin       ; 1 = occupato
  ...                     ; la sezione, corta
  sw   r0, 0(r7)          ; rilascio
```

**La mailbox** porta una parola e conta: dice *quale* messaggio, e due avvisi
non si fondono in uno. L'interrupt è a livello, come quello della tastiera.

**Il tempo.** Il simulatore fa avanzare sempre la CPU più indietro, e a pari
istante la CPU 0: è un arbitro a priorità fissa, e rende ogni corsa
deterministica.

**Dichiarato, e su un chip vero sono i punti dolenti:**

- **niente cache**, quindi niente coerenza da gestire: una scrittura in RAM
  condivisa è subito visibile all'altra CPU;
- **ordine sequenziale**: le scritture arrivano nell'ordine del programma, e
  non servono barriere;
- **nessuna contesa sul bus**: due CPU che accedono insieme non si rallentano;
- un'istruzione si esegue tutta quando parte, quindi l'altra CPU ne vede gli
  effetti fino a una durata d'istruzione prima del vero.

Il codice scritto qui è corretto sulla macchina che garantisce tutte e quattro
queste cose. Le costanti per i programmi stanno in `hal/amp.vinc`, e
`tests/test_amp0.vasm` con `tests/test_amp1.vasm` sono l'esempio: un contatore
in RAM condivisa sotto spinlock, e una raffica di messaggi oltre la profondità
della FIFO. `tests/test_intd0.vasm` con `tests/test_intd1.vasm` mandano la
tastiera alla CPU 1 attraverso il distributore.

---

## 4. Il linguaggio assembly

### 4.1 Sintassi

- **Un'istruzione per riga.**
- **Commenti:** tutto ciò che segue `;` o `#` è ignorato.
- **Operandi** separati da spazi e/o virgole (equivalenti).
- **Label:** un identificatore seguito da `:`. Può stare da solo su una riga
  oppure precedere un'istruzione/direttiva sulla stessa riga.
  - Una label nella sezione **codice** vale l'**indice dell'istruzione**
    successiva (target di salto).
  - Una label nella sezione **dati** vale l'**indirizzo di memoria** corrente.
- **Immediati interi:** decimali (`10`), esadecimali (`0x2a`) oppure il **nome
  di un simbolo** (risolto al suo valore, es. l'indirizzo di un array).
- **Immediati float:** notazione decimale (`2.5`).

Esempio:

```asm
loop:   setvl r4, r3      ; 'loop' = indice di questa istruzione
  vload v0, r1      ; commento
```

### 4.2 Direttive

| Direttiva | Sintassi | Effetto |
|---|---|---|
| `.text` | `.text` | passa alla sezione codice (default) |
| `.data` | `.data` | passa alla sezione dati |
| `.shared` | `.shared` | passa alla sezione della **RAM condivisa** (più CPU). Si **riserva** e non si inizializza — `.word`/`.float` lì dentro sono un errore — e i suoi simboli sono **globali per definizione**, senza `.global`. Vuole il linker: in un file singolo è un errore, perché l'indirizzo lo decide il file di collocazione (§4.2.7) |
| `.float` | `.float a, b, c, ...` | scrive i valori come float (4 byte l'uno) e avanza il puntatore dati |
| `.word` | `.word a, b, c, ...` | scrive interi con segno a 32 bit (4 byte l'uno); accetta decimale o esadecimale (`0x...`) |
| `.space` | `.space N` | riserva `N` elementi (`N*4` byte) senza inizializzarli |
| `.equ` | `.equ NOME valore` | definisce una **costante** di compile-time (`.set` è sinonimo); il valore è un intero o una costante già definita |
| `.struct` | `.struct NOME` … `.ends` | apre/chiude un blocco struttura: gli offset dei campi diventano costanti `NOME.campo` |
| `.field` | `.field campo [dim]` | dentro `.struct`: definisce `NOME.campo` = offset corrente e avanza di `dim` byte (default 4) |
| `.res` | `etichetta: .res TIPO` | nel segmento dati: riserva `TIPO.size` byte (come una `.space` *type-aware*); l'etichetta ne è l'indirizzo |
| `.include` | `.include "file"` | inserisce testualmente `file` a quel punto, **una volta sola** (§4.2.2); il nome si cerca nella **cartella del file di primo livello** e poi nelle cartelle passate con `-I` (o si usa così com'è, se assoluto). Utile per condividere `.struct`/`.equ` fra più sorgenti |
| `.ifdef` / `.ifndef` | `.ifdef NOME` … `.endif` | assembla le righe che seguono **solo se** `NOME` è (o non è) fra i nomi passati con `-D` sulla riga di comando (§4.2.3) |
| `.else` | `.else` | l'altro ramo del `.ifdef`/`.ifndef` aperto |
| `.endif` | `.endif` | chiude il condizionale aperto più di recente |
| `.proc` / `.endproc` | `.proc NOME` … `.endproc NOME` | prologo/epilogo automatico per procedure non-foglia a corpo lineare (§4.2.1) |
| `.macro` / `.endmacro` | `.macro NOME p1, p2` … `.endmacro` | una sequenza definita una volta ed **espansa** dove la si nomina; il corpo può essere **vuoto**, e allora non emette niente (§4.2.4) |
| `.supertask` | `.supertask NOME` | nel segmento dati: il TCB del **super task**, come `NOME: .res TCB` ma con `TCB.nopreempt` già a 1 e il globale `super_tcb` allo stesso indirizzo; due super task sono un errore di build (§4.2.6) |
| `.interrupt` / `.endinterrupt` | `.interrupt NOME` … `.endinterrupt NOME rN` | un gestore di trap che esce dal kernel: la coda salta a `sched_isr_exit_to` col TCB di `rN` da mettere in esecuzione direttamente, `r0` = nessuno (§4.2.5) |
| `.global` | `.global sym ...` | **esporta** un simbolo definito qui (compilazione separata, §2.5) |
| `.extern` | `.extern sym ...` | **importa** un simbolo definito in un altro modulo (§2.5) |

> `.global`/`.extern` servono solo per la compilazione separata (`asm`/`ld`).
> In un singolo file `.vasm` eseguito col percorso classico sono superflue.

Le label nella sezione dati catturano l'indirizzo corrente:

```asm
.data
x:  .float 1, 2, 3, 4      ; x -> indirizzo 0
y:  .float 10, 20, 30, 40  ; y -> indirizzo 16
buf: .space 64             ; buf -> indirizzo 32, riserva 256 byte
n:   .word 5, -3, 0x10, 42 ; interi a 32 bit, letti con lw/scritti con sw
```

**Costanti e strutture.** `.equ` e `.struct` producono **costanti simboliche** di
compile-time: puri interi (offset di campo, dimensioni), risolti come letterali
ovunque serva un intero — displacement di `lw`/`sw` e immediati di `li` — e **mai
rilocati**. Servono a togliere i *magic number* dagli accessi ai campi di una
struttura in memoria:

```asm
.equ VLMAX 64             ; costante semplice, poi usabile come `li r1, VLMAX`

.struct TCB               ; layout di un descrittore di task
  .field fwd              ; TCB.fwd   = 0
  .field bwd              ; TCB.bwd   = 4
  .field sp               ; TCB.sp    = 8
  .field state            ; TCB.state = 12
.ends                     ; definisce anche TCB.size = 16

  lw r4, TCB.sp(r1)       ; invece di  lw r4, 8(r1)
  li r2, TCB.size         ; la dimensione totale come immediato
```

Una variabile di quel tipo si alloca nel segmento dati con `.res`, che riserva
`TIPO.size` byte (l'etichetta è il suo indirizzo, in stile C `TIPO nome;`):

```asm
.data
tcbA: .res TCB            ; riserva 16 byte; &tcbA in `li r1, tcbA`
```

Le costanti sono locali al file; per condividerle basta un **header** incluso con
`.include`, che dà una sola sorgente di verità: p.es.
`rtos/scheduler/interface/tcb/tcb.vinc` con `.struct TCB` e gli stati, incluso
sia dal kernel sia da chi lo usa — sempre con la stessa grafia,
`.include "tcb/tcb.vinc"`, perché il nome porta il nome della libreria e si
risolve via `-I`. Come si risolve un nome, e perché una seconda inclusione dello
stesso file non è un errore, sta in §4.2.2.

Le costanti sono **locali al file** (nessuna rilocazione) e vanno definite prima
dell'uso in una direttiva `.equ`/`.field`; nel codice invece sono usabili anche
in avanti. Un blocco `.struct` non chiuso da `.ends` è un errore.

Un operando simbolico può avere un **offset**: `simbolo+N` o `simbolo-N` (N
decimale o esadecimale). Per i dati l'offset è in **byte**, quindi `li r1, y+8`
punta a `y[2]`. Funziona sia in un singolo file sia in compilazione separata
(l'offset diventa l'*addend* della rilocazione, §2.5).

#### 4.2.1 `.proc` / `.endproc`: prologo/epilogo per procedure non-foglia

`call` è sempre `jal r15, target` (il link register è cablato nell'assembler,
non è un operando scelto dal chiamante): una procedura che a sua volta chiama
qualcos'altro (**non-foglia**) deve salvare r15 prima di farlo, altrimenti perde
il proprio indirizzo di ritorno. La convenzione manuale, già usata ovunque nel
kernel, è:

```asm
mia_proc:
  addi r14, r14, -4
  sw r15, 0(r14)      ; prologo: salva il ritorno
  call altra_cosa
  lw r15, 0(r14)
  addi r14, r14, 4    ; epilogo: ripristina il ritorno
  ret
```

`.proc NOME` / `.endproc NOME` genera questo prologo/epilogo per il caso
comune — corpo **lineare**, **un solo** punto d'uscita. `.proc NOME`
**definisce lei stessa l'etichetta** `NOME` nel punto in cui compare
(esattamente come `NOME:`): non va ripetuta una label separata prima, sarebbe
una doppia definizione dello stesso simbolo (errore di assemblaggio, non un
no-op):

```asm
  .proc mia_proc
  call altra_cosa
  .endproc mia_proc
```

`.endproc` verifica che il nome combaci con l'ultimo `.proc` aperto (un `.proc`
senza `.endproc`, o annidato, è un errore di assemblaggio; così come `.proc`
usato fuori dalla sezione `.text`). Non c'è alcuna analisi automatica
leaf/non-foglia: r15 viene sempre salvato, incondizionatamente.

**Salvataggio automatico degli scalari usati.** Oltre a r15, il prologo salva
— e l'epilogo ripristina — solo gli scalari **r1..r13 che il corpo scrive
davvero**, cioè quelli che compaiono come destinazione di un'istruzione
"semplice" (`li`, `mov`, `add`, `sub`, `mul`, `addi`, `slli`, `srli`, `and`,
`or`, `xor`, `div`, `rem`, `lw`, `setvl`, `mfpsw`, `mfepc`). `.endproc`
scansiona l'intero corpo prima di generare il prologo — per questo il corpo
tra `.proc` e `.endproc` non può contenere etichette né direttive, solo
istruzioni semplici (coerente con la forma "corpo lineare" già richiesta):

```asm
  .proc clobber
  li r7, 111          ; r7 scritto qui -> salvato/ripristinato
  add r8, r7, r7       ; r8 scritto qui -> salvato/ripristinato
  call altra_cosa      ; r15 salvato comunque, sempre
  .endproc clobber
```

genera: push r15, push r7, push r8, corpo, pop r8, pop r7, pop r15, `ret` (r15
per primo/ultimo perché deve sopravvivere a **ogni** `call` nel corpo; gli
altri intorno, in ordine crescente/decrescente). In un listato prodotto con
`--emit-expanded` (vedi `vcpu_sim asm`) la prima riga del prologo e l'ultima
dell'epilogo portano un commento che nomina la `.proc` di origine e i
registri auto-salvati/ripristinati, **ciascuno nell'ordine reale delle
`sw`/`lw` sottostanti** (crescente nel prologo, decrescente nell'epilogo, r15
sempre agli estremi), cosi' si distinguono a colpo d'occhio dal corpo scritto
a mano e il commento fa da traccia leggibile del blocco:

```
  addi r14, r14, -4  ; .proc clobber: prologo auto (r15,r7,r8)
  sw r15, 0(r14)
  ...
  ret  ; .proc clobber: fine epilogo auto (r8,r7,r15)
```

**Limite importante**: è un'analisi statica delle sole istruzioni scritte nel
corpo, **non** vede cosa sporca una routine chiamata. Un registro il cui
valore arriva da una `call` (come la `psw` nell'idioma `irq_save`/
`irq_restore` in `generic/queue/impl/src/queue.vasm`, dove la routine
chiamata scrive `r5` ma il corpo della `.proc` lo tratta solo in memoria) resta
invisibile allo scanner e va ancora salvato a mano intorno alla `call`,
esattamente come prima.

Questa sugar **non copre** i casi che non hanno la forma "corpo lineare, un
solo esit" — e nel kernel/HAL sono la maggioranza:

- procedure **foglia** (non chiamano nulla): non serve salvare r15 affatto —
  vedi `ctx_save` in `hal/impl/src/machine.vasm`;
- procedure che **non ritornano mai** (finiscono in `reti`, o incatenano una
  `call` finale che a sua volta non ritorna): non c'è un epilogo da generare —
  vedi `_trap_entry`, `ctx_restore`, `sched_dispatch`, `dispatcher`;
- corpi con **uscite anticipate** verso un epilogo condiviso (un branch a
  un'etichetta a metà procedura): l'epilogo va scritto a mano perché non c'è un
  singolo punto in cui inserirlo automaticamente;
- procedure con **un solo chiamante per costruzione**, non un'API generica
  riusabile (es. `scheduler` in `rtos/scheduler/impl/src/scheduler.vasm`): anche
  quando la forma sarebbe lineare, restano scritte a mano per scelta, riservando
  `.proc` a contratti stabili e pensati per essere richiamati da più punti.

In questi casi si scrive a mano, esattamente come prima che la direttiva
esistesse.

#### 4.2.2 `.include`: dove si cerca un file, e perché includerlo due volte è lecito

Il nome che segue `.include` viene cercato in quest'ordine:

1. la cartella del file di **primo livello** — non quella del file che scrive la
   `.include`, il che conta appena un `.vinc` ne nomina un altro;
2. ogni cartella passata sulla riga di comando con **`-I`**, nell'ordine in cui è
   stata data.

Un nome assoluto (che comincia con `/`) è usato così com'è. Il flag `-I` sta sia
sul percorso a file singolo sia su `asm`, nelle due forme abituali:

```bash
./build/vcpu_sim -I rtos/scheduler/interface -I generic/pool/interface \
                 -I generic/queue/interface tests/test_include.vasm
./build/vcpu_sim asm -Igeneric/pool/interface -Igeneric/coda/interface \
                 generic/test/test_pool.vasm -o build/test_pool.vo
```

Grazie a `-I` la **dipendenza si scrive per nome** (`.include "pool/pool.vinc"`)
invece che per posizione (`.include "../../generic/pool/interface/pool/pool.vinc"`),
e lo stesso file di interfaccia ha una sola grafia in tutto il progetto invece di
una per ogni cartella da cui viene incluso.

> **Il nome porta il nome della libreria, e non è una convenzione estetica.**
> Ogni libreria propaga come `-I` la propria cartella `interface/`, dentro cui
> c'è una sottocartella che ripete il nome della libreria: il file si trova
> solo scrivendo `"pool/pool.vinc"`, e solo se quel `-I` è arrivato. Un modulo
> che include un header senza aver dichiarato la libreria che lo pubblica **non
> assembla**. Prima tutti i `.vinc` stavano in una cartella sola e un `-I` li
> serviva tutti, quindi la dichiarazione di dipendenza era un commento. Chi usa
> il build CMake non scrive nessun `-I` a mano: li genera `INTERFACES` (§3.24
> dell'handoff).

**`.include` è idempotente.** Un file già entrato in questa unità di
assemblaggio non viene incluso una seconda volta: la direttiva è un no-op
silenzioso, esattamente come `#pragma once` in C. L'identità è quella del file
sul disco (path canonicalizzato), non della stringa scritta: `"pool/pool.vinc"`
e `"../generic/pool/interface/pool/pool.vinc"` sono lo stesso file e vengono
riconosciuti tali.

Non è un accessorio. I `.vinc` contengono **solo** costanti di compile-time,
quindi una seconda inclusione fallirebbe con `duplicate constant`; è per questo
che, finché l'idempotenza non c'era, valeva la regola «i `.vinc` sono foglia» —
nessuno poteva includerne un altro, perché sarebbe esploso appena un chiamante
avesse incluso entrambi. Con l'idempotenza un file di interfaccia può dichiarare
le proprie dipendenze come farebbe un header C, e un sorgente può includere due
fornitori senza sapere cosa hanno in comune.

Restano un limite di profondità (8 file aperti insieme) e uno sul numero di file
distinti per unità di assemblaggio. Un file che include sé stesso è un no-op, non
una ricorsione.

`tests/test_include.vasm` è il test mirato di entrambe le proprietà.

#### 4.2.3 `.ifdef`: compilare via del codice, e perché non guarda le `.equ`

Quattro direttive — `.ifdef`, `.ifndef`, `.else`, `.endif` — e un flag, `-D`:

```asm
.ifdef MARKS
  start_mark SCHEDULER, SCAN      ; c'è solo se il kernel è strumentato
.endif
```

```bash
./build/vcpu_sim asm -D MARKS scheduler.vasm -o scheduler.vo   # con
./build/vcpu_sim asm          scheduler.vasm -o scheduler.vo   # senza
```

`-D NOME` (o `-DNOME`) dichiara una **presenza**, non un valore: un nome è
definito o non lo è. `-DNOME=valore` è **rifiutato** — definire anche una
costante è un'altra funzione, e accettare la sintassi ignorando la metà dopo
l'`=` sarebbe la peggiore delle tre uscite. Lo stesso vale per un nome che non
è un identificatore (`-D a-b`): nessun `.ifdef` potrebbe mai scriverlo, quindi
non scatterebbe mai e il build sarebbe sbagliato senza dire niente.

Il flag sta su `asm` e sul percorso legacy a file singolo, come `-I`. Un nome
non definito non è un errore: è la domanda a cui si risponde «no».

I condizionali si **annidano**, e un `.ifdef` dentro un ramo compilato via resta
compilato via qualunque cosa dica: il suo `.else` non può resuscitarlo.

> **`.ifdef` interroga SOLO i nomi di `-D`, mai una costante `.equ`.** Gli
> assembler classici fanno il contrario, e qui sarebbe una trappola: le `.equ`
> si raccolgono **durante** il pass 1 e **in ordine**, quindi la stessa
> `.ifdef VLMAX` risponderebbe «no» sopra la sua `.equ` e «sì» sotto — lo
> stesso difetto silenzioso di una `.word` che riceve il nome di una costante.
> Con i soli `-D` la risposta non dipende da **dove** è scritta la domanda, ed è
> questa proprietà che permette al filtro di stare nel **lettore di righe**, cioè
> prima di tutto il resto.

Che il filtro stia lì ha una conseguenza che conviene sapere: una riga in un ramo
compilato via **non esiste**. Non definisce un'etichetta, non definisce una
costante, non avanza il puntatore dei dati, non finisce nel listato di
`--emit-expanded`. Ed è lo stesso filtro per il percorso a file singolo e per
`asm`: l'assembler legge il sorgente in un punto solo, quindi le due strade non
possono divergere su cosa dice il sorgente.

Quattro errori, tutti dichiarati invece che silenziosi: un `.else`/`.endif` senza
`.ifdef`, un secondo `.else`, un condizionale ancora aperto alla fine di un file
(anche di un `.vinc`: non può chiudersi in quello che lo include), e
un'etichetta sulla stessa riga di un condizionale — che dovrebbe essere definita
dalla riga che il filtro sta per togliere.

`tests/test_ifdef.vasm` è il test mirato, e gira **due volte** con lo stesso
sorgente: senza `-D` e con `-D MARKS`. Una sola delle due proverebbe che il
filtro fa qualcosa, non che sceglie.

#### 4.2.4 `.macro`: una sequenza scritta una volta, e la variante che non c'è

```asm
.macro SOMMA dst, k, tmp
  li dst, k
  li tmp, 5
  add dst, dst, tmp
.endmacro

  SOMMA r1, 10, r2        ; -> li r1, 10 / li r2, 5 / add r1, r1, r2
```

Il corpo viene **espanso** dove la macro è nominata, con i parametri sostituiti
dagli argomenti della chiamata. La sostituzione è per **token intero**: un
parametro che si chiama `a` non riscrive la `a` dentro `vale_a`.

**Un corpo vuoto è legale, e si espande in zero istruzioni.** Non è un caso
limite: è il motivo per cui la direttiva esiste. Due definizioni della stessa
macro dentro un `.ifdef` — una piena e una vuota — danno un tag che nel
programma strumentato c'è e in quello di produzione **non lascia un byte**:

```asm
.ifdef MARKS
.macro DBG_CLOSE ch, tmp
  li tmp, ch
  sw r0, 0(tmp)
.endmacro
.else
.macro DBG_CLOSE ch, tmp
.endmacro
.endif
```

Al punto d'uso non resta nessun condizionale, e `tools/fingerprint.sh` continua
a dimostrare che i programmi puliti sono identici byte per byte. È la forma che
il marcatore usa (`hal/marker.vinc`), scelta contro una coppia di **procedure**
vuota/piena: una `call` più un `ret` costerebbero più di ciò che la sonda misura
e li pagherebbe il binario di produzione (misurato: ~1% della corsa, ~5% del
cammino di latenza pubblicato in `scheduler-facts.md` §6.1).

Le regole, e ognuna è un errore rumoroso invece di una sorpresa:

- il corpo contiene **solo istruzioni semplici**. Niente etichette — due
  espansioni definirebbero lo stesso simbolo due volte — niente direttive,
  niente `.macro` annidata, e nessuna macro che ne chiama un'altra. È la stessa
  restrizione di `.proc`, per la stessa ragione: il corpo scavalca il normale
  smistamento di etichette e direttive;
- la chiamata passa **esattamente** tanti argomenti quanti sono i parametri;
- una `.macro` non chiusa è un errore, e il messaggio dice a che riga era aperta;
- la **definizione** può stare ovunque (non emette niente, quindi vive bene in
  un `.vinc` incluso prima di `.text`); la **chiamata** deve stare nel testo.

Un'etichetta davanti a una chiamata cade sulla **prima** istruzione
dell'espansione.

`tests/test_macro.vasm` è il test mirato: sostituzione per token intero, corpo
vuoto, due espansioni indipendenti della stessa macro e l'etichetta davanti a
una chiamata. I cinque numeri attesi sono tutti derivabili a mano — un test del
linguaggio che dipendesse dai cicli misurerebbe la macchina invece
dell'assembler.

#### 4.2.5 `.interrupt`: il gestore di trap, e il dispatch diretto

```asm
  .interrupt isr
  mfcause r3
  ...                     ; il corpo: etichette e direttive ammesse
  li r1, la_mia_wait
  call wait_signal        ; r1 = il TCB da svegliare, o 0
  .endinterrupt isr r1
```

`.interrupt NOME` definisce l'etichetta `NOME`, come `.proc`, e non emette
altro. `.endinterrupt NOME rN` emette la **coda**, e soltanto quella:

```asm
  mov r2, rN              ; omessa se rN e' gia' r2
  mov r1, r14             ; il contesto opaco del task interrotto
  j   sched_isr_exit_to
```

`sched_isr_exit_to` (nel kernel, `rtos/scheduler/impl/src/`) mette in
esecuzione il TCB in `r2` **senza passare dallo scheduler**: è il dispatch
diretto di §3.74 dell'handoff. Con `r2 = 0` fa quello che fa `sched_isr_exit`,
cioè l'uscita ordinaria — quindi `.endinterrupt NOME r0` è la forma per un
gestore che non ha nessuno da svegliare. L'operando è **obbligatorio**: dirlo
costa un token, e toglie il caso di un gestore che salta al kernel con un `r2`
qualunque.

**La testa è vuota, di proposito.** Il contesto lo salva già il vettore
dell'HAL (`_trap_entry` → `ctx_save`), che consegna con `r1` = contesto opaco
**e** `r14` = lo stesso puntatore, perché il contesto opaco è la cima dello
stack. Da cui l'unica regola del corpo: **deve lasciare lo stack com'era**, la
stessa che `.proc` impone al suo. Le ISR scritte a mano tengono il contesto in
un registro scelto a occhio (`mov r6, r1` … `mov r1, r6`); con la direttiva non
serve.

A differenza di `.proc` il corpo **non** è bufferizzato — la coda non dipende da
lui — quindi etichette e direttive dentro sono ammesse. Non è annidabile, non
accetta `.proc` dentro, vive solo in `.text`, e un `.interrupt` non chiuso è un
errore. Un programma che non la usa non ne riceve un byte.

`rtos/test/test_wait.vasm` è il programma che la usa, insieme alla coppia
`wait`/`wait_signal` (`rtos/services/wait/`).

#### 4.2.6 `.supertask`: il TCB che nasce super, e al più uno

```asm
.include "tcb/tcb.vinc"
  .data
  .supertask tcbT          ; al posto di  tcbT: .res TCB
```

Il **super task** è il task che l'ISR della time line mette in esecuzione col
dispatch diretto, e mentre gira **lo scheduler non sceglie**: gli interrupt si
servono, ma nessuno prende la CPU finché lui non va in `wait`. La definizione
completa sta in `tcb.vinc`, accanto al campo `TCB.nopreempt`.

La direttiva fa tre cose, e tutte e tre sono sue e non di chi la scrive:

- definisce l'etichetta `NOME` e riserva `TCB.size` byte, come `.res TCB`;
- scrive **1** nella parola `TCB.nopreempt` dell'immagine dati: il TCB nasce
  super, e nessun boot deve ricordarsi di scriverlo;
- definisce allo stesso indirizzo il simbolo **globale** `super_tcb`.

Il terzo punto è la rete. Il super task è **al più uno**, e l'unicità la
controlla la toolchain con i meccanismi che ha già: due `.supertask` nello
stesso file fermano l'assembler («the super task is at most one»), in due file
fermano `ld` («duplicate global 'super_tcb'»).

Vive solo in `.data`, e vuole `tcb/tcb.vinc` incluso: cerca `TCB.size` e
`TCB.nopreempt` fra le costanti, e senza di loro è un errore.

#### 4.2.7 `.shared`: le variabili che due CPU si scambiano

Con più CPU ognuna ha la sua **RAM locale** da 0, e si incontrano in una **RAM
condivisa** (`hal/amp.vinc`). Il problema che `.shared` risolve: due programmi
linkati **separatamente**, uno per CPU, devono concordare sull'indirizzo di ciò
che sta in mezzo — e se l'accordo è scritto in due posti, divergono in silenzio.

Si dichiarano una volta, in un oggetto linkato in **tutte e due** le immagini:

```asm
  .shared
contatore:  .space 1      ; una parola
pronto:     .space 1
```

e chi le usa le importa come qualunque altro simbolo esterno:

```asm
  .extern contatore
  ...
  li r8, contatore        ; l'indirizzo viene dal file di collocazione
```

Tre regole, e ognuna ha la sua ragione:

- **non si inizializza.** `.space` e `.res` sì, `.word` e `.float` no. Se le due
  immagini portassero entrambe il valore iniziale lo scriverebbero due volte
  nella stessa RAM, e su hardware vero l'ordine dei caricamenti — e quale CPU è
  già partita — deciderebbe chi vince. Il valore iniziale, se serve, lo scrive
  **una** CPU in codice e l'altra aspetta un flag;
- **i simboli sono globali per definizione**, senza `.global`: la sezione dice già
  che il dato sta nella RAM che l'altra CPU vede. Da cui una rete gratis: la
  stessa variabile dichiarata da due oggetti della **stessa** immagine ferma `ld`
  con `duplicate global`;
- **vuole il linker.** L'indirizzo lo decide la regione `SHARED` del file di
  collocazione (`vcpu_sim ld --verbose` per vedere quello incorporato), e un
  programma a file singolo non ne ha uno: lì `.shared` è un errore che lo dice.

E `run`, con due programmi, **confronta** ciò che i due `.symmap` dichiarano di
condiviso — nomi, indirizzi, dimensioni — e rifiuta due immagini che non sono
d'accordo, prima di far partire le CPU.

### 4.3 Manuale delle istruzioni

Convenzioni: `rd`/`rs` = registro scalare intero, `fd`/`fs` = registro scalare
float, `vd`/`vs` = registro vettoriale, `imm` = immediato intero/simbolo,
`fimm` = immediato float, `label` = etichetta di codice.
La colonna **Cicli** riporta il costo nel modello di timing (§6);
`P = ceil(VL / VEC_LANES)`.

#### Scalari intere

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `li` | `rd, imm` | `rd = imm` (imm può essere un simbolo/indirizzo) | 1 |
| `mov` | `rd, rs1` | `rd = rs1` | 1 |
| `add` | `rd, rs1, rs2` | `rd = rs1 + rs2` | 1 |
| `sub` | `rd, rs1, rs2` | `rd = rs1 - rs2` | 1 |
| `mul` | `rd, rs1, rs2` | `rd = rs1 * rs2` | 1 |
| `addi` | `rd, rs1, imm` | `rd = rs1 + imm` | 1 |
| `slli` | `rd, rs1, imm` | `rd = rs1 << imm` (shift logico a sinistra) | 1 |
| `srli` | `rd, rs1, imm` | `rd = rs1 >> imm` (shift logico a destra, senza segno) | 1 |
| `srai` | `rd, rs1, imm` | `rd = rs1 >> imm` (shift **aritmetico**: il segno si replica). Su un negativo è la divisione per `2^imm` col **floor** — `srli` darebbe il pattern letto senza segno, e `div` costa 20 cicli. Non è `div`: `srai` arrotonda verso meno infinito, `div` tronca verso zero, quindi `-65 >> 3` fa `-9` e `-65 / 8` fa `-8` | 1 |
| `and` | `rd, rs1, rs2` | `rd = rs1 & rs2` (AND bit a bit) | 1 |
| `or` | `rd, rs1, rs2` | `rd = rs1 \| rs2` (OR bit a bit) | 1 |
| `xor` | `rd, rs1, rs2` | `rd = rs1 ^ rs2` (XOR bit a bit) | 1 |
| `div` | `rd, rs1, rs2` | `rd = rs1 / rs2` (divisione intera; `0` se `rs2 == 0`) | 20 |
| `rem` | `rd, rs1, rs2` | `rd = rs1 % rs2` (resto; `0` se `rs2 == 0`) | 20 |

#### Scalari float

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `fli` | `fd, fimm` | `fd = fimm` | 1 |
| `flw` | `fd, rs1` &nbsp;/&nbsp; `fd, disp(rs1)` | `fd = mem_float[rs1 + disp]` (carica un float; `disp` opzionale, default 0) | 4 |
| `fsw` | `fs, rs1` &nbsp;/&nbsp; `fs, disp(rs1)` | `mem_float[rs1 + disp] = fs` | 4 |
| `fadd` | `fd, fs1, fs2` | `fd = fs1 + fs2` | 4 |
| `fmul` | `fd, fs1, fs2` | `fd = fs1 * fs2` | 4 |
| `fmacc` | `fd, fs1, fs2` | `fd += fs1 * fs2` (multiply-accumulate, **non fusa**: due arrotondamenti, come `vmacc`) | 4 |
| `fmov` | `fd, fs1` | `fd = fs1` | 4 |
| `fmin` | `fd, fs1, fs2` | `fd = min(fs1, fs2)` | 4 |
| `fmax` | `fd, fs1, fs2` | `fd = max(fs1, fs2)` | 4 |
| `fsub` | `fd, fs1, fs2` | `fd = fs1 - fs2` | 4 |
| `fdiv` | `fd, fs1, fs2` | `fd = fs1 / fs2` (`0` se `fs2 == 0`) | 4 |
| `fneg` | `fd, fs1` | `fd = -fs1` | 4 |
| `fsqrt` | `fd, fs1` | `fd = sqrt(fs1)` | 4 |

#### Memoria scalare (interi)

Accessi a **word intere con segno a 32 bit** (elemento = 4 byte). `lw` estende il
segno, `sw` tronca a 32 bit. I dati interi si inizializzano con la direttiva
`.word` (§4.2).

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `lw` | `rd, rs1` &nbsp;/&nbsp; `rd, disp(rs1)` | `rd = mem_int32[rs1 + disp]` (esteso in segno) | 4 |
| `sw` | `rs, rs1` &nbsp;/&nbsp; `rs, disp(rs1)` | `mem_int32[rs1 + disp] = (int32) rs` | 4 |

**Indirizzamento base+displacement.** Le quattro load/store scalari (`lw`, `sw`,
`flw`, `fsw`) accettano un operando di memoria in due forme:

- `rs1` &rarr; indirizzo = contenuto del registro base (displacement 0);
- `disp(rs1)` &rarr; indirizzo = `rs1 + disp`, con `disp` intero (decimale o `0x`
  esadecimale, anche negativo), in **byte**.

È la modalità classica "alla RISC-V" per accedere ai **campi di una struttura**
tramite un puntatore base: `lw r6, 8(r1)` carica il campo a offset 8 dalla struct
puntata da `r1`, senza calcolare a parte l'indirizzo. La forma `lw rd, rs1` resta
valida ed equivale a `lw rd, 0(rs1)`.

#### Controllo del vettore

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `setvl` | `rd, rs1` | `VL = min(rs1, VLMAX); rd = VL` | 1 |

#### Controllo di flusso

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `beq` | `rs1, rs2, label` | se `rs1 == rs2` salta a `label` | 1 |
| `bne` | `rs1, rs2, label` | se `rs1 != rs2` salta a `label` | 1 |
| `blt` | `rs1, rs2, label` | se `rs1 < rs2` salta a `label` | 1 |
| `j` | `label` | salto incondizionato | 1 |
| `jal` | `rd, label` | jump-and-link: `rd = pc successivo; pc = label` | 1 |
| `jalr` | `rd, rs1` | jump indiretto: `rd = pc successivo; pc = rs1` | 1 |
| `call` | `label` | zucchero per `jal r15, label` (chiamata) | 1 |
| `ret` | — | zucchero per `jalr r0, r15` (ritorno) | 1 |
| `jr` | `rs1` | zucchero per `jalr r0, rs1` (salto indiretto) | 1 |
| `halt` | — | ferma l'esecuzione | 1 |

**Chiamate a subroutine.** `call`/`ret` sono lo zucchero sintattico su `jal`/`jalr`
attorno al registro di link convenzionale **`ra = r15`** (indirizzo di ritorno).
`jal rd, label` salva in `rd` l'indice dell'istruzione *successiva* alla chiamata,
poi salta a `label` (rilocabile, quindi le funzioni possono stare in un altro modulo);
`jalr rd, rs1` fa lo stesso ma salta all'indirizzo contenuto in `rs1`, ed è il
meccanismo del ritorno (`ret` = `jalr r0, r15`, con la scrittura in `r0` scartata).
Con un solo registro di link, le **chiamate annidate** devono salvare `ra` prima di
chiamare (tipicamente sullo stack, per convenzione `sp = r14`); una foglia che non
chiama nulla può lasciare `ra` in `r15`. Vedi `standalone/call.vasm`.

#### Interruzioni e parola di stato

Il modello ha un semplice **timer** che genera interruzioni, uno strato di
controllo scalare con **parola di stato** `psw` (bit 0 = `IE`, interrupt enable),
e i due registri ombra salvati al trap: `epc` (PC di ripresa) e `epsw` (copia
della `psw`). Al reset è tutto a zero, quindi un programma che non abilita gli
interrupt gira esattamente come prima.

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `sethandler` | `label` | `handler = label` (indice istruzione del gestore) | 1 |
| `settimer` | `rs1` | periodo del timer in cicli (`0` = disarmato); arma a `cicli + rs1` | 1 |
| `sti` | — | `psw \|= IE` (abilita le interruzioni) | 1 |
| `cli` | — | `psw &= ~IE` (disabilita le interruzioni) | 1 |
| `reti` | — | ritorno da interrupt: `pc = epc; psw = epsw` (ripristina lo stato) | 1 |
| `mfpsw` | `rd` | `rd = psw` (leggi la parola di stato) | 1 |
| `mtpsw` | `rs1` | `psw = rs1` (scrivi la parola di stato) | 1 |
| `mfepc` | `rd` | `rd = epc` (per salvare il contesto) | 1 |
| `mtepc` | `rs1` | `epc = rs1` (dove tornerà la `reti`) | 1 |
| `mfepsw` | `rd` | `rd = epsw` (la parola di stato del task interrotto) | 1 |
| `mtepsw` | `rs1` | `epsw = rs1` (**in che regime** tornerà la `reti`) | 1 |
| `mfvl` | `rd` | `rd = vl` — lettura **non distruttiva** della lunghezza vettoriale | 1 |
| `mtvl` | `rs1` | `vl = min(rs1, VLMAX)` — **ripristino**, non richiesta | 1 |
| `mfvmask` | `rd` | `rd = vmask` (64 bit) | 1 |
| `mtvmask` | `rs1` | `vmask = rs1` | 1 |
| `mark` | `porto, valore` | annota una marca sul canale del marcatore: valore ≠ 0 **apre** una finestra, 0 la **chiude**. Due immediati, **nessun registro** | 1 |
| `mfcause` | `rd` | `rd = cause` — **chi** ha interrotto: `CAUSE_TIMER` o `CAUSE_KBD` | 1 |

> **`mfcause` esiste perché le sorgenti sono due** (14/09/2026). Fino a quel
> giorno la macchina aveva solo il timer e la tastiera si interrogava; da allora
> anche il tasto può interrompere, e con un vettore solo il gestore deve poter
> chiedere chi è stato. È il modello di **RISC-V** — una causa che il gestore
> legge — e non quello del NVIC di un Cortex-M, che ha un vettore per sorgente.
>
> La differenza fra i due si paga in **latenza**: leggere la causa e diramarsi
> costa istruzioni, e quelle istruzioni stanno sul cammino fra l'interruzione e
> il codice che la serve. È esattamente la grandezza che il marcatore misura,
> quindi quanto varrebbe il vettoriamento su questa macchina si potrà **misurare
> invece che stimare**.
>
> La causa vale **fino alla trap successiva**: un'ISR che riabilita gli
> interrupt deve leggerla subito, come si fa con `mcause`. E se le due sorgenti
> sono pronte insieme **vince il timer** — è il battito dello scheduler e non
> deve derivare, mentre un carattere può aspettare un tick senza che nessuno se
> ne accorga.

> **`mark` esiste per un motivo solo: non toccare registri** (14/09/2026).
> La stessa marca si scriveva già con `li`/`li`/`sw` su un porto MMIO, e continua
> a funzionare. Il problema di quella forma non era il costo — erano i registri:
> ne serve uno d'appoggio, e va scelto guardando chi è vivo a quel punto. **Dove
> non ce n'è uno libero, il tag non si può mettere.** È il caso della chiusura
> dentro `ctx_restore`: prima della `reti` ogni registro porta già un valore del
> task, quindi la finestra si chiudeva sette istruzioni troppo presto e il numero
> pubblicato in `scheduler-facts.md` §6.1 era corto di 11 cicli.
>
> **Costa 1 ciclo, non 0.** Uno slot occupato lo occupa anche l'hardware di
> trace, e in cambio si ottiene una proprietà utile: il costo della
> strumentazione dentro una finestra è **esattamente il numero di marche che
> contiene**, quindi il numero del sistema non strumentato si deduce dalla
> registrazione invece che da un'analisi del listato.
>
> Il porto è un **indirizzo**, lo stesso che la `sw` usava (`MARK_SCHED` e
> compagnia, generati da `marks.conf`): il porto ha già un nome, e dargliene un
> secondo sarebbe la seconda verità che quel file esiste per togliere. Fuori dai
> porti del marcatore, o su un canale riservato alla macchina, la macchina si
> ferma e lo dice — una marca persa in silenzio è una misura che manca senza che
> nessuno lo sappia.
>
> L'opcode sta **in fondo** all'enum, e non è un dettaglio: gli opcode sono
> numerati per posizione e finiscono così negli oggetti. Inserirlo in mezzo ha
> mosso l'impronta di tutti e quindici i programmi, compresi quelli che `mark`
> non la contengono nemmeno.

> **I quattro accessori vettoriali esistono per una cosa sola: rendere
> salvabile il contesto** (12/09/2026). Prima di loro `vmask` era leggibile solo
> da `vmerge` e dalle operazioni mascherate, e `vl` era soltanto *impostabile* —
> `setvl` scrive e restituisce il valore nuovo, quindi leggerlo lo distrugge. Due
> terzi dello stato architetturale vettoriale non erano accessibili al software,
> e il context switch di un task vettoriale non era scrivibile.
>
> `mtvl` sta **accanto** a `setvl` e non al suo posto: `setvl` è la richiesta di
> un calcolo («dammene fino a *n*»), `mtvl` è il ripristino di uno stato. Usare
> `setvl` per ripristinare funzionerebbe *per caso*, perché il valore salvato è
> già ≤ VLMAX.
>
> Nota di disegno, che si scopre solo provando a fermare la macchina: in RISC-V
> «V» la maschera **è `v0`**, un registro vettoriale ordinario — non per economia
> di codifica, ma perché il salvataggio di contesto non abbia un caso speciale.
> Qui `vmask` è un registro a sé, e quella divergenza costa esattamente queste
> due istruzioni.

**`PSW_VDIRTY` (bit 1) e `PSW_FDIRTY` (bit 2 della psw).** La macchina li alza a
ogni scrittura di stato dell'**estensione**: `VDIRTY` per `v0..v7`, `vl`, `vmask`;
`FDIRTY` per i sedici registri **float** (anche le riduzioni `vredsum`/`vredmax`/
`vredmin`, che scrivono un float). Servono a `ctx_save`, che così salva **solo i
blocchi che il task ha toccato**: i 64 byte dei float, i 2060 dei vettori, tutti e
due o nessuno. Salvare sempre costerebbe ~1600 cicli per commutazione, pagati
anche da chi l'estensione non la sfiora. Viaggiano nella psw, quindi nel frame e
indietro con `reti`, perché sono una proprietà del task che riprende. Le letture
(`mfvl`, `mfvmask`, `fsw`, `vstore`) **non** li alzano.

> Fino all'8/10/2026 il bit era uno solo, `VDIRTY`, e lo alzavano anche i float:
> un task che faceva un conto in float senza un'istruzione vettoriale salvava
> anche i 2 KB dei vettori. `tests/test_fdirty.vasm` misura la taglia del frame
> nei quattro casi: 68, 132, 2128, 2192 byte.

**Come funziona il trap.** L'interruzione del timer è consegnata al **confine di
istruzione**: quando `IE` è attivo, il timer è armato e il contatore dei cicli
raggiunge la scadenza, prima di eseguire l'istruzione successiva la macchina salva
`epc = pc` ed `epsw = psw`, azzera `IE`, ri-arma il timer e salta a `handler`. Il
gestore gira quindi con le interruzioni disabilitate (niente annidamento con una
sola coppia di registri ombra). `reti` ripristina in blocco `pc` e `psw`: è il
modello "salva-stato / ripristina-stato" (analogo a `mepc`/`mret` di RISC-V o
all'*exchange package* del Cray). Per un **cambio di contesto** il gestore
riscrive `epc` con `mtepc` prima di `reti`, facendo ripartire un task diverso.
Nel debugger `p psw` e `p epc` mostrano questi registri.

> **Il ritorno decide due cose: dove si va e in che regime.** `epc` dice dove,
> `epsw` dice se gli interrupt saranno aperti. Sono parole distinte e vanno
> scritte con istruzioni distinte — `mtepc` e `mtepsw`.
>
> Attenzione a un errore facile, che questo manuale conteneva fino al
> 05/09/2026: **`mtpsw` non serve a questo.** Scrive la PSW *attiva*, che `reti`
> sovrascrive un'istruzione dopo con `epsw`; per lo stesso motivo non serve
> nemmeno `cli`. Fino a quel giorno `epsw` non era né leggibile né scrivibile,
> quindi ogni `reti` riportava per forza il regime del task interrotto — bene
> finché si torna sempre a un task, un baco nel momento in cui il ritorno punta
> a codice di kernel, che si troverebbe a girare con gli interrupt aperti.
> `mfepsw`/`mtepsw` esistono per questo: vedi §12.5 della
> [proposta](proposta-kernel-realtime.md), e `tests/test_epsw.vasm` che lo
> dimostra.

#### Vettoriali

Tutte operano sui primi `VL` elementi.

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `vload` | `vd, rs1` | load unit-stride: `vd[i] = mem_float[rs1 + i*4]` | 12 + P |
| `vload` | `vd, rs1, rs2` | load con stride in byte `rs2`: `vd[i] = mem_float[rs1 + i*rs2]` | 12 + P |
| `vstore` | `vs, rs1` | store unit-stride: `mem_float[rs1 + i*4] = vs[i]` | 12 + P |
| `vloadx` | `vd, rs1, vidx` | gather: `vd[i] = mem_float[rs1 + vidx[i]*4]` (indici in elementi) | 12 + 2P |
| `vstorex` | `vs, rs1, vidx` | scatter: `mem_float[rs1 + vidx[i]*4] = vs[i]` (indici in elementi) | 12 + 2P |
| `vadd` | `vd, vs1, vs2` | `vd[i] = vs1[i] + vs2[i]` | 6 + P |
| `vsub` | `vd, vs1, vs2` | `vd[i] = vs1[i] - vs2[i]` | 6 + P |
| `vmul` | `vd, vs1, vs2` | `vd[i] = vs1[i] * vs2[i]` | 6 + P |
| `vmacc` | `vd, fs, vs1` | `vd[i] += fs * vs1[i]` (scalare × vettore, accumulato) | 6 + P |
| `vscale` | `vd, vs1, fs` | `vd[i] = vs1[i] * fs` | 6 + P |
| `vadds` | `vd, vs1, fs` | `vd[i] = vs1[i] + fs` (somma di uno scalare a ogni corsia) | 6 + P |
| `vmin` | `vd, vs1, vs2` | `vd[i] = min(vs1[i], vs2[i])` | 6 + P |
| `vmax` | `vd, vs1, vs2` | `vd[i] = max(vs1[i], vs2[i])` | 6 + P |
| `vredsum` | `fd, vs` | riduzione: `fd = vs[0] + vs[1] + ... + vs[VL-1]` | 6 + P + ⌈log₂VL⌉ |
| `vredmax` | `fd, vs` | riduzione: `fd = max(vs[0..VL-1])` | 6 + P + ⌈log₂VL⌉ |
| `vredmin` | `fd, vs` | riduzione: `fd = min(vs[0..VL-1])` | 6 + P + ⌈log₂VL⌉ |
| `vsplat` | `vd, fs` | broadcast: `vd[i] = fs` per ogni corsia | 6 + P |
| `vcvt` | `vd, vs` | conversione: i bit di `vs[i]` letti come `int32`, `vd[i] = (float)` di quel valore | 6 + P |

> **`vcvt` è l'unica conversione intero → float dell'ISA** (28/09/2026), e
> nasce con l'ADC a 12 bit: i campioni sono interi, i calcoli sono in float.
> Funziona perché un registro vettoriale porta **bit**, non un tipo: `vload`
> copia la parola così com'è, e finché nessuna operazione aritmetica la tocca
> resta un intero. Usarla come float **prima** di `vcvt` dà spazzatura: un
> intero negativo letto come float è un NaN. È il modello di RVV
> (`vfcvt.f.x.v`). Non esiste la versione scalare né quella inversa: non
> servono ancora a nessuno.

> **Attenzione all'ordine degli operandi float:** in `vmacc` il registro float è
> il **secondo** operando (`vmacc vd, fs, vs1`), in `vscale` è il **terzo**
> (`vscale vd, vs1, fs`).

> `vredsum` è l'unica operazione che **collassa** un vettore in uno scalare
> (float): serve per prodotti scalari e norme. Il termine `⌈log₂VL⌉` nel costo
> modella l'albero di riduzione, il motivo per cui le riduzioni sono meno
> efficienti delle operazioni elemento-per-elemento. La somma si accumula
> internamente in doppia precisione e si arrotonda a 32 bit **una volta**, alla
> fine: il risultato non dipende dall'ordine degli elementi.

> `vloadx`/`vstorex` (**gather/scatter**) usano un vettore di indici arbitrari
> (uno per corsia, espressi in *elementi*) invece di uno stride costante:
> servono per permutazioni, tabelle di lookup e dati sparsi. Costano una
> passata in più sulle corsie (`12 + 2P`) perché gli accessi non sono
> contigui.

#### Predicazione / maschere vettoriali

Le istruzioni di confronto scrivono il **registro di maschera** `vmask` (un bit
per corsia, per i primi `VL` elementi); `vmerge` seleziona per corsia in base a
`vmask`. È il modo con cui una macchina vettoriale realizza gli `if` per corsia
senza divergenza di flusso.

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `vmslt` | `vs1, vs2` | `vmask[i] = (vs1[i] < vs2[i])` | 6 + P |
| `vmsgt` | `vs1, vs2` | `vmask[i] = (vs1[i] > vs2[i])` | 6 + P |
| `vmseq` | `vs1, vs2` | `vmask[i] = (vs1[i] == vs2[i])` | 6 + P |
| `vmerge` | `vd, vs1, vs2` | `vd[i] = vmask[i] ? vs1[i] : vs2[i]` | 6 + P |

Le versioni **mascherate** aggiornano solo le corsie con `vmask[i] = 1`; le
altre restano invariate (tail *undisturbed*). È la predicazione per corsia,
l'equivalente vettoriale di un `if` senza divergenza di flusso.

| Istruzione | Operandi | Semantica | Cicli |
|---|---|---|---|
| `vaddm` | `vd, vs1, vs2` | `vd[i] = vmask[i] ? vs1[i]+vs2[i] : vd[i]` | 6 + P |
| `vsubm` | `vd, vs1, vs2` | `vd[i] = vmask[i] ? vs1[i]-vs2[i] : vd[i]` | 6 + P |
| `vmulm` | `vd, vs1, vs2` | `vd[i] = vmask[i] ? vs1[i]*vs2[i] : vd[i]` | 6 + P |
| `vstorem` | `vs, rs1` | store unit-stride della sola corsia `i` se `vmask[i] = 1` | 12 + P |

#### Debug (costo 0 cicli)

| Istruzione | Operandi | Effetto |
|---|---|---|
| `dumps` | `rs1` | stampa un registro scalare intero |
| `dumpf` | `fs` | stampa un registro float |
| `dumpv` | `vs` | stampa i primi `VL` elementi di un vettore |
| `dumpm` | `rs1, imm` | stampa `imm` float a partire dall'indirizzo in `rs1` |
| `dumpmask` | — | stampa i primi `VL` bit del registro di maschera `vmask` |

---

## 5. L'assembler

L'assembler (`src/assembler.c`) traduce il sorgente `.vasm` in un array di
strutture `Instr` decodificate. Lavora in **due passi**.

### Passo 0 — il lettore di righe

Prima dei due passi c'è il punto in cui una riga **entra** nell'assembler, ed è
uno solo (`inc_next_line`): il passo 1 legge da lì e conserva per il passo 2 le
righe di codice sopravvissute. Lì stanno le due cose che decidono *quale
sorgente* si sta assemblando — la pila delle `.include` (§4.2.2) e il filtro dei
condizionali (§4.2.3) — e starci in un punto solo è ciò che impedisce alle due
passate, e ai due ingressi (file singolo e `asm`), di non essere d'accordo su
cosa dice il sorgente.

### Passo 1 — raccolta simboli ed emissione dati

Per ogni riga:

1. Rimuove il commento (da `;` o `#`) e tokenizza (le virgole valgono come
   spazi).
2. Se la riga inizia con una **label** (`nome:`), la registra nella symbol table
   con valore:
   - indice della prossima istruzione, se nella sezione **codice**;
   - indirizzo dati corrente, se nella sezione **dati**.
3. Se è una **direttiva** (`.text`, `.data`, `.float`, `.space`) la esegue:
   `.float` scrive subito i byte nella memoria della macchina e avanza il
   puntatore dati.
4. Se è un'**istruzione**, la memorizza (senza la label) per il passo 2 e
   incrementa il contatore delle istruzioni.

### Passo 2 — codifica

Ogni riga di codice viene ri-tokenizzata e passata a `encode()`, che:

- riconosce il mnemonico,
- verifica il numero di operandi,
- interpreta i registri controllando il **prefisso** (`r`/`f`/`v`) e
  l'intervallo,
- risolve gli immediati (numeri decimali/esadecimali) e i **simboli** (label →
  valore),
- produce la struttura `Instr`.

### La symbol table

- Capacità: `MAX_SYMBOLS` (512).
- Un solo spazio dei nomi condiviso tra label di codice e di dati; il *valore* è
  interpretato in base al contesto (indice istruzione per i salti, indirizzo per
  `li`).
- Le **label in avanti** funzionano: essendo raccolte tutte nel passo 1, un
  salto può riferirsi a una label definita più in basso nel file.
- Le label duplicate sono un errore.

### Limiti

| Limite | Valore | Costante |
|---|---|---|
| Istruzioni per programma | 4096 | `MAX_INSTR` |
| Simboli | 512 | `MAX_SYMBOLS` |
| Lunghezza riga sorgente | 512 byte | — |
| Token per riga | 64 | — |

---

## 6. Il modello di timing

È un modello **al prim'ordine**, in-order, **senza sovrapposizione né chaining**
(esecuzione puramente sequenziale). Ogni istruzione aggiunge un costo fisso al
contatore `cycles`.

Costanti (in `include/vcpu.h`):

| Costante | Valore | Uso |
|---|---|---|
| `CYC_SCALAR_ALU` | 1 | ALU intera, `li`, `mov`, `setvl`, `fli` |
| `CYC_SCALAR_BR` | 1 | salti e `halt` |
| `CYC_SCALAR_FP` | 4 | ALU float scalare |
| `CYC_SCALAR_MEM` | 4 | load/store scalari (`flw`/`fsw`) |
| `VEC_LANES` | 1 | corsie parallele dell'unità vettoriale |
| `VEC_ARITH_STARTUP` | 6 | riempimento pipeline per op aritmetiche vettoriali |
| `VEC_MEM_STARTUP` | 12 | riempimento pipeline per op di memoria vettoriali |
| `CPU_HZ` | 100 000 000 | la frequenza: **100 MHz**, cioè un ciclo = 10 ns |

Costo di un'operazione vettoriale su `VL` elementi:

```
cicli = STARTUP + ceil(VL / VEC_LANES)
```

Lo **startup si paga una volta per istruzione**, quindi si ammortizza sui vettori
lunghi: è la ragione per cui l'esecuzione vettoriale batte quella scalare quando
i dati sono lunghi e regolari.

Esempio (SAXPY, `N = 10`):

| Versione | Cicli |
|---|---:|
| Scalare | 206 |
| Vettoriale | 94 |

Il vantaggio **cresce con `N`**: a `N = 64` (un blocco pieno) il rapporto passa
da ~2,2× a ~4,2×.

> Estensioni realistiche non ancora implementate: **chaining** (l'op successiva
> inizia mentre la precedente sta ancora producendo elementi), più **corsie**
> (`VEC_LANES > 1`) e latenze di memoria variabili. Gather/scatter esistono
> (`vloadx`/`vstorex`) ma con un modello di costo semplificato (`12 + 2P`, senza
> penalità per conflitti di banco).

### 6.1 La frequenza, e perché i cicli si possono leggere come tempo

`CPU_HZ` è la metà mancante del modello: senza di essa un ciclo è un numero
puro, con essa è **10 ns**. Sta in `include/vcpu.h` accanto alle altre costanti
perché è una proprietà della **macchina**, e da lì **viaggia**: la simulazione
la stampa accanto ai cicli (`clock (Hz)`) e la scrive in testa alla
registrazione delle marche (`# frequenza`). Gli strumenti la leggono da lì, e
nessuno ne tiene una copia — `tools/trace.py` e `tools/marks.py read`
accettano `--mhz` per rileggere la **stessa** registrazione a un'altra
frequenza, senza rieseguire niente.

Dal 29/09/2026 `CPU_HZ` è la frequenza del **clock master** della scheda
(`include/machine.h`), e ogni dominio ne prende una con un divisore intero: la
CPU, le periferiche (`ADC_PERIOD` si conta in tick periferiche) e i
millisecondi di `CLOCK_MS`. Oggi i divisori di CPU e periferiche sono 1,
quindi un ciclo di CPU è ancora un tick master e tutto quanto sopra vale alla
lettera. Il tempo simulato **si calcola, non scorre**: il clock non genera
eventi, e nessun processo lo scandisce.

**100 MHz** perché è il **GR712RC**, il LEON3-FT doppio che l'ESA ha volato di
più, e questa è una macchina di quella famiglia: `VEC_LANES 1` con startup 6 —
cioè una **pipeline vettoriale alla Cray**, un elemento per ciclo, non una SIMD
larga — 1 MiB on-chip e nessuna cache. È il profilo di un core embedded
single-issue in-order con coprocessore vettoriale, del tipo che sta in FPGA o in
un ASIC rad-hard.

| | |
|---|---|
| LEON3 rad-hard (il cavallo da lavoro ESA) | 50–100 MHz |
| **GR712RC** (LEON3-FT doppio, molto volato) | **100 MHz** |
| GR740 (LEON4-FT quad, NGMP) | 250 MHz |
| soft core / coprocessore in fabric | 50–200 MHz |

> **ATTENZIONE, e conta più del numero.** I cicli di questo progetto **non sono
> una misura: sono l'uscita di un modello**. Li decide `vcpu.c`, e qui non c'è
> un sistema di memoria — niente cache miss, niente contesa DMA, niente
> conflitti di banco. Moltiplicarli per una frequenza dà un tempo che *sembra
> più reale dei cicli da cui viene*: «118 cicli» si legge come un numero di
> modello, «1,18 µs» si legge come una misura.
>
> Da cui la regola che tutti gli strumenti seguono: **mai il tempo da solo** —
> sempre accanto ai cicli, che restano la cosa misurata (`Δ 13.430 cicli ·
> 134,3 µs`) — e **la frequenza sempre visibile** accanto alla conversione
> (`@ 100 MHz`), così si legge per l'ipotesi che è.

---

## 7. Esempi completi

### 7.1 SAXPY vettoriale con strip-mining (`standalone/saxpy.vasm`)

```asm
.data
x:  .float 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
y:  .float 10, 20, 30, 40, 50, 60, 70, 80, 90, 100

.text
  li    r1, x          ; &x
  li    r2, y          ; &y
  li    r3, 10         ; n
  fli   f0, 2.5        ; a
loop:
  setvl r4, r3         ; VL = min(n_rimasti, VLMAX)
  vload v0, r1         ; blocco di x
  vload v1, r2         ; blocco di y
  vmacc v1, f0, v0     ; y += a * x
  vstore v1, r2
  slli  r5, r4, 2      ; byte = VL * 4
  add   r1, r1, r5
  add   r2, r2, r5
  sub   r3, r3, r4
  bne   r3, r0, loop   ; continua finché restano elementi
  li    r6, y
  dumpm r6, 10
  halt
```

Esecuzione:

```bash
./build/vcpu_sim standalone/saxpy.vasm
```

### 7.2 Confronto scalare vs vettoriale

La versione scalare equivalente è in `standalone/saxpy_scalar.vasm` (un elemento
per iterazione, con `flw`/`fmacc`/`fsw`). Per confrontare i cicli:

```bash
gcc -std=gnu11 -Wall -Wextra -O2 -Iinclude src/*.c -o build/vcpu_sim -lm
echo "=== VECTOR ===" && ./build/vcpu_sim standalone/saxpy.vasm
echo "=== SCALAR ===" && ./build/vcpu_sim standalone/saxpy_scalar.vasm
```

Entrambe devono produrre lo stesso risultato numerico
(`12.5 25 37.5 ... 125`), ma con conteggi di cicli diversi.

### 7.3 Prodotto scalare con riduzione (`standalone/dotprod.vasm`)

Il prodotto scalare `r = Σ x[i]·y[i]` mostra la riduzione `vredsum`: `vmul`
calcola i prodotti elemento-per-elemento, `vredsum` collassa il blocco in uno
scalare e `fadd` accumula tra i blocchi.

```asm
loop:
  setvl r4, r3
  vload v0, r1
  vload v1, r2
  vmul  v2, v0, v1      ; prodotti
  vredsum f2, v2        ; somma del blocco -> scalare
  fadd  f1, f1, f2      ; totale corrente
  slli  r5, r4, 2
  add   r1, r1, r5
  add   r2, r2, r5
  sub   r3, r3, r4
  bne   r3, r0, loop
  dumpf f1             ; atteso: 220
  halt
```

```bash
./build/vcpu_sim standalone/dotprod.vasm      # f1 = 220
```

L'operazione complementare `vsplat` (broadcast di uno scalare in tutte le
corsie) è in `standalone/vsplat.vasm`.

### 7.4 Gather / scatter con indici (`standalone/gather.vasm`)

`vloadx`/`vstorex` accedono alla memoria con **indici arbitrari** presi da un
vettore (uno per corsia, in elementi), non con uno stride costante. Servono per
permutazioni, lookup e dati sparsi.

```asm
  li    r1, 4
  setvl r2, r1
  li    r3, gidx
  vload v1, r3          ; indici = [7, 0, 3, 5]
  li    r4, src
  vloadx v0, r4, v1     ; gather: v0 = [src[7], src[0], src[3], src[5]]
  dumpv v0              ; = 17 10 13 15
  li    r5, sidx
  vload v2, r5          ; indici = [3, 2, 1, 0]
  li    r6, dst
  vstorex v0, r6, v2    ; scatter (inverte l'ordine)
  dumpm r6, 4           ; = 15 13 10 17
  halt
```

```bash
./build/vcpu_sim standalone/gather.vasm
```

### 7.5 ReLU con predicazione (`standalone/relu.vasm`)

`y[i] = max(x[i], 0)`. Il confronto `vmsgt` scrive il registro di maschera
`vmask`, e `vmerge` seleziona per corsia: dove la maschera è 1 tiene `x`, altrove
prende 0. È così che una macchina vettoriale realizza gli `if` per corsia.

```asm
  li    r1, 8
  setvl r2, r1
  li    r3, x
  vload v0, r3          ; x
  fli   f0, 0.0
  vsplat v1, f0         ; vettore di zeri
  vmsgt v0, v1          ; vmask[i] = (x[i] > 0)
  dumpmask              ; = 0 1 0 1 0 0 1 0
  vmerge v2, v0, v1     ; v2[i] = mask ? x[i] : 0
  dumpv v2              ; = 0 2 0 5 0 0 7 0
  halt
```

```bash
./build/vcpu_sim standalone/relu.vasm
```

La stessa ReLU si può ottenere direttamente con `vmax v2, v0, v1` (dove `v1` è
il vettore di zeri): `vmerge` è mostrato per illustrare le maschere.

---

### 7.6 Norma euclidea con `fsqrt` (`standalone/norm.vasm`)

`||x|| = sqrt( Σ x[i]² )`. La riduzione `vredsum` accumula i quadrati dei blocchi
e `fsqrt` estrae la radice. È l'esempio che completa l'aritmetica float scalare
(`fsub`/`fdiv`/`fneg`/`fsqrt`).

```asm
  vmul  v1, v0, v0       ; quadrati elemento-per-elemento
  vredsum f2, v1         ; somma del blocco
  fadd  f1, f1, f2       ; totale
  ...
  dumpf f1               ; 385  (somma dei quadrati)
  fsqrt f3, f1
  dumpf f3               ; 19.6214 = sqrt(385)
```

```bash
./build/vcpu_sim standalone/norm.vasm
```

> Nota: `fsqrt` usa `sqrt()` della libreria matematica, quindi il link richiede
> `-lm` (già incluso nel `Makefile` e nei comandi `gcc` di §2.1).

---

### 7.7 Array di interi: `.word` + `lw`/`sw` (`standalone/words.vasm`)

A differenza di `.float`/`flw`/`fsw`, i dati sono **interi con segno a 32 bit**.
`.word` li inizializza, `lw` li carica estendendo il segno e `sw` li scrive
troncando a 32 bit. L'esempio somma un array e salva il risultato in memoria.

```asm
.data
a:   .word 5, -3, 100, 42, -7   ; somma = 137
out: .space 4
.text
  lw    r5, r1            ; carica a[i] (con segno)
  add   r3, r3, r5        ; acc += a[i]
  ...
  sw    r3, r6            ; salva la somma
  lw    r7, r6            ; rileggila
  dumps r7                ; 137
```

```bash
./build/vcpu_sim standalone/words.vasm
```

---

### 7.8 Aritmetica e store predicati (`standalone/masked.vasm`)

Le versioni mascherate (`vaddm`/`vsubm`/`vmulm`/`vstorem`) aggiornano solo le
corsie con `vmask[i] = 1`; le altre restano invariate. Qui si aggiunge `100` ai
soli elementi `> 10` e si memorizzano solo quelli.

```asm
  vmsgt v0, v1             ; mask = (x > 10)  -> 0 1 0 1 0 1 0 0
  vadds v3, v0, f2         ; v3 = copia di x
  vaddm v3, v0, v2         ; +100 solo dove mask=1
  dumpv v3                 ; = 3 112 7 120 1 115 8 2
  vstorem v3, r4           ; scrive solo le corsie attive
  dumpm r4, 8              ; = 0 112 0 120 0 115 0 0
```

```bash
./build/vcpu_sim standalone/masked.vasm
```

### 7.9 Scheduler round-robin preemptive (`standalone/scheduler.vasm`)

L'esempio più completo: uno **scheduler round-robin** con preemption guidata dal
timer. Mostra come le interruzioni (§4.3, «Interruzioni e parola di stato») e
l'indirizzamento base+displacement si combinano per costruire un mini-kernel.

**Strutture dati.** La *ready queue* è una lista circolare doppiamente concatenata
con nodo **sentinella** (il pattern `list_head` del kernel Linux): testa e nodo
condividono i primi 8 byte (`fwd`, `bwd`) a offset 0, così lo stesso codice di
inserimento/rimozione vale per entrambi. Coda vuota: `fwd = bwd = &testa` e
`count = 0`. Ogni task ha un **TCB** (`+0 fwd  +4 bwd  +8 sp_salvato  +12 stato`)
e uno stack proprio; il contatore applicativo del task vive **fuori** dal TCB.

**Tre stati, tre posizioni.** Lo stato di un task coincide con dove sta fisicamente
il suo TCB: `READY` = linkato nella ready queue; `RUNNING` = fuori da ogni coda,
puntato solo dalla variabile globale `current`; `SUSPENDED` = in una coda di
semaforo/mailbox (estensione futura, non usata qui).

**Frame di contesto.** Al momento della preemption si salvano sullo stack del task
**solo gli scalari** — `r1..r13`, `r15` (ra) ed `epc` — mentre `r14` (sp) finisce
nel TCB. I registri **vettoriali** (`v*`, `vl`, `vmask`) sono per convenzione
*volatili* attraverso un punto di preemption: dato che la trap arriva solo al
confine d'istruzione e i vettori vivono solo dentro sequenze
`vload → calcola → vstore` già riversate in memoria, non c'è nulla di vivo da
salvare. È la stessa scelta della ABI vettoriale di RISC-V (vettori caller-saved).

**Scheduler e dispatcher separati.** La ISR del timer fa tre cose: (A) salva il
contesto del task uscente sul suo stack e ne aggiorna `sp` nel TCB; (C) rimette il
task in fondo alla ready queue (`enqueue_tail`); poi invoca lo **scheduler**
(*politica*: `dequeue_head` sceglie il prossimo) e il **dispatcher**
(*meccanismo*: ricarica `sp`, ripristina i registri, `mtepc`, `reti`).

**Avvio del primo task.** `reti` esegue `psw = epsw`, e non esiste un modo per
scrivere direttamente `epsw`: l'unico che lo imposta è la trap (che vi copia la
`psw` corrente). Ma per il *primo* task non serve `reti`: un task a freddo non ha
un contesto da ripristinare. `main` costruisce le strutture, mette il primo task
in `RUNNING` (`current`, fuori dalla coda), arma il timer, abilita gli interrupt
con `sti` (la `psw` **è** scrivibile) e **salta** al suo entry con `jr`. Al primo
tick la trap salva `epsw = IE` dal task in corso, così il secondo task — che parte
via `reti` dal frame primato — riceve la `psw` corretta. Solo i task che
*ripartono* dopo una preemption passano dal dispatcher e da `reti`; è la stessa
asimmetria (cold start diretto vs. resume via eccezione) di molti kernel reali.

I due task incrementano ciascuno il proprio contatore in un loop infinito; dopo 8
tick lo scheduler stampa i contatori e ferma la macchina:

```bash
./build/vcpu_sim standalone/scheduler.vasm
# r5 = 124   (tickA)
# r5 = 99    (tickB)
```

Entrambi i contatori sono cresciuti: i task si sono davvero alternati. Con
`--trace` si vedono gli 8 `-- timer trap -> handler N` e il passaggio del controllo
dall'uno all'altro. È un esempio a **file singolo** che tiene *tutto* insieme
(kernel e applicazione) come riferimento didattico compatto; la versione §7.10 lo
spezza in kernel riutilizzabile + demo e mostra un confine più realistico.

### 7.10 HAL, kernel puro e preemption differita (`hal/` + `generic/queue/` + `rtos/`)

Il mini-kernel di §7.9 mescola software di base e applicazione in un solo file.
Ora che i **puntatori a funzione** attraversano la toolchain (rilocazione
`R_ADDR`, §2.5), possiamo **spezzarlo e linkarlo** in tre strati netti, come in un
sistema reale (l'*arch/port* di Linux e FreeRTOS rispetto al core portabile):

| File | Strato | Ruolo | Esporta |
|------|--------|-------|---------|
| `hal/impl/src/machine.vasm` | **HAL** (hardware) | vettore di trap, save/restore contesto (`ctx_save`/`ctx_restore`), timer, `sti`, sezioni critiche | `_trap_entry`, `ctx_restore`, `ctx_init`, `timer_init`, `irq_arm`, `irq_enable`, `irq_save`, `irq_restore` |
| `generic/queue/impl/src/queue.vasm` | generic | le 5 routine di coda (`list_head`) | `queue_init`, `enqueue_tail`, `enqueue_head`, `dequeue_head`, `remove_buffer` |
| `rtos/scheduler/impl/src/scheduler.vasm` | **kernel puro** | orchestrazione + politica RR + dispatch, **nessun CSR** | `sched_dispatch`, `irq_install`, `request_preempt`, `ready`, `current` |
| `rtos/demo/scheduler_demo.vasm` | applicazione | boot (`main`) + due task + `timer_isr` + dati | `main` |

**HAL: l'unico strato che tocca l'hardware.** Il *vettore grezzo* di trap
(`_trap_entry`), il salvataggio/ripristino dei registri (`ctx_save`/
`ctx_restore`), i CSR delle eccezioni (`mfepc`/`mtepc`/`reti`) e i primitivi del
timer (`timer_init`, `irq_arm`, `irq_enable`) vivono qui. L'HAL conosce il file
dei registri e il layout del frame; **non** conosce code, TCB, ISR né politica —
chiama sempre e solo un unico simbolo kernel fisso (`sched_dispatch`), senza
sapere cosa fa. Il contesto salvato di un task è per il kernel un **puntatore
opaco** (`sp`): l'HAL lo costruisce (`ctx_save`), lo passa al kernel e riceve
indietro quello da riprendere (`ctx_restore`, che non torna mai: chiude con
`reti`).

`_trap_entry` contiene una sola istruzione scritta a mano invece che dentro
`ctx_save`: il push di r15 **prima** di qualunque `call`. `call` è sempre
`jal r15, target` (link register cablato nell'assembler): alla primissima
chiamata di un trap handler, r15 contiene ancora il valore *live* del task
interrotto, non ancora salvato — se la call lo sovrascrivesse per prima, quel
valore andrebbe perso. È un vincolo strutturale della ISA (qualunque registro
si scegliesse come link register avrebbe lo stesso problema), non un'eccezione
di stile: per questo resta l'unico frammento non delegato a `ctx_save`.

**Kernel puro, tre responsabilità nette.** `rtos/scheduler/impl/src/scheduler.vasm`
non contiene **nessuna** istruzione hardware, ed è a sua volta stratificato in tre
routine che non si mischiano:

- **`sched_dispatch`** (orchestratore — meccanismo puro): unico punto di
  contatto con l'HAL. Registra lo `sp` uscente nel TCB, chiama l'**handler**
  dell'app (via `jalr` — un vero puntatore a funzione), e **solo se è stata
  richiesta** una preemption consulta `scheduler`. Chiama **sempre** `dispatcher`
  per rimettere in esecuzione `current` — con o senza switch è l'unico modo di
  uscire dalla trap.
- **`scheduler`** (chi è il prossimo — la politica): round-robin a coda
  singola (`enqueue_tail` dell'uscente, `dequeue_head` del prossimo). Cambiare
  politica (RR → priorità) significa riscrivere **solo `scheduler`**: HAL,
  `sched_dispatch` e `dispatcher` restano intatti.
- **`dispatcher`** (mette in esecuzione — meccanismo puro): chiama l'HAL
  (`ctx_restore`) sullo `sp` corrente di `current` e non ritorna mai.

`sched_dispatch`, `scheduler` e `dispatcher` non sono API generiche — hanno un
solo chiamante per costruzione — quindi restano scritte a mano invece che con
la sugar `.proc`/`.endproc` (§4.2), riservata a procedure con un contratto
stabile e riusabile.

**Confine app.** L'applicazione possiede l'**handler** (`timer_isr`, registrato con
`irq_install`) e la **decisione** di preemptare. L'handler non commuta il task: se
vuole uno switch chiama `request_preempt`, che arma `g_resched` (il `need_resched`
di Linux, lo `xHigherPriorityTaskWoken` di FreeRTOS). Solo `sched_dispatch`, a
uscita IRQ, guarda il flag: se è zero riprende lo *stesso* task, altrimenti chiama
`scheduler`. Un handler che *non* chiama `request_preempt` non causa alcuno
switch — è una scelta dell'architetto. Il `main` costruisce i contesti iniziali
con `ctx_init` (l'HAL sa com'è fatto un frame) e arma l'hardware con
`irq_arm`/`timer_init`/`irq_enable`, senza mai nominare un CSR o il vettore.

La pipeline di compilazione separata (o, in alternativa, un archivio `libkernel.va`
con inclusione selettiva):

```bash
# Un -I per libreria: e' la cartella interface/ che ogni libreria pubblica, e
# il nome incluso porta il nome della libreria ("coda/queue.vinc", "tcb/tcb.vinc").
Ihal=-Ihal/interface
Icoda=-Igeneric/coda/interface
Itcb="-Irtos/scheduler/interface $Icoda"      # tcb.vinc include coda/queue.vinc
./build/vcpu_sim asm $Ihal hal/impl/src/machine.vasm           -o build/machine.vo
./build/vcpu_sim asm $Icoda generic/queue/impl/src/queue.vasm    -o build/coda.vo
./build/vcpu_sim asm $Itcb rtos/scheduler/impl/src/scheduler.vasm -o build/scheduler.vo
./build/vcpu_sim asm $Itcb rtos/demo/scheduler_demo.vasm       -o build/scheduler_demo.vo
./build/vcpu_sim ld build/scheduler_demo.vo build/scheduler.vo build/coda.vo \
                    build/machine.vo -o build/scheduler_demo.vx
./build/vcpu_sim run build/scheduler_demo.vx
# r5 = 97    (tickA)
# r5 = 64    (tickB)
```

Gli 8 tick e i due contatori che crescono alternandosi sono le stesse invarianti
*qualitative* di §7.9; i numeri assoluti cambiano (call e `jalr` in più per i
confini tra gli strati). La separazione *hardware / kernel / applicazione* è il
punto: ogni strato ignora i dettagli degli altri.

**Sezioni critiche sulle code.** Le routine di coda fanno aggiornamenti
multi-passo **non atomici**. Finché ogni chiamata avviene a interrupt disabilitati
(il boot prima di `irq_enable`, l'ISR dentro la trap) non c'è corsa. Ma un *task*
gira a `IE=1`: se il timer si interpone a metà di un `enqueue_tail`, la lista resta
incoerente e `scheduler` la corrompe. Su un monoprocessore in-order la sezione
critica è semplicemente **disabilitare gli interrupt** (una `fence` non darebbe
atomicità: servirebbe con multicore + RMW atomica, che questa ISA non ha).

Da qui il **pattern a due livelli** (lo `xQueueSend`/`xQueueSendFromISR` di
FreeRTOS, `__list_add` di Linux): la primitiva **raw** manipola i dati, è
lock-free e chiamabile da ISR (già a `IE=0`); un **wrapper protetto** col suffisso
`_s` la racchiude in una sezione critica. L'HAL fornisce la coppia componibile
`irq_save() → r5 = psw; IE=0` e `irq_restore(r5)`: usa **save/restore** e non
`cli`/`sti` secco, così resta corretta anche annidata o con `IE` già a zero. I
wrapper (`enqueue_tail_s`, `enqueue_head_s`, `dequeue_head_s`, `remove_buffer_s`)
sono NON-FOGLIA: salvano `r15` e conservano la `psw` sullo stack attraverso la
chiamata alla raw. `irq_save`/`irq_restore` usano `r5` e non toccano `r1`/`r2`,
quindi gli argomenti (e il valore di ritorno di `dequeue_head`) restano intatti.
Le ISR e il boot usano le raw; il **task** usa i wrapper `_s`.

### 7.11 La FFT sul blocco dell'ADC (`dsp/fft/` + `dsp/test/test_fft.vasm`)

Una FFT complessa a 64 punti, radix 2, **decimation in time** di Cooley–Tukey:
ingresso in ordine bit-reversed, uscita in ordine naturale. È una libreria,
`lib_fft`, e il contratto sta in `dsp/fft/interface/fft/fft.vinc`:

```asm
  li   r1, blocco          ; 64 campioni dell'ADC, I e Q alternati, interi
  li   r2, re              ; re[64]: float, li scrive la FFT
  li   r3, im              ; im[64]
  call fft64               ; X[k] in ordine naturale, senza normalizzazione
```

I buffer d'uscita sono del chiamante, e un fasore di ampiezza A con k giri
interi nel blocco dà `|X[k]| = A·64`. La trasformata è la diretta, quindi una
frequenza negativa cade in `64 − k`. Costa **7.663 cicli** dal `call` al
ritorno, misurati con `--trace`: il 7,7% di un millisecondo.

Tre cose che la macchina impone, e che valgono per qualunque algoritmo che
permuti dati fra corsie:

- **il bit reversal è un gather.** Una `vloadx` sul blocco con gli indici
  `2·rev6(i)` separa le I dalle Q (il 2 salta la parola della Q) e le mette in
  ordine bit-reversed in un colpo; sulle Q la stessa tabella con la base a +4.
  Poi `vcvt`. I DSP classici hanno l'indirizzamento bit-reversed nel
  generatore di indirizzi; qui è una tabella, e costa 64 cicli per canale in
  più della `vload` a passo 8 che sostituisce;
- **ogni stadio passa dalla memoria.** La farfalla accoppia la corsia `i` con
  la `i+h`, e l'ISA non ha permutazioni dentro un registro: le fanno solo
  gather e scatter. Ogni stadio raccoglie alti e bassi, calcola, e riscrive
  sugli stessi indici, in place — si può perché dentro uno stadio ogni lettura
  di un array precede le sue scritture;
- **gli indici sono float.** `vloadx`/`vstorex` troncano a intero il valore
  float della corsia (§7.4), quindi anche le tabelle di indici sono `.float`.

Le tabelle (bit reversal, indici alti per stadio, twiddle per stadio) le genera
`tools/fft_tables.py` in `dsp/fft/impl/src/fft_tables.vinc`, che si committa e
non si modifica a mano. Il test gira due volte, `--adc 78125,1000` e
`--adc -78125,1000`: con `ADC_PERIOD = 100` un bin vale 15.625 Hz e 78.125 Hz
sono cinque bin esatti, quindi `5 64 0` e `59 64 0` — il bin del picco, il
picco in migliaia, il massimo degli altri bin in migliaia.

---


## 8. Tutorial: il tuo primo programma

In questo tutorial costruiamo passo dopo passo una **somma vettoriale**
elemento-per-elemento, `C = A + B`. Il programma finale è in
`standalone/tutorial.vasm`.

### Passo 0 — l'obiettivo

Dati due array di 5 float, vogliamo calcolare:

```
C[i] = A[i] + B[i]   per i = 0..4
```

con una sola operazione vettoriale invece di un ciclo scalare.

### Passo 1 — dichiarare i dati

Mettiamo gli array in memoria con la sezione `.data`. Le label catturano
l'indirizzo; `.space` riserva spazio non inizializzato per il risultato.

```asm
.data
A:  .float 1, 2, 3, 4, 5
B:  .float 10, 20, 30, 40, 50
C:  .space 5                 ; 5 elementi = 20 byte per il risultato
```

Dopo questo blocco: `A = 0`, `B = 20`, `C = 40` (indirizzi in byte, 4 byte per
elemento).

### Passo 2 — caricare i puntatori e il conteggio

Nella sezione `.text` mettiamo gli indirizzi nei registri scalari. `li` accetta
un simbolo e lo risolve al suo indirizzo.

```asm
.text
  li    r1, A          ; &A
  li    r2, B          ; &B
  li    r3, C          ; &C
  li    r4, 5          ; numero di elementi
```

### Passo 3 — impostare la Vector Length

Le operazioni vettoriali lavorano su `VL` elementi: dobbiamo impostarlo **prima**
di usarle. `setvl` satura a `VLMAX` (64) e ci restituisce il valore effettivo.

```asm
  setvl r5, r4         ; VL = min(5, 64) = 5, e r5 = 5
```

### Passo 4 — caricare, calcolare, salvare

Carichiamo i due array nei registri vettoriali, sommiamo e scriviamo il
risultato in memoria. Ogni istruzione tocca esattamente `VL` elementi.

```asm
  vload  v0, r1        ; v0 = A[0..4]
  vload  v1, r2        ; v1 = B[0..4]
  vadd   v2, v0, v1    ; v2 = A + B  (5 addizioni in una sola istruzione)
  vstore v2, r3        ; C = v2
```

### Passo 5 — ispezionare il risultato e fermarsi

```asm
  dumpm r3, 5          ; stampa 5 float a partire da &C
  halt
```

### Passo 6 — eseguire

```bash
./build/vcpu_sim standalone/tutorial.vasm
```

Output atteso:

```
mem[r3=0x28] = 11 22 33 44 55
---- stats ----
instructions executed : 11
vector element ops     : 20
cycles (timing model)  : 68
```

`0x28` è 40 in esadecimale, cioè l'indirizzo di `C`. I `20` "vector element ops"
sono le 4 istruzioni vettoriali (`vload`, `vload`, `vadd`, `vstore`) × VL=5.

### Passo 7 — e se ho più di 64 elementi?

Un registro vettoriale contiene al massimo `VLMAX = 64` elementi. Per array più
lunghi si usa lo **strip-mining**: un ciclo che elabora un blocco alla volta,
facendo ricalcolare `VL` a ogni giro con `setvl` (che satura a 64) e avanzando i
puntatori di `VL * 4` byte.

```asm
loop:
  setvl r5, r4         ; VL = min(elementi_rimasti, 64)
  vload  v0, r1
  vload  v1, r2
  vadd   v2, v0, v1
  vstore v2, r3
  slli   r6, r5, 2      ; byte processati = VL * 4
  add    r1, r1, r6     ; avanza &A
  add    r2, r2, r6     ; avanza &B
  add    r3, r3, r6     ; avanza &C
  sub    r4, r4, r5     ; elementi_rimasti -= VL
  bne    r4, r0, loop   ; ripeti finché restano elementi
```

È lo stesso schema usato in `standalone/saxpy.vasm`: cambia solo l'operazione
interna (`vadd` invece di `vmacc`).

### Esercizi

1. Modifica il tutorial per calcolare `C = A - B` (`vsub`).
2. Calcola `C = 3.0 * A + B`: usa `fli` per caricare la costante e `vmacc`.
3. Porta gli array a 100 elementi (`.float` più lunghi o `.space` + inizializza)
   e applica lo strip-mining del Passo 7. Confronta i cicli con una versione
   scalare che usa `flw`/`fadd`/`fsw`.
