# Stato dei lavori — `vcpu_sim`

> Ultimo aggiornamento: **5 settembre 2026**
> Scopo: fotografia dello stato per riprendere il lavoro a distanza di giorni
> senza dover ricostruire il contesto.

---

## 0. STATO ATTUALE — LAVORO NON COMMITTATO NEL WORKING TREE

**RIPRENDI DA QUI.** Il **30/08/2026** ci sono state **tre sessioni**, non una:
§3.9 (il TCB e i timeout), §3.10 (la mailbox, progettata e implementata) e §3.11
(il gestore dei timeout, riprogettato da capo). Il documento da leggere resta
[`docs/proposta-kernel-realtime.md`](proposta-kernel-realtime.md), ora coerente:
§8 per la mailbox, **§9 per il gestore dei timeout**.

> ### ⚠ La §9 è andata quasi persa, e la lezione vale più del testo
>
> La sessione di §3.11 aveva scritto la nuova §9 in un file di **scratchpad** e
> l'aveva innestata nel documento con uno script. Il documento era però aperto
> nell'IDE, e un salvataggio successivo ha rimesso sul disco il **buffer
> precedente**: la §9 vecchia è tornata, lo scratchpad è stato cancellato con la
> sessione, e la sessione dopo ha riproposto all'utente il modello già bocciato.
> Il testo è stato **recuperato dalla trascrizione** in
> `~/.claude/projects/-home-roberto-migliori-sandBox-vcpu-sim/*.jsonl`, che
> conserva per intero i `tool_use` di ogni sessione.
>
> **Regola operativa: non tenere aperto nell'editor un file che la sessione sta
> modificando**, e a fine sessione verificare con `git diff --stat` che le
> modifiche annunciate siano davvero sul disco.

**Stato del codice del kernel: tutto scritto, verificato e COMMITTATO** (§2).
Quattro pezzi, nell'ordine in cui sono nati:

| Pezzo | Dove | §  |
|---|---|---|
| Mailbox: `send`/`send_s`/`receive`, contatore con segno, strato `_nc` | `kernel/messageHandling.vasm`, `kernel/coda.vasm` | §3.10 |
| Gestore dei timeout: vettore di descrittori, `timeout_arm`/`timeout_cancel` | `kernel/timeout.vasm`, `include/timeout.vinc` | §3.13 |
| Invariante dei link nelle code + esito in `r3`, e il puntatore nullo imposto dalla toolchain | `kernel/coda.vasm`, `include/vcpu.h`, `src/` | §3.14 |
| Pool di buffer, sei classi dimensionate 10/4/0/0/0/0 | `kernel/pool.vasm`, `include/pool.vinc` | §3.15 |

A cui si aggiunge, dal 05/09/2026 e su un fronte diverso (il build), `-I`
nell'assembler e `.include` idempotente — §3.16, committato in `21c9305`.

Cinque test mirati in `tests/`, tutti verdi, elencati in §4.

**Tutto quello che non dipendeva dallo scheduler è fatto**: mailbox, vettore di
descrittori, invariante dei link, pool. Quello che resta del gestore dei timeout
— la scansione delle scadenze e il ciclo del task — vuole la commutazione
volontaria (§8.7) e lo scheduler a priorità (§7.4).

**RIPRENDI DA §12 DELLA PROPOSTA**, la revisione del confine HAL/ISR/kernel
(§3.18). Non dipende da §7.4. Il **passo 1 di §12.6 è fatto** — `mfepsw`/`mtepsw`
sono nell'ISA (§3.19), invarianti immobili e `.vo` identici byte per byte. Il
prossimo è il **passo 2**: la parola di stato nel frame (60 → 64 byte) e
`hal.vinc` con `CTX_FRAME_SIZE` e `PSW_IE`.

Le altre due strade, entrambe in §5:

1. **§7.4**, la decisione ferma: ereditarietà di priorità o priority ceiling per
   i mutex. È quella che sblocca tutto il resto del kernel.
2. ~~La ristrutturazione del build in target CMake~~ — **FATTA il 05/09/2026**
   salvo il punto 3, che non blocca niente. Vedi il riquadro qui sotto.
3. **La revisione del confine HAL/ISR/kernel — §12 della proposta**: è da qui
   che riparte il codice, e non dipende da §7.4. Il passo 1 di §12.6 è **fatto**
   (§3.19); restano il frame a 64 byte con `hal.vinc`, il percorso di trap
   nuovo — che **sposterà l'invariante (2)** — e le librerie.

> ### 05/09/2026 — il build è in target CMake, invarianti immobili
>
> Tre punti su quattro in una giornata: `-I` nell'assembler e `.include`
> idempotente (**§3.16**), poi le 12 `.include` a nome nudo e i target CMake con
> `ctest` (**§3.17**). Da ora:
>
> ```bash
> cmake -B out -S . && cmake --build out -j && ctest --test-dir out
> ```
>
> 22 test verdi. La suite di regressione non è più un commento e la disciplina di
> chi lo esegue — **quello era il premio, più dei path**. Il `Makefile` è intatto
> e i due build convivono; ritirarlo è una decisione a parte, non forzata da qui.
>
> Le invarianti di §4 danno gli **stessi identici numeri**, e la migrazione è
> fedele fino agli artefatti: i `.vo` prodotti da CMake sono identici byte per
> byte a quelli delle pipeline a mano. L'unica eccezione consapevole è il layout
> di tre `.vx` che ora linkano l'archivio — stesse sequenze, ordine dei moduli
> diverso: il riquadro in §3.17 dice quali e perché.
>
> Resta aperto il solo punto 3, `--emit-deps`: le dipendenze oggi sono
> *dichiarate* nel `CMakeLists.txt`, non *scoperte* dall'assembler.

> Le due domande che questo paragrafo poneva prima di §3.11 — se la risposta
> riusa lo stesso buffer, e dove sta il modulo di protocollo — restano
> **superate, non risolte**: nel modello nuovo il cliente non fornisce nessun
> buffer e non c'è nessuna risposta da girare. Il modulo di protocollo generico
> serve quando ci sarà un fornitore vero a richiesta/risposta.

Sullo **scheduler** invece non è cambiato nulla e non si scrive codice: **quattro
decisioni prese** (§7.1–7.3 e §7.5), **una aperta**, §7.4 — ereditarietà di
priorità contro priority ceiling per i mutex — che cambia il layout di strutture
statiche. Restano le due domande minori sul TCB elencate in §5, ma una si è
chiusa da sé: `ctx_init` **non** è eliminabile (§8.7 della proposta).

**Reindentazione a 2 spazi (29/08/2026), FATTA E COMMITTATA** (`ae29292`,
vedi §3.7): `src/*.c` e `include/*.h` sono passati da 4 a 2 spazi, per
allinearsi ai `.vasm` che erano già a 2. Cambiamento puramente cosmetico,
verificato con `git diff -w` vuoto e checksum identici degli artefatti
generati. **Da qui in avanti il codice C si scrive a 2 spazi.**

