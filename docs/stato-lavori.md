# Stato dei lavori — `vcpu_sim`

> Ultimo aggiornamento: **28 agosto 2026**
> Scopo: fotografia dello stato per riprendere il lavoro a distanza di giorni
> senza dover ricostruire il contesto.

---

## 0. STATO ATTUALE — tutto committato, working tree pulito

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

**Working tree pulito** (verificato `git status --short`: solo `.vscode/` non
tracciato, di proposito — vedi §2). Nessun lavoro pendente da riprendere sul
codice: la prossima sessione può ripartire da zero sui prossimi passi (§5), a
scelta dell'utente. Non è stato fatto `git push` — resta da chiedere
esplicitamente se serve.

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
| HAL + kernel + scheduler RR | completo, ridisegnato (§3.5), NON committato | [`linked/scheduler/`](../linked/scheduler/) |
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

```
ae310d5 Ridisegno HAL/kernel a tre confini + auto-save registri in .proc/.endproc  (locale, NON pushato)
98a40a0 Aggiorna handoff: lavoro pendente committato                              <- ultimo pushato
ef10e3b Riorganizza sorgenti .vasm: examples/ -> standalone/ + linked/, doc aggiornata
3434b6d HAL + kernel puro + demo scheduler a preemption differita
2111646 Assembler: direttive di compile-time (.equ/.struct/.field/.res/.include)
3d1b790 Toolchain: reloc R_ADDR per puntatori a funzione (li di simbolo)
55b638e Snapshot iniziale: simulatore vCPU vettoriale + toolchain + scheduler RR
```

`ae310d5` contiene tutto il lavoro di §3.5 (ridisegno HAL/kernel a tre
confini) e §3.6 (auto-save registri in `.proc`/`.endproc` + `--emit-expanded`
+ rifiniture ai commenti del listato espanso), rimasto non committato per tre
sessioni — l'utente ha esplicitamente chiesto **di non pushare** dopo il
commit, quindi il branch locale resta avanti di 1 commit rispetto a
`origin/master` finché non verrà chiesto di nuovo.

**Working tree pulito** (a parte `.vscode/`, mai tracciato, di proposito — da
valutare se aggiungere a `.gitignore` in futuro, non urgente).

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
   `g_resched`, una variabile del kernel. Confrontate le due alternative
   simmetria vs. fedeltà storica, l'utente ha scelto la **simmetria**: l'HAL
   chiama un **unico simbolo kernel fisso** (`sched_dispatch`) e non sa/non
   legge nient'altro; tutta la logica (ISR, flag, politica, dispatch) resta nel
   kernel.

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

**Prossimo passo:** proporre all'utente il commit — working tree sporco da tre
sessioni (§3.4/§3.5/§3.6), mai committato, ora tutto verificato (vedi §2). Il
nuovo `tests/test_proc.vasm` è il primo file sotto una directory `tests/`, mai
esistita prima in questo repo: da valutare con l'utente se diventa la sede
stabile per test mirati futuri o se va spostato/rinominato.

---

## 4. Invarianti di regressione — come verificare che nulla si sia rotto

```bash
make                                    # deve compilare SENZA warning

# (1) invariante saxpy: 17 istruzioni / 40 vec-elem-ops / 94 cicli
./build/vcpu_sim standalone/saxpy.vasm

# (2) demo HAL+kernel: deve stampare r5 = 105 e r5 = 74
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

Stato verificato il 27/08/2026: (1) e (3) danno gli stessi identici numeri di
prima; (2) dà 105/74 dopo il ridisegno di §3.5 (era 99/66 dopo quello di §3.4,
102/70 prima ancora — vedi §3.5 per il perché), stesso numero di tick (8) e
stessa alternanza dei task. `asm` pulito su tutti i 20 sorgenti di
`standalone/`, `linked/multi/`, `linked/scheduler/`, inclusi i test mirati di
`.proc`/`.endproc` (casi validi ed errore, sia a file singolo sia via
`asm`+`ld`).

> Nota: `linked/scheduler/scheduler_demo.vasm` **fallisce di proposito** in
> modalità legacy a file singolo (`./build/vcpu_sim linked/scheduler/scheduler_demo.vasm`),
> perché ha `.extern ready`: va necessariamente linkato. Non è una regressione.

---

## 5. Prossimi passi possibili

Il ridisegno di §3.5 è nel working tree, non ancora committato (§2) — prima
rifinire `.proc`/`.endproc` (§0), poi valutare il commit. A parte questo,
l'unico pezzo che manca al disegno complessivo è il front-end `vc`.

### Front-end `vc` (il pezzo mancante)
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

Utile da sapere: il modello si cambia con `/model` (questa sessione girava su
Opus 5). Il contesto del progetto si ricostruisce in fretta perché il repo è
piccolo (~6.500 righe totali) e i tre documenti in `docs/` sono aggiornati.
