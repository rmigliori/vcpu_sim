# Stato dei lavori — `vcpu_sim`

> Ultimo aggiornamento: **27 agosto 2026**
> Scopo: fotografia dello stato per riprendere il lavoro a distanza di giorni
> senza dover ricostruire il contesto.

---

## 1. Dov'è il progetto adesso

Simulatore didattico di CPU vettoriale (stile RISC-V "V") in C, con toolchain
completa a compilazione separata. **Tutto quello che c'è funziona ed è
verificato.** Manca un solo pezzo del disegno complessivo: il linguaggio ad alto
livello.

| Strato | Stato | File |
|---|---|---|
| CPU + ISA vettoriale | completo | [`src/vcpu.c`](../src/vcpu.c), [`include/vcpu.h`](../include/vcpu.h) |
| Assembler (2 passi, rilocazioni) | completo | [`src/assembler.c`](../src/assembler.c) |
| Linker, archivi, loader | completo | [`src/toolchain.c`](../src/toolchain.c) |
| CLI `asm/ld/run/nm/ar` | completo | [`src/main.c`](../src/main.c) |
| HAL + kernel + scheduler RR | completo, **non committato** | [`linked/scheduler/`](../linked/scheduler/) |
| Linguaggio alto livello `vc` | **da fare** — solo progettato | [`docs/proposta-linguaggio-alto-livello.md`](proposta-linguaggio-alto-livello.md) |

Macchina: 16 registri scalari `r0..r15` (`r0` = 0), 16 float `f0..f15`, 8
vettoriali `v0..v7` con `VLMAX=64`, `vmask` a 64 bit, 1 MiB byte-addressable.
Timing: scalare a latenza fissa, vettoriale `startup + ceil(VL/VEC_LANES)`.

**Riorganizzazione dei sorgenti `.vasm` (27/08/2026):** la vecchia `examples/`
era un misto di programmi a file singolo e progetti multi-modulo. Ora sono
separati per contratto d'uso:

- [`standalone/`](../standalone/) — programmi eseguibili da soli con
  `./build/vcpu_sim FILE.vasm` (modalità legacy a file singolo). Include il
  vecchio monolite `scheduler.vasm` (§3.2, §7.9 del manuale).
- [`linked/`](../linked/) — progetti che richiedono `asm` + `ld` su più moduli:
  - `linked/scheduler/` — lo scheduler a strati (`hal/`, `kernel/`,
    `include/types.vinc`, `scheduler_demo.vasm`), descritto in §3.2 e §7.10.
  - `linked/multi/` — demo minimale di link fra due moduli con eliminazione di
    codice morto (`main.vasm` + `saxpy.vasm` + `unused.vasm`).

Nessun path era hardcoded nel codice C (`src/`), quindi lo spostamento non ha
toccato la toolchain — solo `Makefile` e i tre documenti in `docs/`, aggiornati
di conseguenza. Le tre invarianti di regressione (§4) sono state riverificate
dopo lo spostamento e danno gli stessi numeri di prima.

---

## 2. Git: dove siamo

Branch `master`. Ultimo commit:

```
007c933 Toolchain: reloc R_ADDR per puntatori a funzione (li di simbolo)
10ae28a Snapshot iniziale: simulatore vCPU vettoriale + toolchain + scheduler RR
```

**Lavoro non committato** (tutto funzionante e verificato):

```
 M Makefile                        (target run: standalone/saxpy.vasm)
 M docs/manual.md                  (+129 righe: doc di .equ/.struct/.field/.res/.include, §7.10,
                                     path aggiornati dopo la riorganizzazione examples/ -> linked/ e standalone/)
 M docs/proposta-linguaggio-alto-livello.md  (path aggiornati)
 M src/assembler.c                 (+228 righe: direttive di compile-time, .include, fix warning)
 R examples/*.vasm -> standalone/*.vasm       (12 file, rename puro)
 R examples/multi/*.vasm -> linked/multi/*.vasm (3 file, rename puro)
?? linked/scheduler/               hal/, kernel/, include/types.vinc, scheduler_demo.vasm
                                    (spostati da hal/, kernel/, include/, examples/ — mai committati)
?? .vscode/              (probabilmente da mettere in .gitignore)
```

---

## 3. Cosa è stato fatto nell'ultima sessione