**Incidente tmpfs/quota (28/08/2026), RISOLTO:** una sessione precedente aveva
fatto fallire `gcc` con `fatal error: error writing to /tmp/...: Quota disco
superata` (`/tmp` è tmpfs con `usrquota`, quindi vive in RAM), dopodiché anche
il tool Bash aveva smesso di rispondere (serve scratch su `/tmp` pure lui) e
l'utente aveva dovuto riavviare la macchina. Verificato alla ripresa (`df -h
/tmp`: 1% usato) e non più ripresentato in questa sessione. Nessuna mitigazione
applicata (TMPDIR dentro il progetto): non è servita. Riproporla solo se
l'incidente si ripete.

**Auto-save registri in `.proc`/`.endproc` + `--emit-expanded` (§3.6):
implementato, compilato, VERIFICATO, COMMITTATO** (commit `ae310d5` —
`Ridisegno HAL/kernel a tre confini + auto-save registri in .proc/.endproc`,
vedi §2). Checklist di verifica di §3.6 eseguita per intero, tutto torna:

```
make                                             # pulito, zero warning — OK
./build/vcpu_sim standalone/saxpy.vasm           # 17/40/94 — invariata (1)
asm+ld+run di linked/scheduler/*                 # r5 = 105 / r5 = 74 — invariata (2)
asm di tutti i 20 sorgenti .vasm (standalone+linked) — tutti OK — invariata (3)
tests/test_proc.vasm (NUOVO, committato)         # r1=100/r7=200/r8=300 — OK
--emit-expanded su tests/test_proc.vasm          # prologo/epilogo combaciano
```

`tests/test_proc.vasm`: `main` imposta r1=100/r7=200/r8=300, chiama
`test_proc` (una `.proc`/`.endproc` che riscrive r1/r7/r8 con 11/22/33 e poi
chiama `clobber`, che li sporca ulteriormente con 999/888/777), poi fa
`dumps` sui tre registri — conferma che l'auto-save preserva i valori del
**chiamante** attraverso la call.

**Rifinitura del listato `--emit-expanded`, su richiesta dell'utente**:
`close_and_emit_proc()` (`src/assembler.c`) ora commenta la prima riga del
prologo (`; .proc NOME: prologo auto (r15,rX,...)`) e l'ultima dell'epilogo
(`; .proc NOME: fine epilogo auto (...)`), ciascuna con l'elenco registri
nell'**ordine reale** di push/pop (crescente nel prologo, decrescente
nell'epilogo — due liste separate, non la stessa riusata). Documentato in
`docs/manual.md` §4.2.1. Nessun impatto sulla codifica (`tokenize()` scarta
tutto da `;` in poi anche nel pass 2).

> I quattro paragrafi qui sopra sono **cronaca di sessioni chiuse** (28-29/08),
> tenuti perché contengono convenzioni ancora valide. Lo stato corrente è quello
> dei paragrafi in cima alla sezione.

**Working tree PULITO e tutto pushato** (§2): `origin/master` è a `e2955ff`, che
comprende §3.16 e §3.17 per intero. Il push del 05/09/2026 l'ha chiesto l'utente,
come quello del 04/09 — la regola resta «mai senza che sia chiesto in quel
momento».

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
| HAL + kernel + scheduler RR | completo, ridisegnato (§3.5), committato in `ae310d5` | [`linked/scheduler/`](../linked/scheduler/) |
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

Branch `master`, pubblicato su `git@github.com:rmigliori/vcpu_sim.git` (remote
`origin`, HTTPS + credential helper `git-credential-libsecret` configurato,
push senza prompt).

**Tutto pushato.** Il 05/09/2026 `origin/master` è passato da `20ae071` a
`e2955ff`: nove commit, il lavoro di §3.16 e §3.17 più i tre di handoff rimasti
indietro dal 04/09. Il push **l'ha chiesto l'utente**, come deve essere.

```
e2955ff Aggiorna i documenti: .include a nome nudo e target CMake (§3.17) <- ultimo pushato
735b508 I .vasm in target CMake, e le invarianti diventano ctest
5c14836 .include a nome nudo, e pool.vinc dipende da types.vinc
fe07cf8 Aggiorna handoff: §3.16 committato, working tree pulito
41e4565 Aggiorna i documenti: -I e .include idempotente (§3.16)
21c9305 -I nell'assembler e .include idempotente
99e82f9 Handoff: il push era dell'utente, e la regola sul push resta
3d46446 Handoff: correggi lo stato del push
099454b Handoff: il piano per il build in target CMake
20ae071 Aggiorna i documenti: pool (§10 della proposta) e handoff
dbf869c Pool di buffer a blocchi fissi, sei classi per potenze di due
d2c2527 Gestore dei timeout: vettore di descrittori, arm e cancel
ada7b4c Mailbox, strato _nc e invariante dei link nelle code
6efd08b Toolchain: riserva l'indirizzo 0 come puntatore nullo
79138ca Proposta di riscrittura dello scheduler: priorita' statiche + PCB
b342e83 Aggiorna handoff: reindentazione a 2 spazi + .git-blame-ignore-revs
ae29292 Reindenta i sorgenti C da 4 a 2 spazi (solo spaziatura)
b0b4f28 Aggiorna handoff: lavoro committato, working tree pulito
ae310d5 Ridisegno HAL/kernel a tre confini + auto-save registri in .proc/.endproc
98a40a0 Aggiorna handoff: lavoro pendente committato
ef10e3b Riorganizza sorgenti .vasm: examples/ -> standalone/ + linked/, doc aggiornata
3434b6d HAL + kernel puro + demo scheduler a preemption differita
2111646 Assembler: direttive di compile-time (.equ/.struct/.field/.res/.include)
3d1b790 Toolchain: reloc R_ADDR per puntatori a funzione (li di simbolo)
55b638e Snapshot iniziale: simulatore vCPU vettoriale + toolchain + scheduler RR
```

`ae310d5` contiene tutto il lavoro di §3.5 (ridisegno HAL/kernel a tre
confini) e §3.6 (auto-save registri in `.proc`/`.endproc` + `--emit-expanded`
+ rifiniture ai commenti del listato espanso).

I quattro commit del **04/09/2026** sono in ordine di dipendenza, e va tenuto:
`6efd08b` (il puntatore nullo) **deve** precedere `ada7b4c`, perche' senza
l'indirizzo 0 riservato il test dell'invariante dei link non passa.

> ### Il branch è stato pubblicato il 04/09/2026 — ma la regola sul push resta
>
> Fino a quel giorno il branch locale non era mai stato pubblicato. Alle **20:35
> del 04/09/2026 l'utente ha fatto un push**, portando `origin/master` da
> `98a40a0` a `20ae071`: **dieci commit in una volta**, tutto il lavoro del 30/08
> e del 04/09 più i quattro commit locali di fine agosto. Scelta sua e
> deliberata, confermata a voce.
>
> **La regola «non pushare, chiedere prima» vale ancora**, e l'utente l'ha
> riconfermata esplicitamente dopo quel push. Non è un vincolo sul repo: è
> **un'istruzione alla sessione**. L'utente pubblica quando vuole; Claude no, mai,
> nemmeno adesso che il branch è pubblicato — il fatto che `origin/master`
> esista non rende il push un'operazione di routine. Committare in locale sì,
> quando richiesto; pubblicare mai senza che sia chiesto in quel momento.
>
> **Il 05/09/2026 il push l'ha fatto la sessione, ed è coerente con la regola,
> non un'eccezione**: l'utente l'ha chiesto esplicitamente in quel momento
> («forse prima una push?»). È esattamente il caso previsto. Non vale come
> autorizzazione permanente: al push successivo si richiede.
>
> `.vscode/` resta l'unica cosa non tracciata, di proposito.

**`.git-blame-ignore-revs` (nuovo, 29/08/2026):** elenca i commit puramente
cosmetici che `git blame` deve saltare — al momento solo `ae29292`, la
reindentazione. È già attivo in questo clone
(`git config blame.ignoreRevsFile .git-blame-ignore-revs`), ma la config di
git **non è versionata**: dopo un clone nuovo va rieseguita a mano, altrimenti
il file c'è ma non viene consultato.

`.vscode/` non è mai stato tracciato, di proposito — da valutare se aggiungerlo a
`.gitignore` in futuro, non urgente.

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

### 3.3 Fix del warning di compilazione

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

### 3.4 Dispatcher e scheduler separati in `sched_dispatch`

`sched_dispatch` mischiava due algoritmi in una routine sola: il *meccanismo*
(salva sp uscente, chiama l'handler dell'app, recupera sp entrante) e la
*politica* (leggere `g_resched`, deciderne il consumo, chiamare `ctx_pick`).
Estratta una nuova routine interna **`scheduler`**
([`linked/scheduler/kernel/scheduler.vasm`](../linked/scheduler/kernel/scheduler.vasm)):
consuma `g_resched` e, se richiesto, delega a `ctx_pick`. `sched_dispatch` ora
chiama **sempre** `scheduler`, senza sapere se — o chi — verrà switchato: tre
routine, tre responsabilità nette (dispatcher / scheduler / politica), invece
di due.

`ctx_pick` non è stato toccato: resta l'unico punto da riscrivere per cambiare
politica (RR → priorità).

**Effetto collaterale atteso, non un bug:** le due `call`/`ret` in più nel
percorso IRQ spostano leggermente il timer (periodo fisso in cicli), quindi
i contatori finali della demo sono cambiati **da 102/70 a 99/66** — stesso
numero di tick (8, verificato con `--trace`), stessa alternanza dei task, solo
meno "lavoro utile" per tick per via dell'overhead. Aggiornata la sezione 4
qui sotto e il manuale (§7.10) con i nuovi numeri.

> **Superato dal disegno di §3.5.** Questa sottosezione resta come cronaca di
> come si è arrivati al passo successivo (`ctx_pick` esisteva ancora, la ISR
> era ancora chiamata da `sched_dispatch` senza distinguere orchestratore da
> dispatcher). Il codice attuale è quello di §3.5, non questo.

### 3.5 Ridisegno completo: `.proc`/`.endproc` + HAL/kernel a tre confini netti

Discussione lunga e guidata dall'utente (esperienza pluridecennale in sistemi
embedded/RTOS), partita dalla bocciatura di §3.4 e arrivata, per approssimazioni
successive, a un disegno con **tre confini** invece di due:

1. **HAL ↔ kernel sui registri** (invariato nel principio, ora completo nella
   pratica): salvataggio/ripristino contesto diventano procedure HAL vere e
   proprie, `ctx_save`/`ctx_restore`, non più codice inlineato in `_trap_entry`.
2. **Kernel ↔ applicazione sulla ISR** (invariato): `timer_isr` resta
   applicativa, mai HAL — se l'HAL la chiamasse direttamente aspettandosi un
   ritorno-poi-reti incondizionato, la preemption sarebbe impossibile per
   costruzione.
3. **HAL ↔ kernel sull'orchestrazione dell'IRQ** (la parte nuova, discussa a
   lungo): l'utente ha ricordato una vecchia primitiva `.interrupt` (RTOS anni
   '80/'90) che faceva salvataggio contesto *e* salto condizionato allo
   scheduler **dentro l'HAL** — il che avrebbe richiesto all'HAL di leggere
   `g_resched`, una variabile del kernel. Ne è uscito il disegno in cui l'HAL
   chiama un **unico simbolo kernel fisso** (`sched_dispatch`) e non sa/non
   legge nient'altro; tutta la logica (ISR, flag, politica, dispatch) resta nel
   kernel.

   > ### ⚠ Superato il 05/09/2026 — e l'attribuzione qui sopra era sbagliata
   >
   > Questo punto diceva che «confrontate le due alternative simmetria vs.
   > fedeltà storica, **l'utente ha scelto la simmetria**». L'utente ha
   > smentito: quel disegno non è suo, ed è una verbalizzazione errata di questa
   > sessione — «non è assolutamente la mia idea di scheduler real time». La
   > frase è stata tolta perché finché restava scritta ogni sessione futura
   > sarebbe ripartita da una decisione che nessuno aveva preso.
   >
   > Il confine giusto è in **§12 della proposta**, scritta il 05/09/2026: le ISR
   > sono applicative e sono *clienti* di HAL e kernel, quindi il vettore
   > consegna il controllo all'ISR e **l'HAL non nomina il kernel affatto**.
   > `sched_dispatch` non sopravvive. Gli altri due confini di questa sezione
   > (registri, ISR applicativa) restano validi.

**Il problema tecnico trovato durante la verifica** (non ipotetico, bloccante):
`call` è sempre `jal r15, target` nell'assembler di questo progetto (link
register cablato, non un operando). Se `_trap_entry` cominciasse con
`call ctx_save`, quella `call` sovrascriverebbe r15 **prima** che `ctx_save`
salvi il valore live del task interrotto — perso per sempre. Non è aggirabile
scegliendo un registro diverso: all'ingresso della trap tutti i 15 registri
sono live, quindi qualunque scelta come link register avrebbe lo stesso
problema. Soluzione: `_trap_entry` fa **un solo** push raw di r15 (l'unica
istruzione dell'HAL rimasta scritta a mano fuori da una procedura), poi può
chiamare `ctx_save` in sicurezza. Verificato negli internals dell'assembler
(`OP_JAL`/`OP_JALR`, `REG_RA` in `src/assembler.c`) prima di proporlo.

**`.proc`/`.endproc` (nuova direttiva assembler, richiesta esplicitamente
dall'utente prima di toccare lo scheduler)** — documentata in
[`docs/manual.md` §4.2.1](manual.md). Zucchero sintattico opt-in per il caso
comune (procedura non-foglia, corpo lineare, un solo `ret`): genera il
prologo (push raw di r15) e l'epilogo (pop + ret) automaticamente, verificando
che il nome in `.endproc` combaci con quello in `.proc`. **Nessuna detection
automatica leaf/non-foglia**, nessuna magia: è deliberatamente minimale,
l'utente ha scartato sia un sistema di macro parametriche generico (assente
nell'assembler, verificato) sia una detection automatica ("chi scrive
assembly per uno scheduler di questo tipo deve sapere cosa sta facendo").
Implementata in `src/assembler.c` (funzioni `handle_proc_directive`/
`emit_synth_line`, agganciate sia nel path a file singolo sia in
`assemble_object`), testata con casi d'errore (nome non corrispondente,
annidamento, `.endproc` senza `.proc`, `.proc` mai chiuso) prima di riscrivere
lo scheduler.

**Correzione post-implementazione (segnalata dall'utente):** la prima versione
richiedeva sia una label `NOME:` sia `.proc NOME` con lo stesso nome —
ridondante, l'utente ha fatto notare che non ha senso definire il simbolo e
poi "riusarlo" in `.proc`. Corretto: **`.proc NOME` definisce lei stessa
l'etichetta** (`add_symbol` dentro `handle_proc_directive`, con l'indice di
`g_code_count` corrente), esattamente come farebbe `NOME:` scritta a mano.
Una label separata prima di `.proc` ora è un errore (`duplicate label`), non
un no-op silenzioso — comportamento verificato esplicitamente. Aggiunto anche
un controllo che `.proc` compaia solo in `.text` (serviva comunque passare
`section` a `handle_proc_directive` per poter definire il simbolo come
codice). Aggiornati `coda.vasm` (le quattro `_s`) e `docs/manual.md` §4.2.1.

**Esito nel codice reale: nessuna delle tre routine nuove di
`scheduler.vasm` usa `.proc`**, e non è un fallimento della feature — è quello
che succede applicando con coerenza i criteri appena decisi: `ctx_save` è
foglia (non serve salvare r15), `_trap_entry`/`ctx_restore`/`sched_dispatch`/
`dispatcher` non ritornano mai via `ret` (chiudono con `reti` o con una `call`
finale che non ritorna, quindi non hanno epilogo), e `scheduler`/`dispatcher`/
`sched_dispatch` sono per scelta esplicita dell'utente routine "private" (un
solo chiamante per costruzione, non un'API generica) da scrivere a mano anche
quando la forma sarebbe lineare. Il primo uso vero è arrivato subito dopo,
in `coda.vasm` (vedi sotto): i quattro wrapper `_s`, che hanno esattamente la
forma giusta *e* sono API pubblica riusabile.

**Le tre routine kernel finali** (`linked/scheduler/kernel/scheduler.vasm`):

- **`sched_dispatch`** — orchestratore, UNICO simbolo che l'HAL chiama.
  Registra lo sp uscente, invoca l'ISR applicativa, e **solo se richiesta**
  consulta `scheduler`; chiama **sempre** `dispatcher` (è l'unico modo di
  uscire dalla trap, con o senza switch).
- **`scheduler`** — POLITICA pura, round-robin: è il vecchio `ctx_pick`
  rinominato (stesso corpo, nessuna logica cambiata). Riscriverla è l'unico
  passo per passare a una politica a priorità.
- **`dispatcher`** — MECCANISMO puro: chiama `ctx_restore` (HAL) su `current`
  e non ritorna mai.

**Verificato:** build C pulita senza warning; tutte le 20 sorgenti `.vasm` di
`standalone/`+`linked/` assemblano senza errori; invarianti (1) e (3)
invariate; invariante (2) — demo HAL+kernel — dà **105/74** (era 99/66),
stesso numero di tick (8: 7 `reti` + 1 `halt` al tick finale, verificato con
`--trace`), stessa alternanza dei task (entrambi i contatori crescono,
comparabili in ordine di grandezza). Il cambio nei numeri assoluti è lo stesso
tipo di effetto collaterale già visto in §3.4 (più `call` nel percorso IRQ
spostano l'allineamento del timer), non una regressione.

**`coda.vasm`: applicato `.proc`/`.endproc` ai quattro wrapper `_s`**
(`enqueue_coda_s`, `enqueue_testa_s`, `dequeue_testa_s`, `remove_buffer_s`),
su segnalazione dell'utente dopo aver visto il file aperto nell'IDE. Sono
esattamente il caso d'uso pensato per la direttiva: non-foglia, corpo lineare,
un solo `ret`, ed **esportati** (`.global`) — API pubblica riusabile da
qualunque task, non routine private a chiamante singolo come `scheduler`/
`dispatcher`. `.proc` genera solo il prologo/epilogo di r15; il salvataggio
della psw attorno alla `call` raw (necessario perché la raw usa r5 come
scratch) resta scritto a mano dentro il blocco. Verificato con un test mirato
(`enqueue_coda_s`/`dequeue_testa_s` sotto `IE=1`, ordine FIFO e count finale
corretti) e con l'invariante (2) invariata (105/74: i wrapper `_s` non sono
usati dalla demo).

**Non toccato in questa sessione, deliberatamente fuori scope:**
`linked/scheduler/scheduler_demo.vasm` — `timer_isr` sarebbe un altro
candidato per `.proc` (stessa forma: non-foglia, un solo `ret` via `isr_go`,
il ramo `halt` non ha bisogno di epilogo) ma non è stato richiesto.

### 3.6 `.proc`/`.endproc`: auto-save dei registri usati + `--emit-expanded`

**COMPILATO E VERIFICATO** (vedi §0 per i dettagli e i numeri). Il ritardo
nella verifica è stato causato solo dall'incidente `pkill -f`/tmpfs raccontato
sotto, non da un problema del codice.

**Richiesta dell'utente**, a sessione nuova, in risposta a "`.proc`/`.endproc`
vanno rifiniti" lasciato aperto la volta precedente: nel prologo salvare non
solo r15 ma anche i registri che il corpo usa davvero, invece di lasciare
all'utente il compito di salvarli a mano attorno alla `.proc` quando servono.
Due chiarimenti chiesti e risposti prima di implementare:

1. **Quali banchi di registri considerare?** → solo scalari `r1..r13` (non
   `f0..f15`, non `v0..v7` — questi restano da gestire a mano se servono).
2. **Cosa conta come "usato"?** → solo se il corpo lo **scrive** come
   destinazione di un'istruzione; non basta che lo legga soltanto (altrimenti
   si salverebbero anche registri-parametro che il chiamante deve già
   preservare per conto suo).

**Design/implementazione in `src/assembler.c`:**

- Il prologo dipende da **tutto** il corpo (quali registri scrive), ma il
  corpo si scopre riga per riga durante il pass 1 — quindi non si può più
  emettere il prologo subito quando si incontra `.proc` come faceva la prima
  versione (§3.5). Soluzione: il corpo tra `.proc` e `.endproc` viene
  **bufferizzato** (`g_proc_body[]`/`g_proc_body_count`, testo raw riga per
  riga) invece di essere emesso subito; solo a `.endproc` si conosce l'intero
  corpo, si scansiona, e si emette prologo + corpo + epilogo tutti insieme.
- `scalar_dest_reg(toks, n)`: riconosce se un'istruzione scrive un registro
  `r1..r13` come primo operando, per una lista chiusa di mnemonici
  "destinazione = primo operando, senza ambiguità" (`li mov add sub mul addi
  slli srli and or xor div rem lw setvl mfpsw mfepc`). Deliberatamente NON
  gestisce `jal`/`jalr` con registro esplicito (casi rari/avanzati, fuori
  dalla forma "corpo lineare" pensata per `.proc`) né i banchi float/vettore
  (fuori scope per scelta dell'utente, sopra).
- `close_and_emit_proc()`: scansiona `g_proc_body` con `scalar_dest_reg`,
  costruisce l'insieme dei registri usati, poi emette: push r15 (sempre, per
  primo — deve sopravvivere a **ogni** `call` nel corpo, stesso motivo di
  sempre), push dei registri usati in ordine crescente, il corpo verbatim,
  pop dei registri usati in ordine decrescente, pop r15 (per ultimo), `ret`.
  Stile push/pop pair-per-pair (`addi r14,r14,-4` / `sw`), identico a quello
  già usato in `ctx_save`/`ctx_restore` (`linked/scheduler/hal/machine.vasm`).
- `handle_proc_body_line()`: intercetta ogni riga fisica mentre `g_proc_active`
  è vero, PRIMA della normale gestione di etichette/direttive/istruzioni, sia
  in `assemble()` (path a file singolo) sia in `assemble_object()` (path
  `asm`/`ld`) — è duplicata la struttura del loop pass-1 tra le due funzioni
  nel file originale, quindi il gancio va messo in entrambe (fatto).
- **Vincolo nuovo, esplicito**: dentro `.proc`/`.endproc` non sono più
  ammesse etichette né direttive — solo istruzioni semplici. Necessario perché
  il prologo (quindi l'indirizzo di ogni riga del corpo) non è noto finché non
  si è letto tutto il corpo; è comunque coerente con "corpo lineare, un solo
  esit" già richiesto dalla direttiva. Nessuno dei quattro usi reali in
  `coda.vasm` viola questo vincolo (corpi già senza etichette/direttive).

**Limite importante, documentato in `docs/manual.md` §4.2.1**: è un'analisi
**statica** delle sole istruzioni scritte nel corpo — non vede cosa sporca una
routine chiamata. Conseguenza diretta: **i quattro wrapper `_s` in
`coda.vasm` (§3.5) NON vengono semplificati da questa feature.** Il loro
salvataggio a mano di `r5` (la psw) attorno alla `call irq_save`/
`irq_restore` resta necessario, perché `r5` non è mai scritto da
un'istruzione visibile nel corpo della `.proc` — arriva da un side-effect
della routine chiamata, invisibile allo scanner. La feature aiuta le `.proc`
future che calcolano direttamente con `li`/`add`/... nel corpo, non
retroattivamente questi quattro casi.

**Seconda richiesta della stessa sessione**: un modo per vedere l'assembly
completamente espanso (macro/`.include`/`.equ`/`.struct`/`.proc` già risolti)
prima della codifica finale — utile proprio per ispezionare cosa genera il
nuovo auto-save. Aggiunto:

- `dump_expanded(path, err, errsz)` in `src/assembler.c`: scrive su file,
  usando `g_code_lines`/`g_symbols` (già in memoria a fine pass 1), un
  listato con un'etichetta di codice per riga dove definita e
  `<indice>  <istruzione>` per ogni riga del corpo espanso.
- `assemble_object()` ha un nuovo parametro `const char* expanded_out`
  (NULL = comportamento invariato); se valorizzato chiama `dump_expanded`
  subito dopo il pass 1, **prima** del pass 2 — quindi il dump esiste anche
  se la codifica del pass 2 fallisce dopo, utile per debug. Firma aggiornata
  anche in `include/toolchain.h`.
- Nuovo flag CLI in `src/main.c` (`cmd_asm`): `vcpu_sim asm <in.vasm> -o
  <out.vo> --emit-expanded <file>`.
- **Unico chiamante di `assemble_object` nel codebase è `cmd_asm`** (verificato
  leggendo `src/toolchain.c` e `include/toolchain.h`): nessun altro call site
  da aggiornare per il cambio di firma.

**Incidente della sessione, causa root della mancata verifica:** durante un
test manuale di `.proc` con un file scritto a mano che dimenticava
`li r14, <stack>` (bug del file di prova, non del codice), il simulatore è
entrato in loop (store/load fuori range ripetuti, mai un `halt` raggiunto).
Per ucciderlo ho lanciato `pkill -f vcpu_sim` — che fa match sull'**intera
command line**, non solo sul nome del processo. Il path del workspace
(`.../sandBox/vcpu_sim`) contiene la stringa `vcpu_sim`, quindi molto
probabilmente ha ucciso anche l'extension host di VS Code che ospita questa
sessione. Il tool Bash è rimasto morto da lì fino a fine sessione (l'utente
ha dovuto riavviare VS Code). **Lezione per il futuro: mai `pkill -f` con
pattern generici in una directory che condivide il nome con il progetto —
usare PID espliciti o pattern più stretti** (es. il path completo
dell'eseguibile `./build/vcpu_sim`, non `vcpu_sim`).

**Checklist di verifica — ESEGUITA IL 28/08/2026, tutto torna:**

```bash
make                                    # pulito, zero warning — OK

# invarianti esistenti (§4) — stessi numeri di sempre
./build/vcpu_sim standalone/saxpy.vasm                          # 17/40/94 — OK
./build/vcpu_sim asm linked/scheduler/hal/machine.vasm      -o build/machine.vo
./build/vcpu_sim asm linked/scheduler/kernel/coda.vasm      -o build/coda.vo
./build/vcpu_sim asm linked/scheduler/kernel/scheduler.vasm -o build/scheduler.vo
./build/vcpu_sim asm linked/scheduler/scheduler_demo.vasm   -o build/scheduler_demo.vo
./build/vcpu_sim ld build/scheduler_demo.vo build/scheduler.vo \
                    build/coda.vo build/machine.vo -o build/scheduler_demo.vx
./build/vcpu_sim run build/scheduler_demo.vx                    # r5 = 105 / r5 = 74 — OK

# tutti i 20 sorgenti .vasm (standalone/+linked/) assemblano senza errori — OK

# test mirato NUOVO: tests/test_proc.vasm (committabile, non ancora committato)
./build/vcpu_sim tests/test_proc.vasm
#   r1 = 100 / r7 = 200 / r8 = 300 — preservati attraverso test_proc + clobber — OK
./build/vcpu_sim asm tests/test_proc.vasm -o build/test_proc.vo \
                     --emit-expanded build/test_proc.s
#   build/test_proc.s: push r15, push r1, push r7, push r8 (crescente),
#   corpo, pop r8, pop r7, pop r1 (decrescente), pop r15, ret — combacia con
#   la spec del manuale (§4.2.1) — OK
```

**Committato** in `ae310d5` (§2). Resta una domanda aperta:
`tests/test_proc.vasm` è il primo file sotto una directory `tests/`, mai
esistita prima in questo repo — da decidere con l'utente se diventa la sede
stabile per i test mirati futuri o se va spostato/rinominato.

### 3.7 Reindentazione dei sorgenti C da 4 a 2 spazi (29/08/2026)

Commit `ae29292`. Nasce da una richiesta collaterale: impostare `tab = 2` come
default in VS Code. Controllando i sorgenti prima di applicare è emerso che il
progetto era già **diviso a metà** — i `.vasm`/`.vinc` a 2 spazi (705 righe su
706, zero tab), i `.c`/`.h` a 4 (zero tab) — quindi il default globale avrebbe
fatto litigare il nuovo codice C con quello esistente. Da qui la decisione di
uniformare tutto a 2.

Impostazioni VS Code messe in `~/.config/Code/User/settings.json` (fuori dal
repo): `editor.tabSize: 2`, `editor.insertSpaces: true` e soprattutto
**`editor.detectIndentation: false`** — senza quest'ultima VS Code indovina
l'indentazione dal contenuto del file a ogni apertura e sovrascrive
silenziosamente `tabSize`. Gli override `"[c]"`/`"[cpp]"` a 4 spazi, aggiunti
inizialmente per proteggere questo progetto, sono stati **rimossi** dopo la
reindentazione: non servono più.

**Il punto tecnico interessante.** L'indentazione in C esprime due cose
diverse, che una sostituzione "4 spazi → 2" tratterebbe allo stesso modo
sbagliando:

- **strutturale** (annidamento di blocco) → si dimezza. È una regola
  *model-free*: non richiede di conoscere la profondità di annidamento, quindi
  non può sbagliare la nidificazione nemmeno con un parser impreciso.
- **allineamento** (continuation allineata a una colonna di una riga
  precedente) → va spostata a sinistra **esattamente quanto si è spostato il
  suo riferimento**, che nel frattempo è migrato pure lui. 132 righe su 3581.

Sono continuation sia le righe dentro una `(` o `[` ancora aperta, sia quelle
la cui riga precedente lascia l'istruzione aperta (ternari a catena, operatori
o virgole a fine riga, concatenazione di stringhe letterali). Una riga che
inizia con `}` chiude un blocco ed è **sempre** strutturale, anche dentro il
corpo di una macro multiriga.

**Due difetti trovati guardando il risultato, non dai test** — entrambi
passavano `git diff -w` e tutte le invarianti, perché erano puramente
cosmetici:

1. La prima versione riconosceva solo le continuation *da parentesi aperta* e
   ha sfalsato i ternari a catena di `assembler.c:393-396`, dove la riga
   prosegue per via dell'espressione (finisce con `:`), non di una parentesi.
2. Aggiunta quella regola, il `} while (0)` della macro `R()` finiva a colonna
   0. Da lì la regola sul `}`. I 4 backslash di quella macro (l'unica
   multiriga del codebase) sono stati riallineati a mano.

**Verifica** — il criterio forte è `git diff -w` **vuoto**: significa che
nessuna riga differisce per qualcosa che non sia spaziatura.

| Controllo | Esito |
|---|---|
| `git diff -w` su `src/` e `include/` | vuoto |
| inserzioni / cancellazioni | 2711 / 2711 (nessuna riga aggiunta o persa) |
| `make clean && make` | zero warning |
| output dei programmi vs baseline | identico riga per riga |
| md5 dei 10 artefatti rigenerati (`.vo`, `.vx`, `.s`) | identici |
| invarianti (1)(2)(3) + `test_proc` | 17/40/94 · 105/74 · 18/40/95 · 100/200/300 |

Gli script usati (`analyze.py`, `reindent2.py`) erano in scratchpad, non nel
repo: la trasformazione è fatta e non va rieseguita.

---

### 3.8 Bocciatura del disegno scheduler/dispatcher e nuovo modello a PCB (29/08/2026)

Sessione di sola progettazione, **nessun codice scritto o modificato**. Partita
dall'utente: «secondo me stiamo facendo degli errori architetturali su scheduler
e dispatcher». Analisi del codice esistente, individuazione dei difetti, e
specifica di un modello sostitutivo a priorità statiche.

Tutto il contenuto sta in
[`docs/proposta-kernel-realtime.md`](proposta-kernel-realtime.md) — è quello il
documento da leggere, non questo. Qui basti: **tre decisioni prese, una
aperta**, riassunte in §5.

> Prodotto anche un artifact web con gli stessi diagrammi, pubblicato **per
> errore** (l'utente lo voleva locale) e subito sostituito dal documento
> markdown. Se compare in `/artifacts`, è quello: si può cancellare.

### 3.9 Il TCB e i timeout a messaggio (30/08/2026)

Sessione di sola progettazione, **nessun codice scritto o modificato**. Ripresa
da §7.4, ma l'utente ha chiesto di ragionare **prima** sul TCB — ordine giusto,
perché §7.4 è l'unica decisione che *aggiunge* un campo al TCB.

Tre questioni sollevate sul TCB, in ordine di quanto costa sbagliarle: (1) la
coppia di link unica contro le attese a tempo, (2) chi costruisce il primo frame
di contesto, (3) `entry`/`stack_top` nel TCB o in una tabella di boot separata.

**Chiusa la (1)**, ed è l'utente ad aver proposto il modello che la risolve:
niente seconda coppia di link, perché il timeout non è uno stato del task ma un
**oggetto** — si chiede a un gestore di consegnare un messaggio scelto dal
richiedente alla sua mailbox a una certa scadenza. Verbalizzata nella proposta
come **§7.5** (decisione) più la nuova **§9** (il meccanismo: descrittore =
messaggio, gestore nella ISR del tick, lista non ordinata come primo passo,
cancellazione con il campo `dove`). Aggiornati anche §1, §3 (diagramma delle
classi + layout MESSAGGIO), §8 e §10 della proposta; le vecchie §8/§9 sono
diventate §9/§10.

Le questioni (2) e (3) restano aperte, riassunte in §5.

> **Superata da §3.11 per la parte sul meccanismo.** La §9 descritta qui —
> descrittore = messaggio, gestore nella ISR del tick, campo `dove` per la
> cancellazione — è stata riscritta da capo la sera stessa. La decisione sul TCB
> (§7.5) resta; il meccanismo dei timeout no.

### 3.10 La mailbox: progettata e IMPLEMENTATA (30/08/2026)

Partita da «manca tutto il ragionamento sulle mailbox»: §8 della proposta era
rimasta quella di prima — quindici righe sul vincolo di layout — mentre §7.5
aveva promosso la mailbox a *unico punto di blocco di un task*. Il ragionamento
completo è ora in [§8 della proposta](proposta-kernel-realtime.md), riscritta da
capo in sette sottosezioni. **Non ripeterlo qui**: qui solo cosa è successo e cosa
è stato scritto.

**Il percorso, perché conta più del risultato.** Ho proposto il contatore
contabile alla Dijkstra (*disponibili meno in attesa*), che lasciava lo zero
ambiguo in una finestra. L'utente ha proposto modulo-e-segno, cioè un contatore
**descrittivo**, che l'ambiguità la toglie; ho verificato sulla macchina che il
bit di tipo a bit 31 litiga con l'estensione di segno di `lw` e che senza `andi`
la maschera costa, e il compromesso è il **complemento a due con semantica
descrittiva**: stessa idea dell'utente, encoding che la macchina regala. Poi
l'utente ha bocciato il mio `MESSAGGIO` con header di kernel da 20 byte
(`scadenza`/`mailbox`/`dove`): il messaggio è **solo** `fwd`/`bwd`/`payload`, e
quei campi vanno nel payload del servizio che li usa. Da lì è nato tutto il
modello a interfacce di §8.6.

**Scritto e verificato:**

| File | Cosa |
|---|---|
| `kernel/messageHandling.vasm` | **nuovo**: `send` (raw), `send_s` (`.proc`), `receive` |
| `kernel/coda.vasm` | strato `_nc` (4 primitive), che è il **corpo** di quelle contate: cadono in sequenza, niente `call`, niente splicing duplicato |
| `include/types.vinc` | `MESSAGGIO` (fwd/bwd/payload), `PAYLOAD` (messageType/clientTag/messageCode/replyMailbox/specifiche), `MSG_REQUEST`/`MSG_REPLY` |
| `tests/test_mailbox.vasm` | **nuovo**, dà `0 1 2 11 22 0 33 0 0` |

Il test esercita anche la **consegna diretta** senza avere uno scheduler, con un
trucco che vale la pena ricordare: in un sistema a un flusso solo «bloccarsi»
equivale a «far girare adesso la controparte», quindi lo stub di `task_block`
esegue la `send` che sveglierà il chiamante e ritorna.

**Prova oggettiva che il confine è al posto giusto:** `messageHandling.vasm` non
referenzia **nessun** campo di `MESSAGGIO` (grep a zero) — usa solo `TESTA` per
la mailbox e `TCB.state` per il ricevente.

**Invariante (2) cambiata: 105/74 → 104/73.** `dequeue_testa` ha una `beq` in
più sul percorso non vuoto (il test strutturale del corpo condiviso). È lo stesso
effetto già visto in §3.4 e §3.5, non una regressione: stesso numero di tick, che
è 8 per costruzione. Una prima stesura usava un `j` e costava due istruzioni
(103/72); sostituito con la caduta in sequenza.

**Errori strutturali riconosciuti e non ancora sanati** (elenco completo in §8.7
della proposta): `task_ready`/`task_block` inventate per aggirare la decisione
aperta sulla commutazione volontaria, `receive` che legge `current`, l'`halt` sul
secondo ricevente, `send_s` che promette una preservazione di registri che non
dà. Tutti dipendono da decisioni non ancora prese.

**Scoperto per strada:** `.include` **non è idempotente** — includere due volte
lo stesso `.vinc` dà `asm error: duplicate constant`. Col modello «un `.vinc` per
fornitore» diventa un problema appena i file di interfaccia sono due.
**Risolto il 05/09/2026, §3.16.**

### 3.11 Il gestore dei timeout, riprogettato da capo (30/08/2026, terza sessione)

Sessione di sola progettazione, **nessun codice scritto**. Il ragionamento intero
sta in [§9 della proposta](proposta-kernel-realtime.md), riscritta in sei
sottosezioni; qui solo il percorso, perché è quello che spiega perché il modello
precedente non va riproposto.

Partita da «spiegami bene questo `MSG_FUORI`», e subito dopo da **«secondo me ti
stai complicando la vita»**. L'utente ha proposto un modello completamente
diverso e più semplice, ed è quello adottato:

- **interfaccia procedurale**: `timeout_arm(tick, clientTag, messageCode,
  replyMailbox)`, `timeout_cancel`. Il cliente non fornisce nessun buffer, solo
  quattro parole;
- **vettore statico di descrittori**, non una lista — «quanti timeout
  contemporanei avremo? 10?». Il descrittore non entra mai in una lista, quindi
  sparisce per intero la casistica della cancellazione;
- **pool di buffer**: alla scadenza il gestore ne preleva uno, ci formatta il
  messaggio e lo manda; il ricevente copia ciò che gli serve e lo rilascia il
  prima possibile;
- **il gestore è un task ad altissima priorità**, non l'ISR: l'ISR manda solo un
  messaggio di tick. Così la scansione O(N) esce dal tempo a interrupt
  disabilitati, e il pool vuoto diventa «il timeout arriva tardi» invece di «non
  arriva mai»;
- **il messaggio stantìo è del ricevente**: il gestore non insegue un messaggio
  già consegnato. «Uno sviluppatore sw dovrebbe essere in grado di scrivere una
  macchina a stati in grado di gestire un tale evento» — e lo strumento è il
  `clientTag`, monotono per attesa.

**Il modello precedente non era solo più complicato: era rotto.** La
dimostrazione è in §9.6 della proposta e vale la pena averla in mente, perché è
il tipo di difetto che i test non trovano: `dequeue_testa` non azzera i link del
nodo che sfila, quindi una `timeout_cancel` con `dove == MSG_MAILBOX` su un
messaggio già consumato riscrive `mbox.fwd` e porta `count` a `-1` — che nella
convenzione con segno significa *c'è un TCB in attesa*. La `send` successiva
prende il ramo della consegna diretta, sfila un messaggio credendolo un TCB, gli
scrive dentro `TCB.state` e lo passa a `task_ready`.

**Aperto in §9.5**: se `timeout_arm` restituisce un handle (col rischio del
riciclo dello slot) o se si cancella per identità `(replyMailbox, clientTag)`; la
taglia dei buffer del pool; dove collocare il gestore, che formatta il payload e
quindi per §8.4 **non è kernel**.

### 3.12 Recupero della §9 e allineamento dei documenti (30/08/2026, quarta sessione)

Sessione di sola manutenzione documentale, **nessun sorgente toccato**. Partita
da «rileggi la proposta per capire dove eravamo»: ho riassunto lo stato leggendo
i documenti, e ho proposto per il gestore dei timeout **il modello che l'utente
aveva già bocciato la sera prima**, perché la §9 nuova non era sul disco.

L'utente ha detto «ci siamo persi delle discussioni». Confronto fra i `mtime` dei
file e le trascrizioni: il 30/08 ci sono state tre sessioni ma i documenti ne
raccontavano una sola, e `stato-lavori.md` era fermo alle 19:09, cioè a **prima**
della sessione di §3.11. La §9 nuova esisteva solo nella trascrizione. Recuperata
da lì e reinnestata; il dettaglio del come e la regola operativa che ne discende
stanno in §0.

Poi allineati i punti del documento che raccontavano ancora il modello caduto:

| Dove | Cosa diceva |
|---|---|
| §1 | «Nessun pool: i buffer sono statici e li possiede chi li manda» |
| §3 | classe `TIMEOUT` con `attesa : TESTA` e `timeout_tick()`, relazione `TIMEOUT o-- MESSAGGIO` |
| §3 e §7.5 | il TCB ha una coppia di link sola «perché in lista ci va il messaggio, non il TCB» |
| §8.2 | `coda.vasm` serve anche «alla lista dei timeout» |
| §8.5 | la `send` è raw «perché il gestore gira nella ISR del tick» |
| §10 | `kernel/timeout.vasm`, «gestore dei timeout a messaggio» |

Aggiunte al diagramma di §3 le classi `DESCRITTORE` e `POOL`. Normalizzate le
date: quello che i documenti chiamavano 31/08 era in realtà il 30/08 — tre
sessioni in un giorno solo, non due giorni.

**La cosa tecnica emersa strada facendo**, ed è l'unico contenuto nuovo di questa
sessione: §5 affermava che il gestore dei timeout «non è bloccato da niente di
architetturale». Vero finché girava nell'ISR; **falso da quando è un task**
(§9.2), perché fa `receive` e quindi dipende dalla commutazione volontaria che
manca, e «a priorità massima» presuppone lo scheduler a priorità, fermo su §7.4.
La decisione resta giusta — la scansione O(N) fuori dal tempo a interrupt
disabilitati vale il prezzo — ma il prezzo va scritto. Restano scrivibili subito
il pool e il vettore di descrittori (§5).

### 3.13 Il vettore di descrittori: DECISO e SCRITTO (04/09/2026)

Chiusa la prima delle tre domande di §9.5 e scritto il codice che ne dipendeva.
Il ragionamento sta in [§9.5 della proposta](proposta-kernel-realtime.md), qui
solo l'esito e cosa c'è sul disco.

**Decisione: nessun handle, si cancella per identità `(replyMailbox,
clientTag)`.** L'argomento non è il costo della scansione: è che l'handle non si
può validare se non con l'identità stessa. Il `lw` + `bne` sulla `replyMailbox`
che sembrava bastare intercetta il riciclo dello slot da parte di *un altro*
task e lascia passare quello comune — stesso task che riarma e si riprende lo
stesso slot, cancel stantìa che uccide l'attesa successiva. Aggiunto il
confronto sul `clientTag` per chiudere il buco, l'handle resta solo una
scorciatoia per non scandire, pagata con una parola in più nella macchina a
stati del cliente e con la disciplina di invalidarla dopo ogni `receive`.

**Seconda decisione, dell'utente: libero/occupato esplicito.** §9.1 deduceva lo
slot libero da `replyMailbox == 0`; ora il descrittore ha un campo `stato`
(`TMO_FREE`/`TMO_ARMED`). È lo stesso argomento di §7.2 su `TCB.state`: uno
stato scritto si legge in un dump e si controlla, una convenzione no. Toglie
anche il vincolo implicito «nessuna mailbox all'indirizzo 0» e fa sì che una
cancel con identità spazzatura non combaci con niente invece di combaciare con
**tutti** gli slot liberi. Due valori bastano: «scaduto ma pool vuoto» non è un
terzo stato, perché lasciando la casella armata il ritentativo al tick dopo è
automatico. `TMO_FREE = 0` fa nascere il vettore libero dall'immagine dati
azzerata, quindi non serve nessuna `timeout_init`.

**Scritto e verificato:**

| File | Cosa |
|---|---|
| `include/timeout.vinc` | **nuovo**: `DESCRITTORE` (stato/scadenza/replyMailbox/clientTag/messageCode), `TMO_FREE`/`TMO_ARMED`, esiti `TMO_OK`/`TMO_FULL`/`TMO_DUP`/`TMO_NONE` |
| `kernel/timeout.vasm` | **nuovo**: `tmo_now`, il vettore (10 caselle), `timeout_arm`, `timeout_cancel` |
| `tests/test_timeout.vasm` | **nuovo**, dà `3 0 150 1 2 0 0 0 3 0 1` |

Primo `.vinc` per fornitore del modello di §8.6, e **deliberatamente foglia**:
non include `types.vinc`, perché `.include` non è idempotente e un `.vinc` che
ne includa un altro esplode appena un chiamante include entrambi. Regola
provvisoria finché non è resa idempotente: **i `.vinc` non si annidano**.

Tre cose da non perdere di vista, tutte già nei commenti del sorgente:

- **`arm` e `cancel` non hanno una variante `_s`**, la sezione critica sta
  dentro. Stesso argomento della `receive` (§8.5): la protezione deve
  comprendere la scansione, la decisione e la scrittura, che qui sono un atto
  solo. Da IE=0 restano corrette perché `irq_save`/`irq_restore` sono
  componibili.
- **Una sola passata di `arm` fa due lavori**: cerca il primo slot libero e, sugli
  armati, controlla che l'identità non ci sia già (`TMO_DUP`). L'invariante «al
  più un timeout armato per identità» è quello che permette a `cancel` di
  fermarsi al primo match.
- **`TMO_DUP` non ferma la macchina.** Sarebbe stato il posto naturale per un
  `halt`, ma è il difetto già aperto sulla `receive` (§8.7): fermare la macchina
  è una politica e non è di questo modulo deciderla. Si risponde al chiamante.

**Non scritto, di proposito**: la scansione delle scadenze e la consegna. Vogliono
`buf_alloc`, cioè il pool, ancora in discussione — e nessuno `.extern` di comodo
per aggirarla. Quando arriverà, la transizione `ARMED → FREE` della scadenza
dovrà stare **nella stessa sezione critica in cui si decide di consegnare**:
leggere i campi, uscire, mandare e liberare solo dopo riaprirebbe dentro il
gestore esattamente il bug del riciclo dell'handle.

**Invarianti (1)(2)(3) e il test della mailbox: invariati** — nessun file
esistente è stato toccato.

### 3.14 Il pool: progettato per intero, e le tre cose che vengono prima (04/09/2026)

Sessione lunga, guidata dall'utente, partita da una posizione netta: **per
requisiti di safety un RTOS non deve avere allocazione dinamica**, solo statica.
Il disegno che ne è uscito sta tutto in [§10 della proposta](proposta-kernel-realtime.md)
— sei classi per potenze di due, l'interfaccia, l'invariante dei link — e **non
va ripetuto qui**. Qui il percorso, che è la parte che spiega perché le decisioni
sono quelle.

**Il pool non è scritto**: manca solo il dimensionamento per classe. Sono invece
scritte e verificate le tre cose che vengono prima.

**Il percorso, per approssimazioni successive.** Avevo proposto la parola di
provenienza **prima** dei link, con `buf_free` che la leggeva a `-4`. L'utente
l'ha bocciata («non metterei un campo prima dei link nemmeno se avessi una
pistola puntata alla tempia») e ha ragione con un argomento più forte del gusto:
in questo progetto ogni struttura si sovrappone a offset 0 andando avanti, e un
campo negativo significa che il puntatore consegnato non è la base
dell'allocazione — cioè il `container_of` che `coda.vasm` si vanta di non avere.
Ho allora proposto di mettere la parola dentro `MESSAGGIO`; l'utente l'ha spinta
un livello più su ancora, **dentro il payload**, e anche lì aveva ragione: così
il pool è un *cliente* del messaggio invece che comproprietario del tipo, che è
la stessa regola con cui era stato bocciato l'header di kernel da 20 byte
(§8.4). L'unica cosa che ho aggiunto io è che la parola non può stare **solo**
sui buffer del pool: una mailbox riceve sia il messaggio di scadenza (dal pool)
sia la risposta a una richiesta (il buffer statico del cliente girato), e con due
layout diversi il ricevente non saprebbe nemmeno dove leggere `messageType`.
Quindi sta nella testa comune, `PAYLOAD.pool`, e i buffer statici portano 0
gratis perché il `.data` nasce azzerato.

**Il doppio rilascio, e chi ha vinto la discussione.** Avevo proposto di
riconoscerlo con il **segno** della taglia (dentro/fuori dal pool), sulla scia
del contatore con segno della mailbox. L'utente ha proposto invece di guardare i
**link**: `buf_alloc` li azzera, `buf_free` pretende che siano nulli. È meglio,
per una ragione che il segno non copriva — prende anche il rilascio di un buffer
**ancora accodato in una mailbox**, che il segno avrebbe accettato perché quel
buffer è legittimamente «fuori dal pool». Ma così com'era non funzionava:
`dequeue_testa` **non azzera i link del nodo che sfila**, quindi un buffer che
ha attraversato una mailbox arriva a `buf_free` con i link sporchi e verrebbe
rifiutato pur essendo legittimo. È la stessa identica riga su cui è caduto il
modello dei timeout di §9.6. Da qui l'azzeramento è sceso dentro `coda.vasm`, ed
è diventata un'invariante generale.

**L'ultimo giro, sull'errore.** Avevo proposto un hook fatale raggiunto con `j`
(niente r15, la foglia resta foglia, la politica esce dal modulo più basso).
L'utente l'ha respinto: «l'applicativo non sa che si è verificato un errore e
quindi non può produrre diagnostica». Ha ragione due volte — con un salto nudo il
gestore non riceve nessun contesto, e soprattutto un salto che non ritorna
**toglie all'applicativo la decisione di continuare**, lasciandogli solo il come
morire. Quindi esito di ritorno in `r3`. La diagnostica resta sufficiente perché
**il chiamante ha già tutto il contesto: testa e nodo li ha passati lui.**

**Il puntatore nullo, trovato dal test e non dal ragionamento.** Il primo giro di
`tests/test_coda.vasm` non rifiutava il doppio accodamento: `testa` finiva
all'**indirizzo 0** (primo oggetto del primo modulo nel link), quindi i link di
un nodo accodato valevano 0 ed erano indistinguibili da «non in lista». Non era
un difetto nuovo: il codice **assumeva già** che 0 fosse nullo in tre punti
(`dequeue_testa` che restituisce 0 per coda vuota, `current == 0` = nessun task,
`buf_alloc` che restituirà 0 per «nessun blocco»), semplicemente nessuno l'aveva
mai imposto e nessun oggetto ci era mai finito sopra. Ora il segmento dati parte
da 4 — `NULL_GUARD` in `include/vcpu.h`, applicato dal linker e dal percorso a
file singolo — e l'immagine dati del linker viene azzerata prima di essere
riempita, così la parola di guardia è zero e non memoria di scarto.

**Scritto e verificato:**

| File | Cosa |
|---|---|
| `kernel/coda.vasm` | azzeramento dei link sulla rimozione, controllo sull'inserimento, esito in `r3`; il controllo sta **prima** del contatore, se no un rifiuto lascerebbe la testa incoerente |
| `kernel/messageHandling.vasm` | `send` controlla in cima e restituisce l'esito; il controllo non è delegato all'enqueue perché sul percorso della consegna diretta arriverebbe a TCB già sfilato |
| `include/types.vinc` | `CODA_OK`/`CODA_LINKED`/`CODA_UNLINKED`, `PAYLOAD.pool` come primo campo |
| `include/vcpu.h`, `src/assembler.c`, `src/toolchain.c` | `NULL_GUARD`: i dati partono da 4 |
| `docs/manual.md` §3 | l'indirizzo 0 è riservato — la prima etichetta in `.data` vale 4 |
| `tests/test_coda.vasm` | **nuovo**, dà `0 1 1 1 0 0 0 2 1 0 0 0 0` |

**Due punti dove l'esito è ignorato di proposito, e non è una svista**:
`receive` quando accoda il proprio TCB (sta per bloccarsi, non ha nessuno a cui
tornare) e `scheduler` quando riaccoda il task uscente (gira dentro la trap). È
lo stesso nodo aperto dell'`halt` di §8.7 — come un task entra nel kernel — e va
sciolto lì, non inventando qui una via d'uscita. In `scheduler` il controllo non
può comunque scattare: il TCB uscente è `RUNNING`, cioè fuori da ogni coda, e da
oggi quella dichiarazione è vera nei fatti e non solo nel commento.


**Invariante (2): 104/73 → 98/65**, con gli stessi **8 tick** (7 `reti` + 1
`halt`, verificato con `--trace`). È il costo del controllo e dell'azzeramento su
un percorso che gira a ogni switch: stesso tipo di effetto di §3.4, §3.5 e
§3.10, non una regressione. `NULL_GUARD` non ha spostato niente — nessun codice
dipende da un indirizzo dati assoluto. (1) e (3) invariate, tutti i 13
`standalone/` girano ancora da soli.

### 3.15 Il pool di buffer: SCRITTO (04/09/2026)

Stessa sessione di §3.14, subito dopo. Il disegno era chiuso, mancavano i sei
conteggi; il resto è stato scrittura.

**Dimensionamento: 10 blocchi da 16, 4 da 32, zero le altre quattro.** Il 10 non
è arbitrario — sono gli slot del vettore dei descrittori, cioè il massimo di
scadenze che possono cadere sullo stesso tick: così il pool non può mai essere
*lui* la ragione per cui un timeout arriva tardi. I 4 da 32 servono al primo
servizio che avrà delle specifiche, e intanto al test per dimostrare
l'arrotondamento e il non-ripiego. 456 byte di blocchi più 72 di teste.

| File | Cosa |
|---|---|
| `include/pool.vinc` | **nuovo**: le sei taglie, la vista `BLOCCO`, le sei `BLOCCOnn` per `.res`, gli esiti `POOL_*` |
| `kernel/pool.vasm` | **nuovo**: le sei teste, i blocchi, `pool_init`, `buf_alloc`, `buf_free`, più le due mappature taglia→classe |
| `tests/test_pool.vasm` | **nuovo**, dà `10 4 0 0 9 0 32 3 2 0 0 4 4 4 3 0 0 1 4` |

Quattro cose da non perdere di vista, tutte nei commenti del sorgente:

- **Le mappature taglia→classe sono due, non una.** `buf_alloc` arrotonda per
  eccesso una *richiesta* (20 byte → classe 32); `buf_free` pretende una
  corrispondenza *esatta* con una delle sei taglie, perché lì non legge una
  richiesta ma una **capacità** che ha scritto il pool. È quell'asimmetria a
  rendere riconoscibile un puntatore estraneo invece di lasciarlo sfasciare una
  lista.
- **`buf_free` non ha parametri oltre al buffer**, e i suoi tre rifiuti sono
  esattamente quelli che servono: `POOL_LINKED` (il blocco sta ancora in una
  lista — prende il doppio rilascio *e* il rilascio di un buffer ancora accodato
  in una mailbox) e `POOL_ESTRANEO` (taglia non valida, quindi anche un buffer
  statico del cliente, che ha `pool == 0`). Il primo esiste solo grazie
  all'invariante dei link di §3.14.
- **Nessuna delle due routine ha una sezione critica propria**: la catena
  taglia→classe è aritmetica sull'argomento, e l'unico atto sullo stato è un
  `dequeue_testa_s`/`enqueue_coda_s` che si protegge da sé.
- **`pool_riempi` salva `r10..r13`.** Gli argomenti devono migrare fuori da
  `r1..r6` perché `enqueue_coda` usa `r3` per l'esito e `r4`/`r5` come scratch;
  salvarli tiene il contratto di `pool_init` pulito invece di lasciare una
  trappola per un chiamante futuro.

**Primo modulo che include due `.vinc`** (`pool.vinc` per sé, `types.vinc` per
`TESTA`), e funziona proprio perché sono foglia: se uno dei due includesse
l'altro darebbe `duplicate constant`. È la conferma pratica della regola presa
in §3.13.

**Nessuna invariante si è mossa**: `pool.vasm` è tutto codice nuovo e non tocca
nessun file esistente.

### 3.16 `-I` nell'assembler e `.include` idempotente (05/09/2026)

Primo punto della ristrutturazione del build (§5), e l'unico che si potesse fare
per primo: sono le due cose che rendono vero tutto il resto. **Nessun `.vasm`
esistente è stato toccato** — è deliberato, ed è quello che rende dimostrabile il
vincolo non negoziabile: le invarianti danno gli **stessi identici numeri**,
verificato riga per riga contro una baseline presa prima di cominciare.

**`-I`, cioè la dipendenza scritta per nome invece che per posizione.** Un nome
si cerca prima nella cartella del file di primo livello (comportamento di sempre,
intatto: è per questo che le 12 `.include` esistenti non si sono accorte di
niente) e poi nelle cartelle `-I`, in ordine. Il flag c'è sia su `asm` sia sul
percorso a file singolo, nelle due forme `-I <dir>` e `-Idir`. Un `-I` di troppo
è un errore fatale e non un avviso: scartato in silenzio, riemergerebbe più tardi
come un `cannot open include` incomprensibile.

**L'idempotenza, che era sulla lista degli aperti da §3.10.** Un file già entrato
nell'unità di assemblaggio non rientra: `.include` diventa un no-op silenzioso,
come `#pragma once`. L'identità è quella del **file sul disco** — path
canonicalizzato con `realpath` — non della stringa scritta, e questo è il punto
che conta: `"pool.vinc"` trovato via `-I` e `"../include/pool.vinc"` trovato
accanto al sorgente sono lo stesso file e vengono riconosciuti tali. Se
l'identità fosse la stringa, l'idempotenza non servirebbe proprio nel caso per
cui esiste, cioè quando due percorsi diversi portano allo stesso `.vinc`.

**Cosa si sblocca, concretamente:** la regola provvisoria presa in §3.13 — «i
`.vinc` non si annidano» — **è caduta**, ed era il prerequisito segnalato in §5
(«l'idempotenza smette di essere un accessorio»). Le note in testa a `pool.vinc`
e `timeout.vinc` sono state riscritte di conseguenza: dicono ora che l'ostacolo
non c'è più e cosa resta da fare perché `pool.vinc` possa includere `types.vinc`
davvero — cioè il punto 2, le `.include` a nome nudo, perché **un nome si risolve
rispetto al file di primo livello, non rispetto a chi include**. Quel dettaglio è
la cosa da non riscoprire a metà migrazione: un `.vinc` può nominarne un altro
**solo** per nome nudo, e chi assembla deve passare `-I`.

| File | Cosa |
|---|---|
| `src/assembler.c` | `IncState` (stack dei file + insieme dei file già inclusi), `inc_push`/`inc_begin`/`inc_cleanup`, ricerca su `-I`, `asm_add_include_dir` |
| `src/main.c` | `parse_include_flag`, agganciata a `cmd_asm` e al percorso legacy |
| `include/toolchain.h` | `asm_add_include_dir`/`asm_clear_include_dirs` |
| `linked/scheduler/include/pool.vinc`, `timeout.vinc` | le note sulla regola caduta |
| `docs/manual.md` | §4.2.2 nuova, più la riga di `.include` e quella di `asm` nella tabella dei sottocomandi |
| `tests/test_include.vasm` | **nuovo**, dà `16 12 16 512 1` |

Due cose emerse scrivendo, e sono migliorie non richieste ma dovute:

- **La logica di `.include` era duplicata** nei due loop di pass 1
  (`assemble()` e `assemble_object()`), com'è duplicata la struttura del loop
  stesso — lo stesso punto già annotato in §3.6 per il gancio di `.proc`. Ora la
  parte di include sta in un posto solo e i due chiamanti la usano in due righe
  identiche, se no la ricerca su `-I` e il set dei file visti sarebbero nati
  duplicati e destinati a divergere.
- **Gli include annidati restavano aperti sui percorsi d'errore.** Difetto
  preesistente e innocuo in una CLI che esce subito, ma con `inc_cleanup` costava
  una riga: aggiunto a `fail:` e ai punti di uscita del percorso a file singolo.

**Verifica**, oltre alle invarianti: nome nudo che fallisce senza `-I` e passa
con, nelle due forme del flag; lo stesso file incluso con **tre grafie diverse**
(nudo, relativo, relativo con `..` di mezzo) che dà un solo insieme di costanti;
un `.vinc` che ne include un altro già incluso dal chiamante — il caso che prima
esplodeva; un file che include sé stesso, che è un no-op e non una ricorsione; e
il messaggio d'errore di un file mancante, che elenca le cartelle cercate. In
più, `tests/test_pool.vasm` riscritto **provvisoriamente** a nome nudo, assemblato
con `-I` e linkato: dà la stessa identica sequenza. È l'anticipo del punto 2, e
dice che sarà meccanico; il file è stato ripristinato subito.

### 3.17 Le `.include` a nome nudo, e i `.vasm` in target CMake (05/09/2026)

Punti 2 e 4 della ristrutturazione del build, fatti nella stessa sessione perché
sono accoppiati: il 2 da solo peggiora le cose (ogni comando a mano cresce di un
flag) e il 4 senza il 2 sposterebbe i path dentro il `CMakeLists.txt` invece di
toglierli, che è il fallimento contro cui §5 metteva in guardia.

**Punto 2 — le 12 `.include` a nome nudo.** `.include "types.vinc"` invece di
`"../include/types.vinc"` o `"../linked/scheduler/include/types.vinc"`: lo stesso
file di interfaccia ha ora **una sola grafia in tutto il progetto** invece di una
per ogni cartella da cui viene incluso. Le pipeline scritte a mano hanno
guadagnato `-I`, con una variabile `I=-Ilinked/scheduler/include` in testa per
non farle diventare illeggibili. Unica eccezione voluta: `tests/test_include.vasm`
tiene una grafia relativa, perché è esattamente ciò che testa.

**`pool.vinc` ora include `types.vinc`**, ed è la modellazione che §5 indicava
come giusta: `BLOCCO` *è* un nodo di lista, una free-list è una `TESTA` con i
blocchi come nodi. `timeout.vinc` resta invece foglia, ma per una ragione sua e
non per un limite della toolchain: il `DESCRITTORE` non entra mai in una lista
(§9.3), quindi non ha niente da chiedere a `types.vinc`.

**Punto 4 — CMake.** `CMakeLists.txt` più `cmake/vasm.cmake` (le funzioni) e
`cmake/vasm_check.cmake` (il runner dei test). Cinque forme: `vasm_interface`
per un `.vinc`, `vasm_object` per un `.vasm`→`.vo`, `vasm_archive` per un `.va`,
`vasm_program` per un `.vx`, `vasm_check` per un'invariante.

**Il Makefile non è stato toccato e i due build convivono**: `make` scrive in
`build/`, CMake nella cartella passata a `-B` (`out/` nella documentazione, ora
in `.gitignore`). Se un giorno il `Makefile` va ritirato è una decisione a parte
— questa migrazione non la forza.

**Le tre cose non ovvie di §5, come si sono rivelate nei fatti:**

- **La propagazione dei `.vinc`.** Le `INTERFACE` library li modellano bene
  (sono header-only per costruzione), ma `INTERFACE_INCLUDE_DIRECTORIES` non
  arriva da sola a un `add_custom_command`, che non linka niente e non eredita
  nulla. La chiusura transitiva la percorre `_vasm_closure` a mano, seguendo
  `INTERFACE_LINK_LIBRARIES`, e ne esce sia la lista di `-I` sia quella dei file
  per il `DEPENDS`. L'elenco dei file sta in una proprietà nostra
  (`VASM_HEADERS`) perché CMake propaga da sé solo le `INTERFACE_*` che conosce.
- **Le dipendenze restano DICHIARATE, non scoperte.** Se un `.vasm` include un
  `.vinc` senza che `LIBS` lo dica, CMake non lo saprà mai e il rebuild non
  scatterà. È il punto 3 (`--emit-deps`) e resta aperto — ma **non** era un
  prerequisito, come §5 lasciava intendere: con la dipendenza dichiarata sul
  target dell'interfaccia i path non tornano nel build, perché si scrive una
  volta sola lì.
- **L'archivio `.va` è stato usato davvero.** I quattro test linkano
  `libkernel.va` invece di una lista di `.vo` ricopiata in ogni intestazione.

**Un difetto trovato costruendo, non ipotetico:** col generatore Make un
`add_custom_command` consumato da più target viene copiato in ognuno, e la prima
versione assemblava `k_scheduler.vo` **tre volte** — con `-j` sono processi
paralleli che scrivono lo stesso file. Risolto con una dipendenza fra *target*
(`_vasm_order_after`) oltre a quella fra file. Con Ninja non sarebbe successo,
ma il build non deve dipendere dal generatore scelto.

**La verifica, in tre gradi.** Le invarianti danno gli stessi identici numeri
(confronto riga per riga con la baseline). Gli artefatti prodotti da CMake sono
**identici byte per byte** a quelli delle pipeline a mano — tutti i `.vo`, e i
`.vx` finché il link usa la stessa lista. E i test di `ctest` sono **sensibili**:
spostando di proposito un atteso da `98 65` a `98 66`, il test fallisce.

> **Un punto da non lasciare implicito: con l'archivio, tre `.vx` cambiano.**
> `test_mailbox`, `test_timeout` e `test_pool` non sono più identici byte per
> byte, perché l'archivio include i membri nel **proprio** ordine e non in
> quello della lista scritta a mano. Stesso numero di istruzioni, stesse
> sequenze stampate — e le invarianti di §4 sono i **valori**, non i byte
> dell'immagine. Ma è un cambiamento di layout reale e va saputo: se un giorno
> un'invariante si sposta di poco su uno di questi tre, questo è il primo posto
> dove guardare. `test_coda.vx` resta identico perché per lui l'ordine
> dell'archivio coincide con quello della lista.

**Trovato per strada e corretto**: §7.10 del manuale mostrava ancora `105/74` per
la demo HAL+kernel, numeri fermi a §3.5 — l'invariante è `98/65` da §3.14. Il
manuale non era stato riallineato dopo §3.10 e §3.14.

### 3.18 Il confine HAL/ISR/kernel, e una lacuna nell'ISA (05/09/2026)

Sessione di sola progettazione, **nessuna riga di codice scritta**. Il contenuto
sta in [§12 della proposta](proposta-kernel-realtime.md) e **non va ripetuto
qui**: qui il percorso, perché è quello che spiega perché §3.5 va riletta con
sospetto.

**Partita da una correzione.** Discutendo la decomposizione in librerie
continuavo a trovare un ciclo `hal → kernel → hal` e a trattarlo come una
fatalità del percorso di trap, citando §3.5 e attribuendo all'utente la scelta
del «simbolo kernel fisso». L'utente ha smentito: quel disegno non è suo, e la
frase in §3.5 era una verbalizzazione errata di quella sessione. **La frase è
stata tolta** — finché restava scritta, ogni sessione futura sarebbe ripartita da
una decisione che nessuno aveva preso. È lo stesso tipo di danno di §3.11, dove
una §9 persa fece riproporre un modello già bocciato.

**Il chiarimento che ha sciolto tutto** è dell'utente: «le ISR sono applicative —
una ISR usa libhal e libkernel». Il ciclo non è una fatalità: è il sintomo di un
kernel che si è messo **in mezzo** fra il vettore e l'ISR. Oggi `_trap_entry`
chiama `sched_dispatch`, e il kernel possiede il puntatore all'ISR
(`g_handler`/`irq_install` stanno in `scheduler.vasm`). Spostando quell'indirezione
nell'HAL — dove è coerente, installare un vettore è hardware — l'HAL non nomina
più nessuno e il grafo diventa un DAG, senza costi nuovi: il `jalr` su puntatore
c'è già, si sposta di un livello.

**Il contributo tecnico della sessione, e non è di disegno: manca un'istruzione.**
L'utente ha respinto come baco lo scheduler che gira a interrupt abilitati, e ha
indicato la soluzione — abilitare `IE` nella copia in registro che va nel frame,
non nella PSW attiva. Verificando sul simulatore è emerso che non è scrivibile:
`reti` fa `pc = epc; psw = epsw`, ed `epsw` non ha né lettura né scrittura, a
differenza di `epc` che ha `mfepc`/`mtepc`. Quindi **ogni `reti` riporta il
regime del task**, e i ritorni verso il kernel arriverebbero con gli interrupt
aperti. Si aggiungono `mfepsw`/`mtepsw` (§12.5), e il frame passa da 60 a 64 byte
perché la parola di stato ci entra.

**La prova che è una lacuna e non un'aggiunta**: `docs/manual.md` §4.3
**descriveva già** un cambio di contesto che «riscrive la coppia `(epc, epsw)`
con `mtepc`/`mtpsw`» — scorretto, perché `mtpsw` scrive la PSW attiva e `reti` la
sovrascrive subito dopo. La possibilità era data per scontata da chi scrisse il
manuale senza che l'istruzione esistesse. Corretto, con la spiegazione del
perché.

**Chiuso anche un punto vecchio:** `dispatcher` prende il TCB **in input**. §6
della proposta lo diceva già («salto, TCB in input»); il codice no — `scheduler`
ha il TCB in `r2` dopo `dequeue_testa`, lo scrive in `current`, e `dispatcher` lo
rilegge da lì due istruzioni dopo. Con l'input esplicito il commit di `current`
si sposta dalla politica al meccanismo, che è il difetto annotato in §5.

**Non deciso, e resta il nodo di sempre:** come un task entra nel kernel *di sua
volontà*. I tre ritorni riguardano le ISR, dove `epc`/`epsw` li ha scritti
l'hardware; per un task che si blocca su `receive` non c'è nessuna trap in corso
e nell'ISA non c'è trap software. È §8.7, ed è perché `task_block` è uno stub.

### 3.19 `mfepsw`/`mtepsw`: il passo 1 di §12.6 (05/09/2026)

Primo passo del confine nuovo, e l'unico che si potesse fare per primo: è isolato
e **falsificabile**. Il criterio non era «funziona», era che le invarianti **non
si spostassero di un ciclo** — nessun `.vasm` esistente usa le due istruzioni,
quindi qualunque movimento sarebbe stato un danno.

**Dove stanno nell'enum, e non è una svista.** Il posto logico sarebbe accanto a
`OP_MFEPC`/`OP_MTEPC`; stanno invece **in coda**. Il `.vo` serializza l'opcode
come **numero** (`(int) in->op` in `toolchain.c`), quindi inserirle in mezzo
avrebbe rinumerato tutti gli opcode successivi: ogni `.vo` del progetto avrebbe
cambiato contenuto pur restando equivalente, buttando via la proprietà «identici
byte per byte» usata come prova in §3.17. Verificato con un confronto prima/dopo
sui sei moduli: **nessun `.vo` è cambiato di un byte**.

| File | Cosa |
|---|---|
| `include/vcpu.h` | `OP_MFEPSW`/`OP_MTEPSW` in coda, con il perché |
| `src/vcpu.c` | esecuzione, timing (1 ciclo, come le altre CSR), disassemblatore |
| `src/assembler.c` | i due rami in `encode_instr`, più `mfepsw` nella tabella di `scalar_dest_reg` — scrive un registro, quindi `.proc` deve saperlo salvare |
| `docs/manual.md` §4.3 | le due righe nella tabella, e il riquadro riscritto |
| `tests/test_epsw.vasm` | **nuovo**, dà `1 0 0 7` |

**Il test dimostra il meccanismo, non l'esistenza delle istruzioni.** Arma il
timer; nell'ISR legge `epsw` (deve valere 1: è il regime del **task**) e la `psw`
attiva (deve valere 0: la ISR gira a interrupt chiusi, ed è la prova che sono due
parole distinte); poi dirotta il ritorno con `mtepc` e impone `IE=0` con
`mtepsw`; dopo la `reti` rilegge la `psw` e deve trovare 0. È il **caso 2 di
§12.4**, quello che senza `mtepsw` non si può scrivere.

**Verificata la sensibilità del test**, che è ciò che lo rende una prova:
togliendo la sola `mtepsw`, `r4` passa da 0 a **1** — si arriva nel kernel con
gli interrupt aperti, cioè esattamente il baco che l'utente aveva indicato.

---

## 4. Invarianti di regressione — come verificare che nulla si sia rotto

> ### Dal 05/09/2026 si fa con `ctest` (§3.17)
>
> ```bash
> cmake -B out -S . && cmake --build out -j && ctest --test-dir out
> ```
>
> 23 test: le tre invarianti storiche, i sei test mirati di `tests/`, la demo
> HAL+kernel, e i 13 programmi di `standalone/` che devono continuare a girare da
> soli. I numeri attesi stanno nel `CMakeLists.txt`, in un posto solo.
>
> **Questo era il premio della migrazione**, più dei path: fino a ieri la suite
> era un commento e la disciplina di chi lo eseguiva — quattro pipeline scritte a
> mano nelle intestazioni dei test, ripetute qui sotto, e il confronto con le
> sequenze attese fatto **a occhio**. Il confronto è di ciò che questa sezione
> dichiara invariante — la sequenza dei `dumps`, o le tre statistiche — non di
> tutto l'output, che renderebbe i test più fragili del contratto.
>
> Quello che segue resta valido e resta utile: è la stessa verifica fatta a mano,
> ed è il modo di isolare un singolo passo quando qualcosa si muove.

```bash
make                                    # deve compilare SENZA warning

# (1) invariante saxpy: 17 istruzioni / 40 vec-elem-ops / 94 cicli
./build/vcpu_sim standalone/saxpy.vasm

# -I: dove stanno i .vinc. Serve a ogni sorgente che ha una .include (dal
#     05/09/2026 i nomi sono nudi, §3.17); machine.vasm e linked/multi/ no.
I=-Ilinked/scheduler/include

# (2) demo HAL+kernel: deve stampare r5 = 98 e r5 = 65 (era 104/73 prima
#     dell'invariante dei link in coda.vasm — vedi §3.14, non e' una regressione)
./build/vcpu_sim asm    linked/scheduler/hal/machine.vasm      -o build/machine.vo
./build/vcpu_sim asm $I linked/scheduler/kernel/coda.vasm      -o build/coda.vo
./build/vcpu_sim asm $I linked/scheduler/kernel/scheduler.vasm -o build/scheduler.vo
./build/vcpu_sim asm $I linked/scheduler/scheduler_demo.vasm   -o build/scheduler_demo.vo
./build/vcpu_sim ld build/scheduler_demo.vo build/scheduler.vo \
                    build/coda.vo build/machine.vo -o build/scheduler_demo.vx
./build/vcpu_sim run build/scheduler_demo.vx

# (3) link multi-modulo con inclusione selettiva: 18 / 40 / 95
./build/vcpu_sim asm linked/multi/main.vasm  -o build/mainc.vo
./build/vcpu_sim asm linked/multi/saxpy.vasm -o build/msaxpy.vo
./build/vcpu_sim ld build/mainc.vo build/msaxpy.vo -o build/multi.vx
./build/vcpu_sim run build/multi.vx
```

Quarta verifica, dal 30/08/2026: il test della mailbox (§3.10), che ha bisogno
del link con `messageHandling` e `scheduler` — la pipeline completa sta
nell'intestazione di [`tests/test_mailbox.vasm`](../tests/test_mailbox.vasm).
Deve stampare `0 1 2 11 22 0 33 0 0`.

Settima verifica, dal 04/09/2026: il test del pool (§3.15), che si linka con
`pool` piu' `coda`/`machine`/`scheduler`. Pipeline nell'intestazione di
[`tests/test_pool.vasm`](../tests/test_pool.vasm). Deve stampare
`10 4 0 0 9 0 32 3 2 0 0 4 4 4 3 0 0 1 4`.

Sesta verifica, dal 04/09/2026: il test dell'invariante dei link (§3.14), che si
linka con `coda` piu' `machine`/`scheduler`. Pipeline nell'intestazione di
[`tests/test_coda.vasm`](../tests/test_coda.vasm). Deve stampare
`0 1 1 1 0 0 0 2 1 0 0 0 0`.

Quinta verifica, dal 04/09/2026: il test del vettore di descrittori (§3.13), che
si linka con `timeout.vasm` più `machine`/`scheduler`/`coda` (nessuno dei tre
gira: `machine.vasm` entra solo per `irq_save`/`irq_restore` e si tira dietro il
resto). Pipeline nell'intestazione di
[`tests/test_timeout.vasm`](../tests/test_timeout.vasm). Deve stampare
`3 0 150 1 2 0 0 0 3 0 1`.

Ottava verifica, dal 05/09/2026: il test di `-I` e dell'idempotenza di
`.include` (§3.16). Non ha bisogno del link e non usa la macchina se non per
rendere osservabile l'esito. **È l'unica pipeline del repo che richiede `-I`**,
quindi è anche l'unico sorgente che `asm` senza `-I` rifiuta di proposito:

```bash
./build/vcpu_sim -I linked/scheduler/include tests/test_include.vasm
#   16 12 16 512 1
```

Nona verifica, dal 05/09/2026: il test di `mfepsw`/`mtepsw` (§3.19). Gira da
solo, e dimostra il caso 2 di §12.4 — non che le istruzioni esistano, ma che una
ISR possa tornare verso il kernel a interrupt disabilitati:

```bash
./build/vcpu_sim tests/test_epsw.vasm
#   1 0 0 7
```

Stato verificato il 05/09/2026 (dopo §3.19): **tutti i numeri identici a quelli
di prima dell'aggiunta all'ISA**, nemmeno un ciclo di scarto, e i `.vo` identici
byte per byte. `ctest` dà 23/23.

Stato verificato il 05/09/2026 (dopo §3.16): **tutti i numeri identici a prima**
— (1) 17/40/94, **(2) 98/65 con 8 tick**, (3) 18/40/95, `test_proc` 100/200/300,
e i cinque test di `tests/` con le sequenze attese. `asm` pulito su tutti i 29
sorgenti di `standalone/`, `linked/` e `tests/` (passando `-I
linked/scheduler/include`, innocuo per i 28 che non ne hanno bisogno), e tutti i
13 programmi di `standalone/` girano ancora da soli.

Stato verificato il 04/09/2026 (dopo §3.15): (1) 17/40/94, **(2) 98/65 con 8
tick**, (3) 18/40/95, `test_proc` 100/200/300, e i quattro test di `tests/` con
le sequenze attese. `asm` pulito su tutti i 28 sorgenti di `standalone/`,
`linked/` e `tests/`, e tutti i 13 programmi di `standalone/` girano ancora da
soli.

Stato verificato il 30/08/2026: (1) e (3) danno gli stessi identici numeri di
sempre; (2) dà 104/73 (era 105/74 dopo §3.5, 99/66 dopo §3.4, 102/70 prima
ancora — vedi §3.10 e §3.5 per il perché), stesso numero di tick (8) e
stessa alternanza dei task. `asm` pulito su tutti i 20 sorgenti di
`standalone/`, `linked/multi/`, `linked/scheduler/`, inclusi i test mirati di
`.proc`/`.endproc` (casi validi ed errore, sia a file singolo sia via
`asm`+`ld`).

> Nota: `linked/scheduler/scheduler_demo.vasm` **fallisce di proposito** in
> modalità legacy a file singolo (`./build/vcpu_sim linked/scheduler/scheduler_demo.vasm`),
> perché ha `.extern ready`: va necessariamente linkato. Non è una regressione.

---

## 5. Prossimi passi possibili

Ci sono tre fronti: quello su cui si sta lavorando adesso, quello fermo in attesa
di una decisione, e quello di vecchia data.

### Messaggi e interfacce dei servizi (FRONTE ATTIVO)

**Questo fronte è arrivato in fondo a ciò che si poteva scrivere.** Sono fatti e
testati: la mailbox (§3.10), il vettore di descrittori con
`timeout_arm`/`timeout_cancel` (§3.13), l'invariante dei link con il puntatore
nullo (§3.14) e il **pool di buffer** (§3.15). Quello che resta — la scansione
delle scadenze e il ciclo del task-gestore — dipende da §8.7 e da §7.4, quindi il
lavoro passa all'altro fronte.

> ### ⚠ La scelta «gestore = task» ha spostato le dipendenze
>
> Finché il gestore girava **dentro l'ISR del tick**, questa sezione poteva dire
> che non era bloccato da niente: contesto già salvo, interrupt già disabilitati,
> unico atto una `send` raw. Come **task** (§9.2) non è più vero, ed è il prezzo
> consapevole della decisione — la scansione O(N) esce dal tempo a interrupt
> disabilitati, ma il gestore diventa un cliente dello scheduler:
>
> - fa `receive` sulla propria mailbox, quindi ha bisogno della **commutazione
>   volontaria** che manca (§8.7: nell'ISA non c'è trap software, serve una
>   routine HAL che fabbrichi un frame di trap finto — è anche il motivo per cui
>   `ctx_init` non è eliminabile);
> - «a priorità massima» ha senso solo con lo **scheduler a priorità**, che è
>   fermo su §7.4.
>
> Erano scrivibili subito due pezzi. Il **vettore di descrittori** con
> `timeout_arm`/`timeout_cancel` è fatto (§3.13): sono manipolazioni in sezione
> critica e non toccano nessuna mailbox, tanto che il suo test non ne usa
> nessuna. Resta il **pool** — una `TESTA` con i buffer come nodi, quindi
> `buf_alloc`/`buf_free` sono `dequeue_testa_s`/`enqueue_coda_s` e non c'è
> meccanismo nuovo — verificabile con lo stesso trucco di
> `tests/test_mailbox.vasm` (in un sistema a un flusso solo, «bloccarsi» equivale
> a «far girare adesso la controparte»).

Le domande di §9.5 sono **chiuse tutte e tre**: nessun handle e cancellazione per
identità (§3.13); la taglia dei buffer, che non è una ma sei classi per potenze di
due (§3.14, §10 della proposta); e il pool in `kernel/`, perché non legge ciò che
distribuisce. Resta aperto:

1. **Dove collocare il gestore dei timeout.** La parte scritta (vettore, `arm`,
   `cancel`) non legge nessun payload, quindi sta legittimamente in
   `kernel/timeout.vasm`. La parte che formatta il payload — scansione e consegna
   — per §8.4 non è `kernel/`: quando si scriverà, o il file si sposta o si
   divide.

L'idempotenza di `.include`, che questo fronte aveva lasciato aperta (§3.10)
perché col modello «un `.vinc` per fornitore» diventa un problema appena i file
di interfaccia sono due, **è stata fatta il 05/09/2026** (§3.16) — dall'altro
fronte, perché lì era un prerequisito.

### Riscrittura dello scheduler a priorità statiche (FERMO SU §7.4)

**È qui che riprende il lavoro.** Discussione del 29/08/2026, verbalizzata per
intero in [`docs/proposta-kernel-realtime.md`](proposta-kernel-realtime.md)
(diagrammi Mermaid inclusi): l'utente ha bocciato il disegno attuale a coda
singola e ha specificato un modello a **priorità statiche con un PCB per
livello**, dove il TCB preemptato viene tenuto in un campo dedicato del PCB
invece di tornare in fondo alla coda.

Il difetto che ha innescato tutto: l'attuale `scheduler`
([`kernel/scheduler.vasm:118-135`](../linked/scheduler/kernel/scheduler.vasm#L118-L135))
riaccoda **sempre** il task uscente, il che rende il blocking su semaforo
inesprimibile — pur essendo promesso dal modello a 3 stati dichiarato
nell'intestazione dello stesso file. Inoltre ciò che si chiama "politica"
contiene in realtà transizioni di stato, manipolazione di code e commit di
`current`: cambiare politica non richiederebbe di riscrivere solo quella.

**Decisioni già prese** (§7.1–7.3 e §7.5 della proposta):

1. `current` sopravvive → lo slot si riempie **nell'istante** della preemption,
   non mentre il task gira.
2. `TCB.state` **resta** — serve al debug e serve a sapere in quale coda il TCB
   si trova adesso, che `TCB.pcb` da solo non dice. Ma i valori diventano
   **quattro**: aggiunto `PREEMPTED`.
3. Il campo di priorità nel TCB contiene **l'indirizzo del PCB**, non il numero
   di livello (si chiamerà `pcb`). Porta con sé un invariante da non perdere di
   vista: la tabella dei PCB va disposta in memoria **in ordine di priorità**,
   altrimenti i confronti fra puntatori — che servono per decidere la preemption
   al risveglio di un task — smettono di essere confronti fra priorità, in
   silenzio.

4. **Il TCB ha una sola coppia di link** (§7.5, decisa il 30/08/2026), quindi
   `coda.vasm` resta intatta e il suo contratto «il puntatore al link È il
   puntatore al buffer» pure. La decisione regge, ma **l'argomento che la
   sostiene è cambiato** con la riprogettazione dei timeout (§3.11): non più
   «in lista ci va il messaggio invece del TCB», bensì — più semplicemente —
   che un timeout armato **non è in nessuna lista**, è una casella di un vettore
   statico (§9.3 della proposta). Il task aspetta in un posto solo, la propria
   mailbox. Conseguenze da non perdere di vista, invariate: **la mailbox è
   l'unico punto di blocco di un task** e **`sem_wait` non ha timeout, per
   costruzione**; inoltre il secondo argomento tecnico di §7.2 si indebolisce
   (da rivedere).

**Decisione aperta, da riprendere per prima** (§7.4): per l'inversione di
priorità sui **mutex** (non sui semafori: senza proprietario non c'è nessuno da
promuovere), si va di **ereditarietà** o di **priority ceiling**? Cambia cosa va
dichiarato staticamente — l'ereditarietà vuole una `TESTA` in più nel TCB per la
lista dei mutex posseduti, il ceiling vuole un campo nel mutex e nient'altro. La
proposta raccomanda il ceiling, perché con tutto statico il ceiling è calcolabile
a compile-time; l'utente non ha ancora deciso.

Argomento aggiuntivo emerso il 30/08/2026 a favore del ceiling, **non ancora
verbalizzato nella proposta perché la decisione resta dell'utente**: è più forte
di quello sulla calcolabilità a compile-time. L'ereditarietà promuove il
**possessore** del mutex, che per definizione non sta girando — quindi sta
dentro la coda del suo livello o nello slot `preemptato`, e la promozione deve
**spostarlo fisicamente** fra i livelli. Se lo slot del livello di destinazione è
già occupato, il promosso finisce nella coda e viene scavalcato, il che
contraddice le premesse dell'argomento di §4 per cui «un solo campo `preemptato`
basta». Col ceiling la promozione tocca **solo `current`**, che per §7.1 è fuori
da ogni coda e da ogni slot: zero chirurgia sulle liste. Invariante che ne
discende: **`TCB.pcb` cambia solo per il task puntato da `current`**. In più,
sotto ICPP su monoprocessore la coda d'attesa del mutex non viene mai usata
(nessuno che voglia il mutex può preemptare chi lo tiene), quindi non serve
l'inserimento ordinato per priorità che `coda.vasm` non ha. Il rischio del
ceiling («il ceiling dichiarato male salta in silenzio») si toglie con **una
`blt`** in `mutex_lock`: se `current.pcb` è più prioritario di `mutex.ceiling`,
errore rumoroso.

**Due domande minori ancora aperte sul TCB** (la prima delle tre, i link, è
chiusa in §7.5):

- **Chi costruisce il primo frame di contesto.** §11 della proposta dice che
  `ctx_init` è «probabilmente eliminabile» con l'init statica. Obiezione
  sollevata il 30/08/2026: il layout del frame (60 byte) è conoscenza dell'HAL,
  e scriverlo a mano in `.data` lo duplica fuori da `machine.vasm` — il giorno
  che `ctx_save` cambia, i frame statici restano validi e sbagliati. In più «il
  primo task parte come il millesimo switch» è vero solo se `ctx_init` resta.
- **`entry` e `stack_top`: nel TCB o in una tabella di boot a parte.** Se
  `ctx_init` resta, il ciclo di boot ha bisogno di entrambi per ogni task.
  Metterli nel TCB tiene la descrizione statica di un task in un posto solo, dà
  gratis un'**identità** al TCB per il debug (oggi si distingue solo per
  indirizzo, e la catena degli slot occupati vale meno se stampa indirizzi), e
  `stack_top` serve comunque per un controllo di overflow.

**Non si scrive codice finché §7.4 non è chiusa**: tocca il layout di strutture
statiche.

`hal/machine.vasm` e `kernel/coda.vasm` sopravvivono intatti al ridisegno.

### Ristrutturazione del build in target CMake (FATTA il 05/09/2026, salvo il punto 3)

**Tre punti su quattro sono fatti**: `-I` e l'idempotenza (§3.16), le `.include`
a nome nudo e i target CMake con `ctest` (§3.17). Resta il punto 3,
`--emit-deps`, che non blocca niente. I `.vasm` sono decomposti in **librerie e
librerie di interfaccia**, i path non stanno più nei sorgenti, e la suite di
regressione è `ctest`. Era l'**unico lavoro che si potesse fare senza aver deciso
§7.4**, quindi da qui il fronte torna a essere lo scheduler.

Il testo che segue è la fotografia del problema com'era prima, tenuta perché
spiega perché l'ordine di lavoro era quello.

Qui i `.vinc` sono header-only *per costruzione* (solo costanti di compile-time,
non emettono un byte), quindi la `INTERFACE` library li modella esattamente. Il
blocco non era in CMake ma nell'assembler: una `INTERFACE` library può propagare
una cartella di include solo se l'assembler è disposto a riceverla, e `-I` non
c'era. `.include` risolve rispetto alla cartella del file di *primo livello*,
quindi il nome di una dipendenza dipendeva da chi la include:

```
linked/scheduler/kernel/pool.vasm    .include "../include/pool.vinc"
tests/test_pool.vasm                 .include "../linked/scheduler/include/pool.vinc"
```

Stesso file, due grafie. Finché è così, migrare a CMake sposterebbe i path nel
`CMakeLists.txt` invece di toglierli.

**Ordine di lavoro:**

1. ~~**`-I` nell'assembler** e **`.include` idempotente**.~~ **FATTO** (§3.16):
   il flag c'è su `asm` e sul percorso legacy, l'idempotenza è per identità del
   file sul disco (`realpath`), e la regola «i `.vinc` sono foglia» è caduta. Le
   invarianti danno gli stessi identici numeri. Le 12 `.include` **non sono state
   toccate**: continuano a risolversi come sempre, ed è il punto 2 a cambiarle.
2. ~~**Riscrivere le 12 `.include` a nome nudo**~~ **FATTO** (§3.17), insieme al
   punto 4 perché sono accoppiati: il 2 da solo fa crescere di un flag ogni
   comando a mano, e il 4 senza il 2 sposta i path nel `CMakeLists.txt`.
3. **`--emit-deps`** nell'assembler — **l'unico rimasto aperto**, e non è
   urgente: oggi le dipendenze sono *dichiarate* nel `CMakeLists.txt` (`LIBS
   vinc_pool`), il che basta a far scattare il rebuild e **non** riporta i path
   nel build, perché si scrivono una volta sola sul target dell'interfaccia. Ciò
   che manca è la dipendenza *scoperta*: se un `.vasm` include un `.vinc` che
   `LIBS` non nomina, CMake non lo saprà mai e il rebuild incrementale mentirà.
   Lo stack di include dell'assembler conosce già tutti i file che apre.
4. ~~CMake: `INTERFACE` per i `.vinc`, `vasm_library`/`vasm_program` sopra
   `asm`/`ld`, archivio `.va` per il kernel, **`ctest` per le invarianti**.~~
   **FATTO** (§3.17). Il `Makefile` non è stato toccato: i due build convivono
   in cartelle diverse, e ritirarlo è una decisione a parte.

**Le tre cose non ovvie, da non riscoprire a metà migrazione:**

- ~~**L'idempotenza di `.include` smette di essere un accessorio e diventa un
  prerequisito.**~~ **Risolta al punto 1.** Era questa: la modellazione giusta è
  `vinc_pool` che dipende da `vinc_types` (`BLOCCO` *è* un nodo di lista), ma non
  si poteva scrivere, e con le `INTERFACE` library la disciplina «i `.vinc` sono
  foglia» non sarebbe stata più applicabile, perché la propagazione è transitiva
  per definizione e nessuno può impedire che lo stesso `.vinc` arrivi due volte.
  Ora arriva due volte e non succede niente. **Quello che resta** è che un nome
  si risolve rispetto al file di **primo livello**, non rispetto a chi include:
  quindi `pool.vinc` potrà includere `types.vinc` solo per **nome nudo**, e solo
  una volta che chi lo assembla passa `-I`. È il punto 2, ed è il motivo per cui
  i due `.vinc` sono ancora foglia nei fatti.
- **CMake non traccia le dipendenze di un linguaggio custom.** Non saprà mai da
  solo che `test_pool.vasm` dipende da `pool.vinc`. O si elencano nei `DEPENDS`
  — e i path tornano, spostati nel build — oppure serve il depfile del punto 3.
  Lo stack di include dell'assembler conosce già tutti i file che apre.
- **`INTERFACE_INCLUDE_DIRECTORIES` non arriva da sola a un `add_custom_command`**:
  è una proprietà pensata per C/C++. Va riletta con
  `$<TARGET_PROPERTY:vinc_pool,INTERFACE_INCLUDE_DIRECTORIES>` e trasformata in
  `-I` dentro la funzione `vasm_library`. È il trucco standard, ma è lavoro
  nostro, non magia di CMake.

**Pezzo già costruito e non usato:** `ar` e gli archivi `.va` con inclusione
selettiva esistono dal commit `2111646`. Oggi ogni test si porta in testa una
lista ordinata di `.vo` scritta a mano; con un archivio del kernel diventa una
libreria sola. La chiusura non sparisce — `machine.vo` serve per `irq_save` e
referenzia `sched_dispatch`, quindi tira dentro `scheduler.vo` comunque — ma
diventa una conseguenza del codice invece che una lista da mantenere in quattro
intestazioni.

**Il premio vero non sono i path**: oggi la suite di regressione è un commento e
la disciplina di chi la esegue — quattro pipeline scritte a mano nelle
intestazioni dei test, ripetute in §4, e il confronto con le sequenze attese
fatto a occhio. Con `ctest` diventa una verifica della macchina.

**Vincolo non negoziabile:** alla fine le invarianti devono dare gli **stessi
identici numeri** (§4). Una ristrutturazione del build che sposta `98/65` non è
una ristrutturazione del build.

> **Trovato per strada, indipendente da CMake e valido comunque.** Sul lato C i
> confini degli header non coincidono con quelli dei sorgenti: `assemble()` è
> dichiarata in `include/vcpu.h` e `assemble_object()` in `include/toolchain.h`,
> ma **sono implementate tutte e due in `src/assembler.c`**. L'assembler non ha
> un header proprio, e l'header dello strato più basso dichiara una funzione che
> sta due strati sopra. Oggi non si vede perché il build è un blob solo con
> `-Iinclude` globale. Se un giorno si divide anche il C in target, questo va
> sistemato **prima**, se no il confine è finto: una libreria esporterebbe un
> header che dichiara un simbolo che non definisce. Il grafo delle
> implementazioni invece è già pulito (`vcpu` → `toolchain` → `assembler` →
> `main`, verificato sugli `#include`): i confini li stiamo già rispettando,
> semplicemente nessuno li impone.

### Front-end `vc` (il pezzo mancante di vecchia data)

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

**Per il confine HAL/ISR/kernel (non dipende da §7.4) — CONSIGLIATA:**
```
Leggi docs/stato-lavori.md §3.18/§3.19 e docs/proposta-kernel-realtime.md
§12. Il passo 1 di §12.6 e' fatto: mfepsw/mtepsw sono nell'ISA.
Continua dal passo 2: la parola di stato nel frame di contesto (60 -> 64
byte) e hal.vinc con CTX_FRAME_SIZE e PSW_IE.
```

**Per il build in target CMake — quasi finito:**
```
Leggi docs/stato-lavori.md, la sezione di §5 sulla ristrutturazione del
build. Punti 1, 2 e 4 fatti (§3.16, §3.17): -I, .include idempotente,
nomi nudi e target CMake con ctest. Resta il punto 3, --emit-deps
nell'assembler, per avere le dipendenze scoperte invece che dichiarate.
Alla fine `ctest --test-dir out` deve dare 22/22.
```

**Per lo scheduler (la decisione che sblocca il kernel):**
```
Leggi docs/stato-lavori.md e docs/proposta-kernel-realtime.md.
Mailbox, vettore di descrittori, invariante dei link e pool di buffer sono
scritti e testati: tutto cio' che non dipende dallo scheduler e' fatto.
Riprendiamo dalla decisione aperta §7.4: ereditarieta' di priorita' o
priority ceiling per i mutex.
```

**Per andare invece sullo scheduler (fermo su una decisione):**
```
Leggi docs/stato-lavori.md e docs/proposta-kernel-realtime.md.
Riprendiamo dalla decisione aperta §7.4: ereditarieta' di priorita' o
priority ceiling per i mutex.
```

**Per andare sul linguaggio ad alto livello:**
```
Leggi docs/stato-lavori.md e docs/proposta-linguaggio-alto-livello.md.
Implementa la fase 1 del front-end vc: lexer + parser + il costrutto
a[:] = espr con + - * elementwise, che genera .vasm.
Criterio di successo: saxpy in vc deve dare 17 istruzioni / 40 vec-elem-ops / 94 cicli.
```

Utile da sapere: il modello si cambia con `/model`, ed è ora impostato su Opus
come default in `~/.claude/settings.json`. Il contesto del progetto si
ricostruisce in fretta perché il repo è piccolo (~6.500 righe totali) e i
**quattro** documenti in `docs/` sono aggiornati: `stato-lavori.md` (questo),
`manual.md`, `proposta-kernel-realtime.md` (fronte attivo) e
`proposta-linguaggio-alto-livello.md`.

I sorgenti C sono a **2 spazi** dal 29/08/2026 (§3.7): scrivere nuovo codice
con la stessa convenzione.