### 3.1 Astrazione a compile-time nell'assembler

Nuove direttive, documentate in [`docs/manual.md` §4.2](manual.md):

- `.equ` / `.set` — costanti intere di compile-time
- `.struct` / `.field` / `.ends` — offset simbolici come costanti `NOME.campo`,
  più `NOME.size` generata da `.ends`
- `.res TIPO` — riserva `TIPO.size` byte (una `.space` *type-aware*)
- `.include "file"` — inclusione testuale, path relativo alla cartella del file
  di primo livello, stack di 8 file

Scelta di progetto importante: le costanti vivono in un **namespace separato
dalle label**, quindi non possono mai essere scambiate per simboli rilocabili.

### 3.2 Stratificazione HAL / kernel puro

Rifattorizzazione del monolite [`standalone/scheduler.vasm`](../standalone/scheduler.vasm)
in tre strati con contratto netto:

- [`linked/scheduler/hal/machine.vasm`](../linked/scheduler/hal/machine.vasm) — **unico** codice che tocca l'hardware:
  `_trap_entry` (frame di contesto da 60 byte), `ctx_init` (frame finto, come
  `pxPortInitialiseStack` di FreeRTOS), `timer_init`/`irq_arm`/`irq_enable`, e
  `irq_save`/`irq_restore` a coppia — **componibili**, quindi corretti anche annidati.
- [`linked/scheduler/kernel/coda.vasm`](../linked/scheduler/kernel/coda.vasm) — 5 primitive `list_head` con unlink
  O(1), più le varianti `_s` protette da sezione critica.
- [`linked/scheduler/kernel/scheduler.vasm`](../linked/scheduler/kernel/scheduler.vasm) — kernel **puro**: politica
  round-robin + **preemption differita** (`need_resched` di Linux /
  `xHigherPriorityTaskWoken` di FreeRTOS). Vede il contesto come puntatore
  **opaco**, non tocca un solo CSR.

Separazione mechanism/policy reale: si passa da RR a priorità riscrivendo solo
`ctx_pick`, senza toccare HAL né `sched_dispatch`.

> `standalone/scheduler.vasm` (il monolite originale) resta nel repo come
> riferimento didattico "prima" — vedi §7.9 vs §7.10 del manuale.

### 3.3 Fix del warning di compilazione (ultima cosa fatta)

[`src/assembler.c:1228-1239`](../src/assembler.c#L1228-L1239) — sostituito il
`realloc` di shrink dell'immagine dati con malloc esatto + `memcpy` + `free`:

```c
obj->data = malloc((size_t) data_ptr);
if (!obj->data) { snprintf(err, errsz, "out of memory"); goto fail; }
memcpy(obj->data, data, (size_t) data_ptr);
free(data);
```

Due ragioni, non solo il silenziamento del warning:

1. Il vecchio `if (!obj->data) obj->data = data;` leggeva `data` dopo che
   `realloc` l'aveva consumato → undefined behaviour.
2. Il fallback era **peggio** del fallimento: il buffer di scratch è un
   `malloc(MEM_SIZE)` = **1 MiB**, mentre le immagini dati reali sono decine di
   byte. Tenerlo su shrink fallito tratteneva 1 MiB per oggetto proprio in
   condizioni di memoria esaurita. Ora l'OOM è un errore vero; il salto a `fail:`
   è sicuro perché quel path fa già `free(data)` e `obj` è azzerato con `memset`
   all'ingresso.

**Risultato: build completamente pulita, zero warning.**

---

## 4. Invarianti di regressione — come verificare che nulla si sia rotto

```bash
make                                    # deve compilare SENZA warning

# (1) invariante saxpy: 17 istruzioni / 40 vec-elem-ops / 94 cicli
./build/vcpu_sim standalone/saxpy.vasm

# (2) demo HAL+kernel: deve stampare r5 = 102 e r5 = 70
./build/vcpu_sim asm linked/scheduler/hal/machine.vasm      -o build/machine.vo
./build/vcpu_sim asm linked/scheduler/kernel/coda.vasm      -o build/coda.vo
./build/vcpu_sim asm linked/scheduler/kernel/scheduler.vasm -o build/scheduler.vo
./build/vcpu_sim asm linked/scheduler/scheduler_demo.vasm   -o build/scheduler_demo.vo
./build/vcpu_sim ld build/scheduler_demo.vo build/scheduler.vo \
                    build/coda.vo build/machine.vo -o build/scheduler_demo.vx
./build/vcpu_sim run build/scheduler_demo.vx

# (3) link multi-modulo con inclusione selettiva: 18 / 40 / 95
./build/vcpu_sim asm linked/multi/main.vasm  -o build/mainc.vo
./build/vcpu_sim asm linked/multi/saxpy.vasm -o build/msaxpy.vo
./build/vcpu_sim ld build/mainc.vo build/msaxpy.vo -o build/multi.vx
./build/vcpu_sim run build/multi.vx
```

Stato verificato il 27/08/2026 (dopo la riorganizzazione `examples/` →
`linked/`/`standalone/`): **tutti e tre passano con gli stessi identici numeri**,
più `asm` pulito su tutti i 20 sorgenti di `standalone/`, `linked/multi/`,
`linked/scheduler/`.

> Nota: `linked/scheduler/scheduler_demo.vasm` **fallisce di proposito** in
> modalità legacy a file singolo (`./build/vcpu_sim linked/scheduler/scheduler_demo.vasm`),
> perché ha `.extern ready`: va necessariamente linkato. Non è una regressione.

---

## 5. Prossimi passi possibili

### Opzione A — committare il lavoro pendente
È tutto completo, documentato e verificato, ma vive fuori da git (o non ancora
staged). Da valutare se aggiungere `.vscode/` a `.gitignore`. Commit suggeriti
(tre, separati per tema — in quest'ordine, perché il terzo dipende dai path
introdotti dal secondo):
1. assembler: direttive di compile-time (`.equ`/`.struct`/`.field`/`.res`/`.include`) + fix shrink immagine dati (`git add docs/manual.md src/assembler.c`)
2. HAL + kernel puro + demo scheduler a preemption differita (`git add linked/scheduler/`)
3. riorganizzazione sorgenti `.vasm`: `examples/` → `linked/`/`standalone/`, con
   `Makefile`, `docs/manual.md` e `docs/proposta-linguaggio-alto-livello.md`
   aggiornati di conseguenza — è un `git mv` puro (rename) più i fix di path,
   nessuna modifica di contenuto ai singoli `.vasm` spostati (a parte il fix del
   `.include` in `scheduler_demo.vasm` per la nuova profondità)

### Opzione B — front-end `vc` (il pezzo mancante)
Progetto già completo in
[`docs/proposta-linguaggio-alto-livello.md`](proposta-linguaggio-alto-livello.md):
linguaggio array-first alla Fortran 90/NumPy, EBNF, tabella di precedenze, 5 fasi.
Il compilatore emette `.vasm` → **nessun backend nuovo, solo un front-end**.

Primo passo minimo proposto nel documento: parser + **un solo costrutto**
(`a[:] = espr` con `+ - *` elementwise) che genera `.vasm`.
Criterio di successo: il `saxpy` scritto in `vc` deve riprodurre
**17 istruzioni / 40 vec-elem-ops / 94 cicli**, cioè lo stesso conteggio del
`saxpy.vasm` scritto a mano.

Decisione già presa nel documento (§6): array come **coppia (puntatore,
lunghezza)** a runtime, non lunghezza statica.

---

## 6. Come far ripartire Claude

Aprire Claude Code nella cartella del progetto e scrivere una di queste:

**Per riprendere in generale:**
```
Leggi docs/stato-lavori.md e riprendi da lì.
```

**Per andare sul linguaggio ad alto livello:**
```
Leggi docs/stato-lavori.md e docs/proposta-linguaggio-alto-livello.md.
Implementa la fase 1 del front-end vc: lexer + parser + il costrutto
a[:] = espr con + - * elementwise, che genera .vasm.
Criterio di successo: saxpy in vc deve dare 17 istruzioni / 40 vec-elem-ops / 94 cicli.
```

**Per committare il lavoro pendente:**
```
Leggi docs/stato-lavori.md e fai i commit descritti nell'Opzione A della sezione 5.
```

Utile da sapere: il modello si cambia con `/model` (questa sessione girava su
Opus 5). Il contesto del progetto si ricostruisce in fretta perché il repo è
piccolo (~6.500 righe totali) e i tre documenti in `docs/` sono aggiornati.
