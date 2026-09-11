# Stato dei lavori — `vcpu_sim`

> Ultimo aggiornamento: **11 settembre 2026** (§3.36 `TCB.crit`; §3.37 MMIO e la
> tastiera; §3.38 lo scheduler senza il tick)
> Scopo: fotografia dello stato per riprendere il lavoro a distanza di giorni
> senza dover ricostruire il contesto.

---

## 0. STATO ATTUALE — DA DOVE SI RIPRENDE

> ### ▶ RIPRENDI DA QUI (12/09/2026 o dopo)
>
> `ctest` **30/30**. L'11/09 ha fatto tre cose. **§3.36** ha chiuso la
> discussione rimasta aperta a metà frase: **chi è in sezione critica non si
> ruota**, `TCB.crit`, ceiling classico. **§3.37** ha dato alla macchina un
> **ingresso**: MMIO sopra la RAM e una tastiera letta in polling, con il test
> `kbd` deterministico pur dipendendo dal mondo. **§3.38** ha scritto il primo
> programma del progetto **senza un solo interrupt** — e ha stanato tre difetti,
> uno dell'assembler e due di `traccia.py`.
>
> Pushato fino a `e9e74a3` (§3.36 e §3.37). Il push resta **una richiesta da
> rifare ogni volta**.
>
> ### ▶▶ DOVE SI STAVA ANDANDO
>
> L'utente vuole **vedere il sistema funzionante** prima di nuove
> implementazioni, e ha impostato la strada: prima due test «stupidi» senza
> registri vettoriali, guidati da **eventi** invece che dal tick — ed è per
> questo che la tastiera è arrivata adesso. Poi un eseguibile che usi lo stato
> dell'arte del sistema.
>
> **Il primo dei due è fatto** (§3.38, `test_coop`). Il secondo non è scritto:
> guidato dalla tastiera, sempre senza tick — un task legge in polling, manda il
> carattere a un altro e si blocca aspettando l'ack. Lì l'idle **può** girare,
> perché c'è un mondo esterno a riempire il vuoto: è lo stesso sistema di
> `test_coop` con e senza qualcosa che possa succedere.
>
> La cosa che ho proposto di farne, e che resta da decidere: che
> quell'applicazione faccia un **lavoro vero**, cioè un calcolo vettoriale, così
> che la dimostrazione e la misura siano la stessa cosa. Perché il buco più
> grosso del progetto, verificato l'11/09, è che **le due metà non si sono mai
> incontrate**: non c'è una sola istruzione vettoriale sotto `rtos/` e `hal/`, e
> `machine.vasm:17-19` dichiara i registri vettoriali *volatili attraverso la
> preemption*. Oggi due task che usassero `v0..v7` si corromperebbero a vicenda,
> e nessun test se ne accorge perché nessun task li usa. È il fronte (2) dei tre
> concordati il 07/09; il (1) è chiuso, il (3) — linker/locator — è intatto.
>
> > ### LA DECISIONE, IN QUATTRO RIGHE
> >
> > La rotazione fra pari è l'unico meccanismo del kernel che agisce
> > sull'**uguale**, e al livello di un ceiling l'uguale sono gli **utenti** del
> > mutex: li mette in esecuzione mentre il possessore tiene la risorsa, e la
> > coda che §13.5 promette vuota si popola con il ceiling dichiarato **bene**.
> >
> > Rimedio: un contatore nel TCB (`TCB.crit`), che `sched_preempt` legge prima
> > di ruotare. **Non** la priorità nominale — quando a prendere il mutex è
> > l'utente più prioritario la promozione è *vuota* e non si distinguerebbe
> > niente, proprio nel caso peggiore.
> >
> > Scartate, e vale la pena sapere perché: il **ceiling universale** (è
> > `sched_lock` travestito) e il **ceiling un livello sopra** (sposta il
> > blocking su task che quel mutex non lo usano). La storia intera è in §3.36.
>
> ### ▶▶ §13 È CHIUSA, E RESTA UN PUNTO SOLO
>
> Il kernel realtime ha adesso i suoi **tre punti di blocco**, e la differenza
> fra loro non è di implementazione ma di *cosa stai aspettando*:
>
> | | cosa aspetti | timeout |
> |---|---|---|
> | **mailbox** | il **mondo**, che può non rispondere | sì (§9) |
> | **semaforo** | una **risorsa** che un altro task qui dentro restituirà entro un tempo calcolabile | no, ed è una deduzione (§13.8) |
> | **mutex** | niente: sotto un ceiling corretto non blocca mai. La coda esiste per far **degradare** una dichiarazione sbagliata (§13.5) | — |
>
> La riga del mutex regge su una proprietà dello **scheduler**, non del mutex, e
> dall'11/09 è dichiarata: chi ha `TCB.crit != 0` non viene ruotato fra pari.
> Chi tocca `sp_mio_livello` sta toccando l'invariante di §13.5 (§3.36).
>
> Quello che resta aperto è **uno**, ed è **§13.7**: `mutex_unlock` arma
> `request_preempt` come la sezione prescrive, ma da contesto di task un
> «percorso di uscita» non c'è, quindi il flag aspetta il **tick**. Chiuderlo è
> scrivere `task_yield` — le otto righe di `task_block` con `enqueue_coda` al
> posto di `SUSPENDED` — ed è **kernel**, non servizio: non l'ho scritto perché
> §13.7 prescrive l'altra cosa. È una latenza, non una scorrettezza.
>
> Due cose da sapere leggendo il codice nuovo:
>
> - **`coda_peek` è dell'utente**, e la forma finale pure: un argomento solo, e
>   la fine del giro la riconosce il chiamante **contando**. Un ciclo limitato da
>   un conteggio non scappa nemmeno su una lista corrotta, ed è il limite che
>   §13.6 chiede di dichiarare invece di lasciare implicito;
> - **`prio_pcb` è l'unico punto in cui una priorità è un numero.** Ovunque
>   altrove è l'indirizzo di un PCB (§7.3). Il buco che colma non era del mutex:
>   `tcb.vinc` pubblicava `PRIO_MAX`/`PRIO_IDLE` senza che niente potesse
>   convertirli, e ogni boot scriveva `li r4, pcb2` a mano.
>
> ### ▶▶ §3.28 È CHIUSA: LA SIMULAZIONE COMPLETA GIRA
>
> Il 07/09 ha portato lo scheduler da round-robin a coda singola a **priorità
> statiche con PCB**, poi fino al blocco volontario, poi fino alla catena di
> §3.28. In ordine di lettura:
>
> - **§3.27** i link dei nodi in un posto solo (`.struct LINK` annidata), e
>   `MAILBOX.count` come `.equ` **derivato** invece che copiato;
> - **§3.28** `dispatcher(TCB)` con l'input esplicito (§12.3), e la convenzione
>   **0 = priorità più alta**, da cui il verso dei confronti fra PCB;
> - **§3.29** il modello a PCB gira; `scheduler_demo` **ritirato** e sostituito
>   da `rtos/test/test_scheduler.vasm`;
> - **§3.30** `task_block`/`task_ready` (§8.8), e la **rotazione fra pari** — la
>   distinzione di §2 si risolve senza sapere chi ha chiamato: se la scansione
>   trova qualcuno **prima** del livello dell'uscente era preemption (slot), se
>   arriva al suo livello senza trovare nessuno era fine turno (in fondo alla
>   coda). **Il tick è il quanto**. Dall'11/09 con un'eccezione sola: chi tiene
>   una sezione critica non ruota (§3.36);
> - **§3.31** `test_block`: un task che si blocca su una mailbox vuota, l'ISR
>   che gli consegna, e l'idle che gira **mentre lui dorme** — la CPU libera
>   misurata sul serio;
> - **§3.32** `test_catena`: A→B→C, cioè il **primo dei due passi** della
>   simulazione di §3.28. Mette in gioco tre percorsi mai eseguiti prima —
>   `send_s` da task, il ramo di `task_ready` che **non** preempta, la scansione
>   che attraversa livelli popolati — e, misurando, ha mostrato la
>   **saturazione**: a 800 cicli di periodo l'ultimo anello muore di fame senza
>   che nessuna asserzione scatti;
> - **§3.34** `tools/traccia.py`, la **traccia temporale**: chi gira e in quale
>   intervallo, e quanto di quel tempo è kernel per suo conto. Chiude il debito
>   aperto da §3.29, che adesso ha una risposta;
> - **§3.33** `rtos/gestore_timeout/`, il **primo task di sistema** del progetto
>   (cartella nuova, decisa dall'utente), e `test_gestore`: §8, §9 e §10 girano
>   per la prima volta insieme sotto lo scheduler vero. **§3.28 è chiusa.** E
>   §9.2 aveva un test che non poteva funzionare — `count == 0` mentre il caso
>   normale è `count == −1` — corretto e misurato.
>
> ### ▶▶▶ IL PROSSIMO PASSO
>
> Il kernel non ha più un fronte obbligato: §13 è chiusa e §3.28 pure. Le tre
> strade, in ordine di quanto valgono:
>
> 1. **`task_yield` e §13.7**, sopra. È corta, è l'ultimo pezzo di §13, e
>    tocca il kernel — quindi va decisa, non dedotta;
> 2. **il semaforo davanti al pool** (§13.8, terzo corollario): oggi chi trova
>    vuota una classe riceve `POOL_VUOTO` e ripassa più tardi (§9.2). È il primo
>    cliente vero che il semaforo avrebbe, e il limite è dichiarato — chi gira
>    nel percorso del tick non può bloccarsi, quindi il gestore dei timeout
>    resterebbe sul ramo non bloccante;
> 3. **gli `_api`** per `pool`, `timeout`, `messaggio`, `hal` — l'ultima riga
>    rimasta della tabella di §3.26. L'HAL è quello che rende di più: oggi
>    «`irq_save` restituisce la psw in `r5`» si scopre solo leggendo
>    `machine.vasm`.
>
> Più una piccola, che l'11/09 ha messo in luce senza chiuderla: **la seconda
> causa di §13.5 non è testata**. `test_mutex` prova il ceiling dichiarato male,
> non il possessore che si blocca volontariamente dentro la sezione critica —
> l'altra premessa che fa popolare la coda.
>
> ### ⚠ COSE CHE NON SONO NELLA PROPOSTA, E ALTRI DEBITI
>
> - ~~**cinque voci di §3.26**~~ — **RIPORTATE** il 10/09 (§3.35). Della tabella
>   in fondo a §3.26 resta solo l'ultima riga, gli `_api`, che non riguarda §13;
> - ~~`SEMAFORO.risorse`~~ — **SCRITTO** (§3.35), nella forma decisa: un `.equ`
>   derivato da `TESTA.count`. Con una correzione a §13.1 che il codice ha
>   imposto — il `sem: .word 0, 0, 10` della proposta **non è scrivibile**,
>   perché una `TESTA` vuota non è fatta di zeri;
> - ~~**la traccia temporale**~~ — **FATTA** (§3.34): `tools/traccia.py` dice chi
>   gira e in quale intervallo, e la domanda di §3.29 ha una risposta (l'idle non
>   conta il doppio, ha **fasce** molto più lunghe);
> - il **linker/locator** (`.align`, `.section`, regioni e mappa) e la domanda
>   sul **contesto vettoriale** nel context switch: entrambi in coda a §3.27, ed
>   entrambi fuori dal kernel ma rilevanti.
>
> ### (storico) Come si è arrivati qui
>
> Il **07/09/2026** (§3.26) ha sciolto la decisione che bloccava tutto e ha
> scritto i primi due pezzi. In una riga ciascuno:
>
> - **§13.8 non si sceglieva, si deduceva.** Il timeout è una consegna in
>   mailbox (§9, decisa, con §10 implementato sotto), un task accodato a un
>   semaforo non è nella propria mailbox, dunque **`sem_wait` non ha timeout**.
>   La seconda uscita di §13.8 non era un ramo alternativo: era una riapertura
>   di §9;
> - **la mailbox è tagliata**: un solo TCB in attesa, il contatore si incrementa
>   per i messaggi. Invariante `count >= -1`;
> - **il ceiling non ha una direzione conservativa** — per eccesso è inversione
>   di priorità dichiarata come politica, ed è per questo che §13.5 deve
>   esistere;
> - **codice**: `enqueue_dopo_nc` (§13.6) e `coda_api.vinc`, l'interfaccia che
>   pubblica anche le entry col contratto di chiamata;
> - **scoperta che ferma il mutex**: le priorità non esistono ancora. Vedi sotto.
>
> Niente delle decisioni è ancora nella proposta: l'elenco di cosa riportare, e
> dove, è la tabella in fondo a §3.26.
>
> Il 06/09/2026 aveva avuto due metà, e servono a cose diverse:
>
> - **§3.24 — l'albero**, dieci commit. `ctest` 23/23 e gli stessi identici
>   numeri a ogni commit, verificati confrontando i `.vx` byte per byte;
> - **§3.25 — le decisioni**, nessuna riga di codice. §7.4 chiusa (priority
>   ceiling), e semafori e mutex specificati in **§13** della proposta.
>
> L'albero adesso è a tre strati:
>
> ```
> hal/          la macchina, e il solo strato che la conosce
> generic/      tutto cio' che NON usa lo scheduler: coda, pool, timeout,
>               messaggio (formato) + i suoi test
> rtos/         cio' che lo USA: scheduler, servizi/mailbox,
>               gestore_timeout (il task di sistema), demo, test
> tools/        strumenti di analisi: la traccia temporale (§3.34)
> tests/        i tre test che verificano il SIMULATORE, non l'RTOS
> src/ include/ il simulatore in C
> ```
>
> Ogni libreria ha `impl/src` e `interface/<nome>`, e il `-I` per libreria è ora
> un **vincolo imposto dalla macchina**: includere un header senza averlo
> dichiarato non assembla.
>
> #### I due fronti sono indipendenti, e non hanno la stessa forma
>
> **A) Il kernel — e il prossimo passo NON è §13.**
>
> La decisione che bloccava è presa (§13.8, sopra). Il mutex e il semaforo però
> non sono scrivibili, e il motivo è a monte di §13:
>
> > **Le priorità non esistono ancora.** `TCB` è `{fwd, bwd, sp, state}` senza
> > nessun campo di priorità, `ready:` è **una sola** coda, lo scheduler è
> > round-robin, e `PCB` non compare in nessun sorgente. Il modello di §3 e §4
> > della proposta è specificato e **non costruito**.
>
> `mutex_lock` *è* «scrivi la priorità del task corrente al ceiling» e
> `prio_prec` *è* «la priorità di prima»: non c'è niente da scrivere e niente da
> salvare. Ferma anche il semaforo, perché l'inserimento ordinato di §13.6 deve
> confrontare le priorità dei TCB accodati.
>
> Quindi: **§3/§4 prima** — i PCB, una coda per livello, il campo nel TCB, e la
> politica dello scheduler da round-robin a «il livello non vuoto più alto».
> Mutex e semaforo dopo, e a quel punto sono corti.
>
> Che `task_ready`/`task_block` esistano solo come stub in `test_mailbox.vasm`
> **non** è un blocco per il mutex: quel debito la mailbox l'ha già preso.
>
> Dietro a questa, e solo dopo: **§8.7**, la commutazione volontaria, da cui
> dipendono i due debiti noti (il dispatcher che deve prendere il TCB in input,
> e `messageHandling.vasm` che scrive `TCB.state`). E il debito scoperto in
> §3.24: `task_ready` e `task_block` **non esistono** in `scheduler.vasm` —
> sono `.extern` nella mailbox e le uniche definizioni sono gli stub dentro
> `test_mailbox.vasm`, quindi `lib_messaggi` non si chiude da sola.
>
> **B) Il build e l'albero — quattro rifiniture, tutte incrementali.**
>
> Nessuna blocca niente, e si possono fare in qualsiasi ordine e in qualsiasi
> momento, anche a spizzichi:
>
> 1. **la doppia verità nelle intestazioni dei test** — le pipeline scritte a
>    mano nominano percorsi che non esistono più. È il pezzo rimasto di §3.24 ed
>    è **l'unica che peggiora** col tempo; `tests/test_include.vasm` è già stato
>    riscritto e vale da modello;
> 2. i **difetti minori** del build (§3.23 in fondo): `CMAKE_SOURCE_DIR`,
>    `-Wall` non guardato dal compilatore, `file(GLOB)` senza
>    `CONFIGURE_DEPENDS`, la collisione fra il programma `multi` e il test
>    `multi`;
> 3. `linked/multi` e `standalone/` sotto un `examples/` — **proposto e non
>    risposto**, decisione dell'utente;
> 4. i numeri di riga negli errori dell'assembler **slittano** della lunghezza
>    degli include già processati (`line 84` per una riga che sta alla 51).
>    Preesistente, ma rende faticoso leggere proprio gli errori che il nuovo
>    vincolo sui `-I` produrrà.
>
> **C) La proposta è indietro rispetto alle decisioni.** Tutto §3.26 è
> verbalizzato solo qui: la tabella in fondo a quella sezione dice cosa va
> riportato in `proposta-kernel-realtime.md` e dove. È lavoro di sola scrittura,
> indipendente da (A) e (B), e conviene farlo prima che la memoria di come ci si
> è arrivati sbiadisca.
>
> **Pushato fino a `798ae67`** (06/09). I commit del 07/09 — §3.26, il codice e
> questo aggiornamento — sono **solo in locale**. Il push l'ha sempre chiesto
> l'utente, come deve essere, e resta **una richiesta da rifare ogni volta**.

Il **30/08/2026** ci sono state **tre sessioni**, non una:
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
sono TUTTI E QUATTRO FATTI (§3.19–§3.22): `mfepsw`/`mtepsw` nell'ISA, la parola
di stato nel frame con `hal.vinc`, il percorso di trap nuovo — **l'HAL non nomina
più il kernel e si linka da solo** — e le sei librerie su un grafo che è un DAG.
Invariante (2) a **97/64**, sempre 8 tick.

Restano **due debiti**, annotati nei sorgenti e bloccati su §8.7/§7.4: il
`dispatcher` deve prendere il TCB in input invece di rileggere `current`
(§12.3), e `messageHandling.vasm` scrive `TCB.state` — l'ultima violazione del
confine «solo il kernel gestisce i task». Da lì in avanti si torna su **§7.4**.

Le altre due strade, entrambe in §5:

1. **§7.4**, la decisione ferma: ereditarietà di priorità o priority ceiling per
   i mutex. È quella che sblocca tutto il resto del kernel.
2. ~~La ristrutturazione del build in target CMake~~ — **FATTA il 05/09/2026**
   salvo il punto 3, che non blocca niente. Vedi il riquadro qui sotto.
3. **La revisione del confine HAL/ISR/kernel — §12 della proposta**: è da qui
   che riparte il codice, e non dipende da §7.4. Il passo 1 di §12.6 è **fatto**
   (§3.19), il 2 (§3.20), il 3 (§3.21) e il 4 (§3.22): **§12.6 è completa**.
   Restano i due debiti di §12.3, bloccati su §8.7 e §7.4.

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
| HAL | completo (§3.21) | [`hal/`](../hal/) |
| Code, pool, timeout, formato messaggi | completo, indipendente dallo scheduler (§3.24) | [`generic/`](../generic/) |
| Kernel + scheduler a priorità + mailbox + gestore timeout + semaforo e mutex | completo: PCB, slot, rotazione fra pari (tranne per chi è in sezione critica, §3.36), blocco volontario, task di sistema (§3.28–§3.33), §13 scritta (§3.35). Resta §13.7 | [`rtos/`](../rtos/) |
| Device in MMIO (tastiera, polling) | primo pezzo: registri sopra la RAM, alimentati da una traccia a cicli (§3.37) | [`hal/kbd.vinc`](../hal/interface/hal/kbd.vinc), [`src/vcpu.c`](../src/vcpu.c) |
| Sincronizzatore fra più VM e modelli di hardware | **da fare** — solo progettato (11/09/2026) | [`docs/proposta-sincronizzazione.md`](proposta-sincronizzazione.md) |
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
- [`linked/`](../linked/) — progetti che richiedono `asm` + `ld` su più moduli.
  Dal 06/09/2026 (§3.24) contiene il **solo** `linked/multi/`, demo minimale di
  link fra due moduli con eliminazione di codice morto (`main.vasm` +
  `saxpy.vasm` + `unused.vasm`). `linked/scheduler/` non esiste più: è diventato
  `hal/`, `generic/` e `rtos/` al primo livello.
- [`hal/`](../hal/), [`generic/`](../generic/), [`rtos/`](../rtos/) — l'RTOS,
  una cartella per libreria con `impl/src` e `interface/<nome>` (§3.24).

Nessun path era hardcoded nel codice C (`src/`), quindi lo spostamento non ha
toccato la toolchain — solo `Makefile` e i tre documenti in `docs/`, aggiornati
di conseguenza. Le tre invarianti di regressione (§4) sono state riverificate
dopo lo spostamento e danno gli stessi numeri di prima.

---

## 2. Git: dove siamo

Branch `master`, pubblicato su `git@github.com:rmigliori/vcpu_sim.git` (remote
`origin`, HTTPS + credential helper `git-credential-libsecret` configurato,
push senza prompt).

**Pushato fino a `2c97bf6`, cioè tutto il 07/09.** Tre push in giornata, tutti e
tre chiesti dall'utente: `90c29fd` (§3.32, la catena), poi `2c97bf6` a fine
sessione con §3.33, §3.34 e la riscrittura di §6. Prima era `c890c22`, e prima
ancora `798ae67`; il 06/09/2026 `origin/master` era passato da `e2955ff` a
`1806f5a` — **venti commit**, cioè §3.18-§3.23 del 05/09 più i dodici del 06/09
che sono §3.24, §3.25 e la loro documentazione — poi altri due di handoff fino a
`798ae67`. Ogni push **l'ha chiesto l'utente** («forse è ora di fare push?»),
come deve essere.

**Fuori da `origin/master` non c'è più niente** salvo il commit che aggiorna
questo paragrafo. I paragrafi che qui hanno detto, in momenti diversi della
giornata, «i commit del 07/09 sono solo in locale» valevano ognuno per il
proprio pezzo, e sono stati superati dal push successivo.

Perché quel momento e non un altro, visto che se ne è discusso: i commit della
ristrutturazione sono **verdi uno per uno**, non solo alla fine — ognuno chiude
con `ctest` 23/23 e i `.vx` identici byte per byte al precedente. La storia
pubblicata è quindi **bisecabile**: se un giorno un numero si muove,
`git bisect` atterra su un commit che compila e gira, non su un albero a metà
migrazione. Accumulare altro lavoro sopra avrebbe annacquato quella proprietà.

Gli undici di §3.24 sono in ordine di dipendenza e va tenuto: i due di sorgenti
(`a834cfe`, `913d3f7`) precedono il cambio di build (`4437546`), che precede i
sette spostamenti, che vanno dal basso del DAG in su. Ognuno chiude con `ctest`
23/23 e i `.vx` identici byte per byte al precedente.

```
15d302e Aggiorna manuale e proposta ai percorsi nuovi (§3.24)
        (piu' i due commit di handoff che registrano il push, usciti subito
        dopo: un commit non puo' nominare il proprio hash, quindi qui la
        catena si ferma di proposito invece di rincorrersi)
7e0c127 Handoff §3.24: l'albero ristrutturato, e le tre risposte
076af17 i test unitari sono applicazioni: vanno in cima al DAG del loro gruppo
3232793 la mailbox e la demo: linked/scheduler non esiste piu'
229ae36 lo scheduler in rtos/: kernel da contenitore a una delle sei
e1ebb2f timeout e messaggio: generic/ e' completa
89bc838 pool in generic/: il modulo piu' lontano dalla macchina
a440ebc coda in generic/: la cartella che deve poter vivere senza scheduler
71a9800 hal esce da linked/scheduler: prima cartella per libreria, e il -I morde
4437546 INTERFACES e LINK: la coppia resta, e la specie diventa un controllo
913d3f7 test_coda si dichiara il nodo: generic non chiede piu' l'header del kernel
a834cfe La parola di provenienza e' del pool: proprieta' rovesciata
9785d6d Handoff §3.23: come deve essere fatto l'albero, e le tre domande aperte
127de06 Sei librerie su un DAG: types.vinc spezzato e dipendenze transitive
f65a1b1 Il vettore consegna all'ISR: l'HAL non nomina piu' il kernel
b071ed7 La parola di stato nel frame di contesto, e hal.vinc
8f62946 ISA: mfepsw/mtepsw, il ritorno decide anche il regime
a00df00 Proposta §12: il confine HAL/ISR/kernel, i tre ritorni, mfepsw/mtepsw
a4091d0 Aggiorna handoff: pushato fino a e2955ff
e2955ff Aggiorna i documenti: .include a nome nudo e target CMake (§3.17)
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
> **I push del 05/09 e del 06/09 li ha fatti la sessione, e sono coerenti con la
> regola, non eccezioni**: tutte e due le volte l'utente l'ha chiesto
> esplicitamente in quel momento («forse prima una push?», «forse è ora di fare
> push?»). È esattamente il caso previsto. Che sia successo due volte **non lo
> rende routine**: nessuna delle due vale come autorizzazione permanente, e al
> push successivo si richiede di nuovo.
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

- [`hal/impl/src/machine.vasm`](../hal/impl/src/machine.vasm) — **unico** codice che tocca l'hardware:
  `_trap_entry` (frame di contesto da 60 byte), `ctx_init` (frame finto, come
  `pxPortInitialiseStack` di FreeRTOS), `timer_init`/`irq_arm`/`irq_enable`, e
  `irq_save`/`irq_restore` a coppia — **componibili**, quindi corretti anche annidati.
- [`generic/coda/impl/src/coda.vasm`](../generic/coda/impl/src/coda.vasm) — 5 primitive `list_head` con unlink
  O(1), più le varianti `_s` protette da sezione critica.
- [`rtos/scheduler/impl/src/scheduler.vasm`](../rtos/scheduler/impl/src/scheduler.vasm) — kernel **puro**: politica
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
([`rtos/scheduler/impl/src/scheduler.vasm`](../rtos/scheduler/impl/src/scheduler.vasm)):
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

### 3.20 La parola di stato nel frame, e `hal.vinc` (05/09/2026)

Passo 2 di §12.6. Commit a sé su richiesta dell'utente, perché è il primo che
**sposta un'invariante** e conviene poterlo misurare da solo.

**`hal.vinc`, la prima interfaccia dell'HAL.** Era l'unico modulo senza `.vinc`,
e in una discussione precedente avevo sostenuto che fosse un'eccezione legittima:
sbagliato, come l'utente ha fatto notare — l'HAL è la libreria che rende tutto il
resto indipendente dall'hardware, quindi è quella che *più* di tutte deve
pubblicare un'interfaccia. Pubblica `PSW_IE` e `CTX_FRAME_SIZE`.

**Il frame passa da 60 a 64 byte**: `ctx_save` salva anche la parola di stato
(`mfepsw`), `ctx_restore` la ripristina (`mtepsw`). Il commento che diceva
«`epsw` NON è nel frame: non è scrivibile e IE=1 è uniforme» è stato riscritto —
quell'uniformità valeva finché si torna sempre a un task.

**Il punto delicato è `ctx_init`, e non era ovvio.** Da quando `ctx_restore`
ripristina la `psw` dal frame, uno slot lasciato a zero farebbe partire il task
con `IE=0` — e un task che gira a interrupt disabilitati **non viene mai
preemptato**. Il frame finto deve quindi scrivere `PSW_IE` esplicitamente. Prima
non serviva perché il regime arrivava dalla `epsw` lasciata dalla trap
precedente. Verificato togliendo quella sola `sw`: la demo **non termina più**,
nessun tick, il primo task gira all'infinito. È il tipo di difetto che un test
sulle sequenze non vedrebbe — semplicemente non finisce.

**Trovato per strada: `-COSTANTE` non si poteva scrivere.** `addi r1, r1,
-CTX_FRAME_SIZE` dava `invalid integer`, perché `parse_int` cercava la costante
col segno attaccato. Due righe in `assembler.c`: negare una costante di
compile-time è aritmetica, non rilocazione, e il valore è noto subito. I simboli
rilocabili restano non-negabili, che è corretto — il negativo di un indirizzo non
è un indirizzo. Senza questo avrei dovuto riscrivere `-64` a mano, cioè rimettere
il numero magico che `hal.vinc` esiste per togliere.

**Invariante (2): 98/65 → 94/60**, con gli stessi **8 tick**. Sono 6 istruzioni
in più per switch (3 in `ctx_save`, 3 in `ctx_restore`), quindi meno lavoro utile
per tick: è lo stesso effetto già visto in §3.4, §3.5, §3.10 e §3.14, non una
regressione. Le altre invarianti **non si muovono**, e non è un caso: gli altri
quattro test linkano l'HAL ma non armano nessun timer, quindi non fanno un solo
context switch.

### 3.21 Il percorso di trap nuovo: l'HAL non nomina piu' il kernel (05/09/2026)

Passo 3 di §12.6, il cuore della revisione. Il ragionamento sta in §12.2 della
proposta; qui l'esito e le misure.

**Cosa si è spostato.** `g_handler` e `irq_install` sono passati dal kernel
all'**HAL** — installare un vettore è hardware — e `_trap_entry` ora consegna il
controllo all'ISR applicativa con `jr`, passandole il contesto opaco in `r1`.
`sched_dispatch` non esiste più: al suo posto c'è **`sched_isr_exit(r1 =
contesto)`**, dove l'ISR **salta** quando ha finito. La differenza non è il nome:
prima l'HAL chiamava il kernel e il kernel invocava l'ISR, mettendosi in mezzo;
ora il vettore consegna all'ISR e il kernel rientra solo quando l'ISR glielo
chiede.

**La prova del confine è un comando, non un'opinione:**

```
$ ./build/vcpu_sim ld build/machine.vo -o hal.vx
```

Prima dava `undefined reference to 'sched_dispatch'`. Ora **si chiude**, e
`machine.vasm` ha **zero `.extern`**. È la definizione di «la libreria che rende
tutto il resto indipendente dall'hardware», e fino a oggi non era vera.

**Un guadagno che non avevo previsto: tre eseguibili si sono alleggeriti.** Le
intestazioni di `test_coda`, `test_pool` e `test_timeout` spiegavano che
`machine.vo` «si tira dietro `_trap_entry` → `sched_dispatch` → `scheduler.vasm`»;
ora non più, e `scheduler.vasm` **non entra più nel link** di quei tre. Resta in
`test_mailbox` per una ragione sola e legittima: è lui a definire `current`, che
`receive` legge — cioè uno dei difetti aperti di §8.7. Le quattro intestazioni
sono state riscritte.

**Invariante (2): 94/60 → 97/64**, con gli stessi **8 tick**. Stavolta i
contatori **salgono**, cioè il percorso di trap è diventato più corto: sparisce
un livello di `call`/`ret` (l'HAL chiamava il kernel, il kernel chiamava l'ISR,
l'ISR tornava al kernel; ora sono due salti). Un confine più pulito che costa
meno è un buon segno, non un sospetto: si è tolto un intermediario, non del
lavoro.

**L'ISR della demo è diventata una ISR vera.** `timer_isr` riceve il contesto in
`r1`, lo mette al sicuro perché `r1` le serve come scratch, e **non ritorna**:
esce saltando a `sched_isr_exit`. È una scelta di quella ISR, non una regola —
§12.4 ne elenca tre — ma è la prima volta che nel codice l'uscita è una decisione
dell'applicazione invece di un `ret` verso il kernel.

**Non fatto, ed è il prossimo debito:** `dispatcher` continua a rileggere
`current` invece di ricevere il TCB in input (§12.3). Annotato nel sorgente.

### 3.22 Sei librerie e un DAG: la decomposizione (05/09/2026)

Passo 4 di §12.6, l'ultimo. Due metà, verificate separatamente.

**`types.vinc` spezzato in tre.** Teneva insieme code, task e messaggi: un unico
file di interfaccia per tre fornitori diversi, quindi ogni modulo dipendeva da
tutto — il pool si portava dietro `TCB` e `MESSAGGIO` senza nominarli mai. Ora:

| File | Contiene | Dipende da |
|---|---|---|
| `coda.vinc` | `TESTA`, `CODA_*` | — (il più basso e il più incluso) |
| `tcb.vinc` | `TCB`, `READY`/`RUNNING`/`SUSPENDED` | `coda.vinc` |
| `messaggio.vinc` | `MESSAGGIO`, `PAYLOAD`, `MSG_*` | `coda.vinc` |
| `pool.vinc` | `BLOCCO`, `POOL_*` | `coda.vinc` (era `types.vinc`) |
| `hal.vinc`, `timeout.vinc` | — | foglie |

Le tre dipendenze hanno tutte la stessa ragione: **`TCB`, `MESSAGGIO` e `BLOCCO`
sono nodi di lista.** Ogni sorgente include ciò che *nomina*, non un
aggregatore. Nessun numero si è mosso, ed era il punto: le costanti sono le
stesse, cambia solo chi le riceve.

**Le sei librerie.** `vasm_library` distingue due dipendenze che non vanno
confuse: `LIBS` sono le **interfacce** da cui un modulo prende costanti (→ `-I`),
`LINK` sono le **librerie** di cui usa i simboli (→ `.va` sul comando di `ld`,
chiuse transitivamente da `_vasm_link_closure`).

Il guadagno è dove ci si aspetta: un programma dichiara la libreria che usa e
non la lista dei moduli. `test_mailbox` dice `LINK lib_messaggi` e si ritrova
`lib_coda`, `lib_kernel` e `lib_hal` senza saperlo; prima quella catena stava
scritta a mano nell'intestazione del test, da tenere aggiornata a occhio.

**Il grafo è un DAG**, e non è una parola: `ld lib_hal.va` da solo si chiude
(exit 0). Tutte le frecce vanno verso l'HAL, nessun percorso torna indietro.

```
messaggi ──▶ kernel ──▶ coda ──▶ hal
    │           │                 ▲
    └───────────┴─────────────────┘
               pool ──▶ coda
            timeout ──────────────▶ hal
```

**Invarianti: nessuna si muove.** `ctest` 23/23, e i numeri sono quelli lasciati
dal passo 3 — 17/40/94, **97/64 con 8 tick**, 18/40/95. Non è ovvio: cambiando
gli archivi cambia il layout delle immagini, e i valori sono rimasti perché il
codice eseguito è lo stesso.

**Resta aperto**, e sono i due debiti annotati nei sorgenti: `dispatcher` deve
prendere il TCB in input invece di rileggere `current` (§12.3), e
`messageHandling.vasm` scrive `TCB.state` (riga 177) — l'ultima violazione del
confine «solo il kernel gestisce i task». Quando sarà sanata, quel modulo
smetterà di includere `tcb.vinc`, e il grep a zero sarà la prova che il confine
è vero. Entrambe dipendono da §8.7 e §7.4.

---

### 3.23 Cosa vuol dire «struttura professionale»: una cartella per libreria (05/09/2026, quinta sessione)

**Sessione di sola discussione: nessuna riga scritta, nessun file spostato.** Si
chiude con uno schema **deciso dall'utente** e **tre domande ancora aperte** —
che è il motivo per cui non è stato toccato niente.

**Il punto di partenza.** L'utente guarda il `CMakeLists.txt` uscito da §3.22 e
dice che «non riflette la struttura di un progetto professionale». È vero, e il
difetto è uno: **il file di primo livello conosce il percorso di ogni sorgente
del progetto**. La spia sono `set(VINC_DIR ...)`, `set(SCHED ...)`,
`set(TESTS ...)` — in CMake un `set()` che contiene un percorso verso *un'altra*
cartella è quasi sempre un `add_subdirectory()` mancante. La descrizione di build
di un componente deve vivere **con** il componente.

**Lo schema dell'utente**, più preciso del generico «spezza in sottocartelle»:
ogni libreria è una cartella con due rami, e il ramo dell'interfaccia contiene una
sottocartella **che ripete il nome della libreria**.

```
coda/
  CMakeLists.txt              add_subdirectory(impl interface)
  impl/
    CMakeLists.txt            add_subdirectory(src)
    src/
      CMakeLists.txt          sorgenti + librerie da linkare (anche la propria interfaccia)
      coda.vasm
  interface/
    CMakeLists.txt            add_subdirectory(coda)
    coda/                     <- ripete il nome della libreria
      CMakeLists.txt          la libreria d'interfaccia + cosa linka a sua volta
      coda.vinc
```

> #### Perché quel livello `interface/<nome>/` non è decorazione
>
> **Oggi la propagazione dei `-I` è teatro.** Tutti e sei i `vasm_interface`
> puntano alla *stessa* cartella `linked/scheduler/include/`, quindi qualunque
> modulo — qualunque cosa dichiari in `LIBS` — riceve un `-I` su una cartella che
> contiene tutti e sei i `.vinc`. `test_pool.vasm` può includere `tcb.vinc` senza
> che nessuno lo abbia autorizzato, e assembla. La dichiarazione di dipendenza è
> un commento.
>
> Con `interface/<nome>/` ogni libreria propaga la **propria** cartella e il file
> si include come `coda/coda.vinc`. Chi include `tcb/tcb.vinc` senza aver linkato
> l'interfaccia del kernel non riceve quel `-I` e **non assembla**. Il grafo
> smette di essere documentazione e diventa un vincolo imposto dalla macchina —
> per le *costanti*, esattamente come §3.21 l'ha ottenuto per i *simboli*
> (`ld machine.vo` da solo si chiude).

**Verificato, e vale come vincolo per chi implementa: non serve toccare il C.**
[`src/assembler.c:1180`](../src/assembler.c#L1180) compone `"%s/%s"` fra la
cartella candidata e il nome, quindi un nome **con lo slash** si risolve già oggi;
e `inc_canonical` (`realpath`) lo canonicalizza *dopo* aver aperto il file, quindi
l'idempotenza di §3.16 regge senza modifiche.

Conferma incidentale che lo spezzettamento di §3.22 era giusto: **sei librerie,
sei `.vinc`, uno per libreria**, corrispondenza esatta. Se lo schema non fosse
aderente al progetto si vedrebbe una `interface/` con dentro header di padroni
diversi — ed era esattamente `types.vinc`, che ne teneva tre.

**Le tre domande aperte.** Sono **proposte di Claude, non decisioni
dell'utente**, e non vanno trattate come acquisite:

1. **`impl/` deve guadagnarsi il suo livello.** Così com'è contiene solo `src/`:
   due livelli per uno. Proposta: `impl/` tiene `src/` **e** `test/`, e
   `tests/test_coda.vasm` — che è il test unitario di `lib_coda`, non un test di
   sistema — si sposta in `coda/impl/test/`. Resterebbero in un `tests/` di primo
   livello solo i test che verificano il **simulatore**: `test_epsw`,
   `test_proc`, `test_include`, `standalone/`.
2. **Dove vanno le sei cartelle.** `linked/` nacque per «esempi con più moduli
   linkati» e ci convive `linked/multi/`; l'RTOS non è più un esempio. Proposta:
   `rtos/` a primo livello con le sei librerie sorelle, lasciando `linked/multi/`
   dov'è. Attenzione al **rovesciamento di significato**: oggi `kernel/` è la
   cartella che contiene cinque delle sei librerie, domani sarebbe **una** delle
   sei.
3. **Fondere `LIBS` e `LINK` in `vasm_library`.** Lo schema lo implica: se una
   libreria «linka anche la propria interfaccia», la distinzione fra interfaccia
   (→ `-I`) e libreria (→ `.va`) non è più del chiamante ma è una proprietà del
   target linkato, e la macchina la sa già (chi ha `VASM_HEADERS` è
   un'interfaccia, chi ha un `.va` è una libreria). Un solo `LINK`, come il vero
   `target_link_libraries`.

**Il prezzo, detto per intero:** cinque librerie su sei sono **un solo file
sorgente**, che finirebbe tre livelli sotto (`rtos/coda/impl/src/coda.vasm`) —
sette cartelle e quattro `CMakeLists.txt` per ~200 righe. Si paga volentieri
perché il vincolo sui `-I` frutta **adesso**, non «quando il progetto crescerà», e
perché i confini non sono inventati: sono quelli dimostrati in §3.21-§3.22.

**Altri difetti del `CMakeLists.txt` attuale**, emersi nella stessa analisi e da
sistemare con lo spostamento:

- **`CMAKE_SOURCE_DIR` ovunque**, anche in [`cmake/vasm.cmake:17`](../cmake/vasm.cmake#L17)
  (`VASM_BINARY_DIR`): significa *la radice dell'albero*, non *la radice di questo
  progetto*. Va `PROJECT_SOURCE_DIR` / `CMAKE_CURRENT_SOURCE_DIR` — e a quel punto
  i percorsi diventano nomi nudi.
- **Collisione già in atto:** `multi` è sia il programma sia il test. Funziona
  perché sono due spazi di nomi diversi di CMake, ma è fortuna.
- **`add_compile_options(-Wall -Wextra)` globale e non guardato** dal compilatore:
  va sul target, con `$<$<C_COMPILER_ID:GNU,Clang>:...>`.
- **`file(GLOB)` senza `CONFIGURE_DEPENDS`** per `standalone/`: si aggiunge un
  `.vasm` e il build non se ne accorge.
- **Doppia verità sui numeri attesi.** I valori stanno nel `CMakeLists.txt` **e**
  nell'intestazione di sei test (`Atteso, in ordine: ...`), e accanto ci sono le
  pipeline scritte a mano che CMake ha reso obsolete (altri sei file). Sono copie
  che divergeranno **in silenzio**: `ctest` resterà verde mentre il commento
  mente. Le intestazioni vanno riscritte con *cosa* verifica il test e *perché*;
  pipeline e numeri se ne vanno dove li esegue la macchina.

---

### 3.24 L'albero ristrutturato: tre strati, sette commit, zero numeri mossi (06/09/2026)

Le tre domande aperte di §3.23 hanno avuto risposta, e **due su tre non sono
andate come Claude aveva proposto.** Poi lo spostamento, una libreria per
commit.

#### Il criterio, che è dell'utente e vale più delle tre risposte

> **Tutto ciò che non usa lo scheduler non fa parte di `rtos`.**

Applicato ai **simboli veri** — non alle intenzioni, non ai nomi delle cartelle
— dà una partizione senza residui. Basta guardare cosa ogni modulo chiede a
qualcun altro:

| modulo | simboli che chiede | usa lo scheduler? |
|---|---|---|
| `machine.vasm` | nessuno | no → `hal/` |
| `coda.vasm` | `irq_save`, `irq_restore` | no → `generic/` |
| `timeout.vasm` | `irq_save`, `irq_restore` | no → `generic/` |
| `pool.vasm` | `coda_init`, `enqueue_coda`, `enqueue_coda_s`, `dequeue_testa_s` | no → `generic/` |
| `messaggio.vinc` | (solo `coda.vinc`) | no → `generic/` |
| `scheduler.vasm` | `enqueue_coda`, `dequeue_testa`, `ctx_restore` | **è** lo scheduler → `rtos/` |
| `messageHandling.vasm` | + `task_ready`, `task_block`, `current` | sì → `rtos/` |

Da cui la conseguenza che ha ridisegnato l'albero: **`hal/` e `generic/` non
stanno sotto `rtos/`.** Se ci stessero, l'albero affermerebbe un contenimento
che il grafo nega — una lista doppiamente concatenata e un allocatore a blocchi
fissi si sollevano di peso in un progetto che di scheduler non ne ha, purché
fornisca `irq_save` e `irq_restore`. È il senso della parola *generic*, e la
cartella deve poterlo **dimostrare** invece di dichiararlo.

Due cose che la tabella dice e che non erano scritte da nessuna parte:

- **il pool non nomina un solo simbolo dell'HAL.** La sezione critica gli
  arriva già confezionata dai wrapper `_s` delle code. Sta un gradino più
  lontano dalla macchina delle code stesse;
- **`timeout` entra in `generic/` per la firma esatta di `coda.vasm`.** Con una
  cosa da sapere: `tmo_now` è un contatore di tick e l'incremento appartiene al
  percorso del tick, che non esiste ancora. Quando esisterà sarà il timer ISR a
  muovere quella parola — resta un legame verso l'HAL, non verso il kernel,
  quindi la collocazione non cambia, ma sarà l'unico modulo di `generic/` con
  uno stato che scrive qualcun altro.

#### Le tre risposte

**1. `impl/` NON prende un `test/`, e i test unitari non vanno dentro le
librerie.** La proposta di §3.23 era l'opposto. L'ha bocciata l'utente con un
argomento solo: *un test è un'applicazione che usa la libreria*.

La verifica ha confermato e ha dato il criterio generale. Confrontando gli
`.extern` di ogni test con i `.global` del fornitore, **nessuno dei quattro
scavalca un'interfaccia**: tutti passano dalla porta principale. Quindi:

> Un test sta **dentro** la libreria solo se gli serve un accesso che la
> libreria non pubblica. Se passa dai `.global` è indistinguibile da qualunque
> altro cliente, e metterlo dentro gli regala un privilegio che non usa in
> cambio di un arco di dipendenza che la libreria non ha.

`test_mailbox` lo mostra meglio di tutti: attraversa code, messaggi e kernel —
chiamarlo «test unitario di `lib_messaggi`» era già una forzatura.

Stanno però **dentro il gruppo che verificano** (`generic/test/`, `rtos/test/`)
e non in un `tests/` comune, perché `generic/` deve potersi sollevare altrove:
portarsela via senza le sue verifiche sarebbe portarsi via meno di quello che
c'è. In `tests/` restano i tre che verificano il **simulatore** — `epsw` l'ISA,
`proc` una direttiva, `include` la risoluzione dei `-I` — che appartengono al C.

L'argomento con cui Claude aveva sostenuto lo spostamento dentro le librerie
**non reggeva**, e va detto perché non torni: sosteneva che servisse a chiudere
la falla dei `-I`. Non è così — il vincolo nasce dall'avere **un `-I` per
libreria**, non da dove sta il file.

**2. Tre strati di primo livello, non sei sorelle sotto `rtos/`.**

```
hal/          machine.vasm + hal.vinc
generic/      coda/  pool/  timeout/  messaggio/  + test/
rtos/         scheduler/  servizi/mailbox/  demo/  test/
tests/        test_epsw  test_proc  test_include   (verificano il simulatore)
```

Il rovesciamento previsto da §3.23 è avvenuto ed è una **correzione**, non una
perdita: `kernel/` era la cartella che conteneva cinque librerie su sei, adesso
lo scheduler è **una** delle sei. §3.22 aveva già stabilito che i messaggi sono
un *cliente* del kernel e non parte sua, e la vecchia cartella contraddiceva il
grafo.

**3. `LIBS` e `LINK` NON si fondono.** Anche qui la proposta di §3.23 è stata
bocciata dall'utente, con la domanda giusta: «non rende la comprensione delle
liste più complicata?».

Sì, e l'analogia con cui Claude l'aveva sostenuta la smonta. In CMake
`target_link_libraries` se la cava con una parola chiave sola **perché `PUBLIC`
o `PRIVATE` lo scrivi lì, ogni volta**; la fusione avrebbe tolto l'unico segno
visibile della distinzione *conservandone la sostanza*. E c'è un caso nel
progetto che si legge oggi e che sarebbe sparito: **`lib_kernel` linka le code
ma non ne dichiara l'interfaccia**, perché `scheduler.vasm` chiama
`enqueue_coda` senza aver bisogno di una costante di `coda.vinc`.

Il difetto vero erano i **nomi**. Quindi `LIBS` → `INTERFACES` ovunque, anche in
`vasm_interface` (dove il `LINK` fra due `.vinc` non era un link: un `.vinc` non
emette un byte). E la cosa che la fusione voleva sfruttare — che la macchina sa
già distinguere le specie — è diventata una **verifica** invece che
un'inferenza: ogni forma marchia ciò che produce con `VASM_KIND`
(`interface`/`object`/`archive`/`program`), e `_vasm_require_kind` rifiuta a
tempo di configure una libreria in `INTERFACES` o un'interfaccia in `LINK`.

#### Il vincolo sui `-I` esiste, e ha morso da solo

Era il motivo per cui valeva la pena pagare sette cartelle per ~200 righe.
Provato deliberatamente: aggiungendo `.include "hal/hal.vinc"` a `coda.vasm`,
che non dichiara `vinc_hal`, l'assemblaggio si ferma con `cannot open include`.

Ma la prova migliore non l'ha cercata nessuno: **`test_include` è fallito due
volte da solo**, al passo del pool e a quello dello scheduler, perché nominava
header di librerie che non aveva dichiarato. Prima quella dichiarazione era un
commento.

Un dettaglio dell'assembler che §3.23 non aveva registrato e che è portante:
[`assembler.c:1171`](../src/assembler.c#L1171) cerca in `inc->base_dir`, che è
fissato **una volta sola dal file di primo livello**
([`assembler.c:1127`](../src/assembler.c#L1127)) e **non** dalla cartella di chi
include. Quindi `tcb.vinc` che include `"coda/coda.vinc"` non può risolverlo per
via relativa: o arriva il `-I`, o non assembla. Il vincolo vale anche per gli
archi interfaccia→interfaccia, più forte di quanto §3.23 sperasse.

#### Due scoperte sulla forma delle librerie

**`mailbox` non ha `interface/`, `messaggio` non ha `impl/`**, e sono la stessa
cosa vista dai due lati: il *formato* di un messaggio è costanti senza codice,
il *servizio* che li muove è codice che non pubblica costanti — la sua
interfaccia sono due simboli, `send` e `receive`, e i simboli viaggiano nel
`.va`. Che le due metà fossero una libreria sola era il residuo da sciogliere:
stavano insieme perché parlano della stessa cosa, non perché abbiano le stesse
dipendenze. Semafori e mutex nasceranno con la stessa forma.

**`task_ready` e `task_block` non esistono in `scheduler.vasm`.** Sono `.extern`
in `messageHandling.vasm` e le uniche definizioni sono gli **stub dentro
`test_mailbox.vasm`**. Finché §7.4 non atterra, `lib_messaggi` non si chiude da
sola: l'unico programma che la linka è il test, che il kernel se lo finge.

#### I due commit di sorgenti che sono venuti prima

Nessuno dei due sposta un file, e per questo si verificano ad albero fermo.

**La parola di provenienza è del pool.** `BLOCCO.pool` (+8) e `PAYLOAD.pool`
(payload+0) sono la **stessa parola**, e `BLOCCO.dati` (+12) cade su
`PAYLOAD.messageType`: le due geometrie sono incastrate al byte. Nel codice la
dipendenza non c'era già — `pool.vasm` nomina solo `BLOCCO.*` — ma il
**commento** dichiarava la proprietà al contrario, e così scritto il pool si
definiva a partire da un formato che non conosce. Ora `pool.vinc` possiede
dodici byte di testa e consegna da +12 un'area opaca, e il vincolo è formulato a
carico di chi definisce un formato; `messaggio.vinc` prende il ruolo opposto.

**`test_coda` si dichiara il nodo.** Dichiarava i nodi `.res TCB` e per farlo
includeva `tcb.vinc`, cioè l'interfaccia dello scheduler, pur essendo il test
della libreria più bassa: `generic/` si trascinava dentro il kernel attraverso
il proprio test. Ora una `.struct NODO` propria — due link più otto byte opachi,
che dicono qualcosa in più del vecchio TCB (che `coda.vasm` non assume niente
sulla taglia del nodo) — e **16 byte come TCB apposta**, per non muovere
l'immagine dati.

#### Il difetto di partenza, chiuso

`CMakeLists.txt` da **167 righe a 105**, e non conosce più il percorso di un
solo sorgente dell'RTOS: tre `add_subdirectory`, gli esempi del simulatore, e le
invarianti che restano sue. `VINC_DIR`, `SCHED` e `TESTS` non esistono più. Era
esattamente la diagnosi di §3.23 — «il file di primo livello conosce il percorso
di ogni sorgente del progetto».

Nota per chi rilegge §3.23: lo schema vi era abbreviato in
`add_subdirectory(impl interface)`, che **in CMake vuol dire un'altra cosa**
(`source_dir` e `binary_dir`). Servono due chiamate, e **l'interfaccia va
prima**, perché `impl/` la nomina e un target dev'essere definito prima di
essere nominato.

#### Come è stato verificato

A **ogni** commit: `ctest` 23/23, **e** i sei `.vx` confrontati byte per byte
con quelli del commit precedente, ricostruito ogni volta in una cartella
separata. Non «gli stessi numeri»: gli stessi **byte**. Sui due commit di sola
dichiarazione la verifica è ancora più stretta — filtrando dal diff le righe di
commento non resta nulla.

Invarianti finali: **17/40/94**, **97/64 con 8 tick**, **18/40/95**, e le quattro
sequenze dei test unitari. Migrazione a somma zero, dimostrata e non dichiarata.

#### Cosa resta

- **`linked/multi` e `standalone/` sotto un `examples/`** — aperto, e ora
  `linked/` ha un solo abitante;
- i **difetti minori** elencati in fondo a §3.23: `CMAKE_SOURCE_DIR`, `-Wall`
  non guardato dal compilatore, `file(GLOB)` senza `CONFIGURE_DEPENDS`, la
  collisione fra il programma `multi` e il test `multi`;
- **la doppia verità nelle intestazioni dei test**, ora peggiorata: le pipeline
  scritte a mano nominano percorsi che non esistono più. `test_include` è già
  stato riscritto perché doveva cambiare comunque;
- un difetto dell'assembler visto e non toccato: i messaggi d'errore riportano
  un **numero di riga che slitta della lunghezza degli include già
  processati** (`line 84` per una riga che sta alla 51). È preesistente, ma
  rende più faticoso leggere proprio gli errori che il nuovo vincolo produrrà.

---

### 3.25 §7.4 chiusa: il ceiling, e la specifica di semafori e mutex (06/09/2026, seconda parte)

**Sessione di sole decisioni, come §3.23 — ma stavolta verbalizzate lo stesso
giorno.** Nessuna riga di `.vasm` scritta: il codice di semafori e mutex non
esiste ancora e non poteva nascere prima, perché §7.4 era il tappo.

#### La decisione

> **§7.4: priority ceiling, non ereditarietà.** Decisa dall'utente.

Era la questione aperta più a lungo del progetto. La proposta la raccomandava già
per la calcolabilità a compile-time, ma l'argomento che ha chiuso è un altro, ed
è specifico di *questo* modello — ora scritto in §7.4 della proposta, dove prima
stava solo qui nell'handoff come nota del 30/08:

**Col ceiling la priorità cambia solo al task che sta girando.** Chi acquisisce
sta eseguendo `mutex_lock`, chi rilascia sta eseguendo `mutex_unlock`: in
entrambi i casi ha la CPU, quindi non è dentro nessuna coda e la promozione è la
scrittura di un campo. L'ereditarietà promuove il **possessore**, che è promosso
proprio perché *non* sta girando — sta nella coda del suo livello o nello slot
`preemptato` — e in un modello a una coda per livello quella promozione è
**sfilare da una testa e accodare a un'altra**, dentro la sezione critica di chi
si blocca. La stessa struttura che rende la selezione O(1) rende la promozione
un'operazione di lista.

#### Il mutex È la sezione critica

Osservazione dell'utente, e ha cambiato la forma della specifica: il mutex è
quello che si implementava disabilitando gli interrupt all'inizio e
riabilitandoli alla fine. Non è un'analogia — **è la stessa disciplina con un
limite diverso**. `cli` ti porta al massimo assoluto; il ceiling ti porta alla
priorità più alta *che possa contendere questa risorsa*, e non un filo più su.

La conferma sta nel codice, e nella forma esatta: `irq_save` non «disabilita»,
**restituisce la psw precedente** ([`hal/impl/src/machine.vasm:218`](../hal/impl/src/machine.vasm#L218)),
ed è per questo che l'annidamento funziona. Il campo `prio_prec` del mutex *è*
quel valore di ritorno, promosso di un livello — ed è la ragione per cui §7.4
poteva già dire «salvare nel mutex la priorità precedente basta».

Da lì discendono due cose che prima erano domande aperte:

- **non ci si blocca tenendo un mutex**, perché sarebbe sbagliato esattamente
  come bloccarsi con gli interrupt disabilitati: cederesti la CPU tenendo chiusa
  una porta che nessun altro può aprire;
- **il ceiling non esclude le ISR**, quindi un dato condiviso con un'ISR non è
  protetto da un mutex. Regola: *se la tocca un'ISR, `_s`; se la toccano solo
  task, mutex* — e la seconda metà è un'ottimizzazione di latenza, non di
  correttezza.

#### Il semaforo non è una mailbox, e per un motivo preciso

Sembravano lo stesso oggetto: una `TESTA`, un contatore con segno, task accodati
sul negativo. La differenza è la **natura del contatore**, e l'utente l'ha messa
a fuoco correggendo una propria semplificazione:

- **mailbox — contatore descrittivo**: descrive la lista in ogni istante. Se dice
  3, in lista ci sono tre nodi-messaggio, fisici;
- **semaforo — contatore contabile**: sul positivo conta **risorse**, che non
  sono nodi e non stanno in nessuna lista. `sem_wait` con `count > 0` decrementa
  e ritorna **senza toccare la lista** — un cammino veloce che la `receive` non
  ha e non può avere.

Tre conseguenze non cosmetiche, tutte in §13.1: il semaforo vuole un valore
iniziale (è il primo oggetto del progetto che non nasce «tutti zeri»); il suo
lato positivo **non è verificabile da niente**, mentre il contatore della mailbox
è ridondante con la lista e quindi controllabile; e il negativo vale −N, non −1,
il che gli fa perdere le due semplificazioni di cui la mailbox gode (niente
ordinamento, niente ciclo di ricontrollo al risveglio).

#### Il degrado del mutex, ed è la parte lunga di §13

**§13.5 esiste perché l'utente ha chiesto la spiegazione per esteso**, e la
merita: è il punto in cui il ceiling può fallire in silenzio.

Sotto ICPP vale che un task che gira e prova a prendere un mutex **lo trova
libero**, quindi la coda d'attesa non si usa mai. La dimostrazione è di due
righe, ma poggia su tre premesse — monoprocessore, ceiling dichiarato
correttamente, e nessuno che si blocchi tenendo un mutex. La seconda e la terza
possono cadere, e producono **lo stesso sintomo**: qualcuno finisce nella coda.

Il cuore è cosa fare di quel qualcuno. La reazione istintiva — rimetterlo nella
sua coda di ready perché riprovi — è **l'unica risposta che rompe**: se è più
prioritario di chi tiene il mutex, lo scheduler lo rimette subito in esecuzione,
riprova, fallisce, si riaccoda, e il possessore non gira mai. È **livelock**, con
la macchina al 100% che sembra lavorare. Deve invece **uscire dagli eseguibili** e
bloccarsi sulla coda del mutex: allora il possessore riparte, finisce, rilascia e
lo sveglia. L'inversione c'è ma è limitata dalla sezione critica.

Il che corregge, in meglio, come era stata presentata la coda del mutex a metà
discussione:

> La coda del mutex non c'è per essere usata. C'è perché **un ceiling dichiarato
> male degradi invece di appendere**.

E siccome sotto ceiling corretto è vuota sempre, un TCB accodato a un mutex è
**un'anomalia osservabile** a costo zero — stesso mestiere di `CODA_LINKED` e
dell'invariante `fwd == bwd == 0`.

#### L'inserimento ordinato, e il confine che stava per essere violato

Con N attese servono risveglio per priorità e inserimento ordinato — ai semafori
e ai mutex, **non** alle code di ready (già una per livello, dentro cui il FIFO è
il round-robin) e non alla mailbox (un ricevente solo).

La forma ovvia era una `enqueue_prio` in `coda.vasm`. **Sarebbe stata una
violazione del confine appena costruito**: leggere la priorità dal nodo significa
sapere che il nodo è un TCB, quindi `generic/coda` includerebbe `tcb/tcb.vinc` e
`generic/` dipenderebbe da `rtos/` — il difetto tolto la mattina stessa (§3.24)
che rientra dalla finestra.

La regola l'ha data l'utente, ed è quella giusta: *il gestore delle code non deve
sapere perché gli si chiede di accodare in testa, in coda o fra due elementi.*
Quindi `coda.vasm` guadagna `enqueue_dopo(prec, nodo)` — puro maneggio di link — e
la camminata sta in `rtos/`, dove i TCB si conoscono. E non è nemmeno una terza
disciplina: `TESTA` è una **sentinella** in lista circolare, quindi
`enqueue_testa` è «dopo la sentinella» e `enqueue_coda` è «dopo `testa.bwd`».
La primitiva nuova le contiene entrambe.

#### Cosa è stato scritto

| Dove | Cosa |
|---|---|
| §7.4 della proposta | da **APERTA** a **DECISA: il ceiling**, con l'argomento sullo spostamento fra code che finora stava solo in questo handoff |
| §13 della proposta (nuova) | semafori e mutex: strutture, il contatore contabile contro descrittivo, il mutex come sezione critica, **§13.5 il degrado per esteso**, l'inserimento ordinato e il confine, `mutex_unlock` come punto di preemption |
| §1 della proposta | «Semafori: da specificare» non c'è più |

**§13 e non una sezione fra §10 e §11**, di proposito: i sorgenti nominano
§12.1-§12.6, §10.3, §9.2, §8.4 dentro i commenti. Rinumerare romperebbe decine
di riferimenti nel codice, che è la parte del progetto che invecchia peggio.
**Le sezioni si aggiungono in fondo, non si inseriscono.**

#### La tensione che questa sessione ha aperto, e che NON è stata risolta

§1 e §7.5 dicono che **la mailbox è l'unico punto di blocco di un task**, e da lì
discende la garanzia che *ogni attesa possa avere un tempo massimo*: il gestore
dei timeout consegna un messaggio nella casella su cui il task aspetta.

**Il semaforo blocca**, quindi quella premessa cade: un task accodato a un
semaforo non è nella propria mailbox, e il messaggio di scadenza arriva dove non
c'è nessuno in ascolto. §7.5 lo aveva mezzo previsto («`sem_wait` non ha timeout,
per costruzione»), ma le due frasi non stanno insieme.

Le due uscite sono in §13.8, e la seconda ha un prezzo che va guardato prima di
sceglierla: se il timeout smette di consegnare un messaggio e diventa «sgancia il
task da dove sta», **il pool resta senza clienti** — è oggi l'unico, ed è nato
per quello.

È la prima cosa da riprendere sul fronte del kernel.

---

### 3.26 §13.8 sciolta, la mailbox tagliata, e il primo codice di §13 (07/09/2026)

**Sessione mista: cinque decisioni e due pezzi di codice.** Nessuna delle
decisioni è ancora nella proposta — vivono solo qui, e riportarle in
`proposta-kernel-realtime.md` è la prima cosa da fare (elenco in fondo).

#### La tensione sui timeout non si sceglieva: si deduceva

§13.8 la presentava come due uscite di pari dignità. **Non lo sono**, e l'ha
detto l'utente in una riga: *«il timeout deve essere notificato con un
messaggio, non era questa la decisione?»*. Sì — §9 apre con «un timeout resta
una consegna in mailbox che avviene più tardi», §9.5 è marcata DECISA, e sotto
c'è §10 implementato il 04/09, il pool, che esiste **solo** per dare un buffer a
un cliente che dorme.

Quindi la seconda uscita di §13.8 («il timeout sgancia il task da dove sta») non
è un ramo alternativo: è **una riapertura di §9 e §10**. E una cosa decisa si
riapre solo se non esiste una via che non lo richieda. Esiste:

> Il timeout è una consegna in mailbox (§9). Un task accodato a un semaforo non
> è nella propria mailbox. Dunque **`sem_wait` non ha timeout** — non per
> convenzione, come lo scriveva §7.5, ma perché è l'unica forma compatibile con
> una decisione già presa.

§13.8 è formulata male anche su un secondo punto: presenta «il pool resta senza
clienti» come il *costo* di quella uscita, mentre è il **sintomo** che quella
uscita smonta il modello sotto. Un'uscita che lascia senza clienti un
sottosistema implementato tre giorni prima non sta pagando un prezzo.

**La giustificazione, che trasforma la prima uscita da concessione a principio:**

> La mailbox è dove aspetti **il mondo**, e il mondo può non rispondere:
> quell'attesa vuole un orologio. Il semaforo è dove aspetti che **un altro task
> qui dentro** restituisca una risorsa presa per un tempo analizzabile:
> quell'attesa è limitata dall'analisi, non da un clock.

È lo stesso principio di §13.5 applicato una seconda volta — là un ceiling
dichiarato male si aggiusta nella dichiarazione, non nel runtime; qui un'attesa
su semaforo che ha bisogno di un timeout è un semaforo il cui conteggio o il cui
tempo di tenuta non sono stati analizzati. Da cui la regola d'uso, nella forma
di quella di §13.2:

> **Aspetti qualcosa che può non arrivare? Mailbox, e un timeout. Aspetti una
> risorsa che qualcuno qui dentro restituirà entro un tempo calcolabile?
> Semaforo, e nessun timeout.**

Gli altri tre punti di §13.8 si chiudono come **corollari**, non come decisioni
separate: la variante di *segnalazione* non esiste (un `post` che si perde è
un'attesa di evento, e gli eventi passano dalla mailbox); il semaforo davanti al
pool è il caso canonico della regola; e l'ultimo punto è §13.4 e non una regola
nuova — bloccarsi su un semaforo tenendo un mutex è bloccarsi tenendolo.

**La porta lasciata aperta, col prezzo già misurato.** Se un giorno servisse
davvero un'attesa a tempo su una risorsa, la strada **non** è appendere un
timeout a `sem_wait`: è far consegnare al semaforo nella mailbox
dell'attendente, dove §9 sa già arrivare. Costa un pool che non possa
esaurirsi, dimensionato staticamente, perché una `post` che fallisce col pool
vuoto non ha una mossa — non è il gestore dei timeout, non può ripassare al tick
dopo.

#### Il principio che ha tagliato via tre proposte

> *«Non stiamo progettando uno scheduler realtime che sia in grado di ovviare ai
> bachi che uno sviluppatore può codificare, lo sviluppatore deve sapere cosa
> sta facendo.»*

La riga, detta in modo che si possa applicare:

> Il kernel controlla ciò che, non controllato, **corromperebbe le proprie
> strutture**. Non controlla se l'uso che ne fai ha senso.

Il pool ne è la prova: `coda.vasm` verifica `fwd == bwd == 0` non per proteggere
chi scrive, ma perché un `enqueue` di un nodo già in lista distrugge una lista
che appartiene anche ad altri — e infatti §10.3 non ferma la macchina,
restituisce `CODA_LINKED` e lascia all'applicativo la decisione di continuare.
Il controllo è per giunta gratis, perché quei due link la primitiva li sta già
guardando.

Sono cadute tre cose che erano state proposte in questa stessa sessione:

- **il tetto del contatore del semaforo**. Un `post` di troppo non corrompe
  niente — il contatore non è dereferenziato e non indicizza niente — è una
  risorsa restituita due volte, cioè un errore di costruzione di un sistema che
  è statico proprio perché quelle cose si chiudono prima di girare. Il secondo
  punto di §13.8 si chiude con **no**, non con un valore;
- il **contatore diagnostico** della profondità negativa;
- il controllo «mi sto bloccando su un semaforo con un timeout armato».

E una lettura sbagliata da correggere in §13.1: «sotto non c'è rete» **non è una
richiesta di rete**. «Un solo scrittore e un'invariante dichiarata» è disciplina
nel codice del kernel, cioè il lato buono della riga.

#### Il ceiling non ha una direzione conservativa

L'utente ha proposto, **come esperimento e dichiarandolo poi un baco
concettuale**: e se il ceiling fosse sempre definito sopra la priorità del task
più prioritario del sistema? Sembra una semplificazione enorme — §13.5 perde la
ragione di esistere per costruzione (la premessa (2) non può cadere), la coda del
mutex diventa irraggiungibile, spariscono `count`, la `TESTA` e persino `owner`,
24 byte diventano 8.

**È un baco, e sta nella parola «conservativo».** Il valore del ceiling *è*
l'insieme dei task esclusi: non è un margine, come si dimensiona un buffer un po'
più grande per stare tranquilli. Per difetto rompe la mutua esclusione (ed è
§13.5). Per eccesso rompe la garanzia temporale, e la rompe **sempre, su ogni
sezione critica**: il task più prioritario aspetta la sezione critica del meno
prioritario, cioè l'inversione di priorità che §7.4 esiste per limitare,
dichiarata come politica.

Da cui, e non era scritto da nessuna parte, **perché §13.5 deve esistere**: se
sovradichiarare fosse la direzione sicura, nessuno scriverebbe mai un ceiling
sbagliato — si metterebbe il massimo dappertutto. §13.5 è il prezzo di una
grandezza che deve essere **esatta**. Ed è la parola «per risorsa» della lista
dei decisi: il limite non è un'ottimizzazione sopra il mutex, è ciò che lo rende
un mutex invece che una sospensione dello scheduler.

Il danno concreto si misura su §9.2: lì si è comprata di proposito la proprietà
«la latenza di interrupt non dipende da quanti timeout sono armati», spostando
la scansione in un task a priorità massima. Un ceiling universale la ridà
indietro un piano sotto — la latenza del servizio di kernel torna a dipendere
dalla più lunga sezione critica applicativa, scritta da chiunque.

Sopravvive una cosa sola, e va tenuta al suo posto: `sched_lock`/`sched_unlock`
è una primitiva legittima dove serve escludere *tutti* i task e non una
sottoclasse, ma è il gemello di `cli` un piano più in su, non un mutex
economico, e non è il default di niente.

#### Dove si scrive chi usa un mutex

Né nel mutex né nel TCB, a runtime. Nel mutex sta il **risultato** (`ceiling`,
§13.3) e nel TCB non sta niente, come §13.3 già dice. L'insieme degli utenti è
l'**ingresso** del calcolo: non lo legge nessuno mentre la macchina gira, quindi
sta nel sorgente — nel `.vinc` del modulo che possiede il dato protetto, perché
per §8.6 chi può chiamare quel servizio è chi include quel `.vinc`.

La forma, verificata sull'assembler: `.equ NOME valore` accetta «un intero o una
costante già definita» ([manual.md:511](manual.md#L511)) e non ci sono
espressioni, quindi `max(...)` non è scrivibile. **Ma non serve: il massimo di un
insieme è uno dei suoi elementi**, quindi quello che si scrive è *quale task è
l'utente più prioritario*:

```asm
.equ MTX_POOL_CEILING  PRIO_GESTORE_TIMEOUT   ; utenti: gestore timeout, log, sensore
```

Un `.word 7` sarebbe un numero che nessuno può ri-derivare; un nome è
un'affermazione rileggibile, e segue da sola se cambia il valore numerico di
quella priorità. Grandezza **derivata**, non asserita — la stessa distinzione del
ceiling universale, dal lato giusto.

Corollario secco: il ceiling si calcola su chi **può** prendere il mutex, non su
chi lo prende. Un task che include l'interfaccia di un servizio che poi non
chiama alza il ceiling per niente, quindi **«non includere ciò che non usi»
smette di essere igiene e diventa una proprietà temporale.**

E il limite, detto per intero: niente di questo lo **verifica**. L'assembler non
può sapere quale task esegue una data `mutex_lock` — è un fatto sul grafo delle
chiamate, e un grafo delle chiamate non c'è. Metterlo nel `.vinc` non lo rende
controllato: lo mette dove lo vedrà chi dovrebbe cambiarlo.

#### La mailbox: il taglio

L'utente ha spinto su due punti, e il primo era già vero nel codice:

1. **un task può aspettare eventi da più sorgenti di interruzione, e i messaggi
   si accumulano prima che giri.** Vero e funzionante: `messageHandling.vasm:113`
   è un incremento vero. Il `li r4, 1` di `:135` è il ramo della **consegna**,
   dove il conto valeva −1 e dopo in lista c'è esattamente un messaggio. Quei due
   `li` (a `:135` e `:188`) sono il posto dove l'ipotesi «un ricevente per
   mailbox» di §8.1 è compilata dentro due istruzioni invece di stare in un
   commento;
2. **con due task sulla stessa mailbox si dovrebbe decrementare, non assegnare
   −1.** Aritmeticamente giusto, e infatti lo scenario proposto — arriva il
   messaggio del secondo, si sveglia il primo, vede che non è suo, lo rimanda e
   torna in `receive` — **funziona**, perché il secondo è davanti nella coda e
   chi torna si rimette dietro.

Ma con il destinatario **non in attesa** il giro non si chiude: chi si sveglia
rimanda il messaggio in una lista vuota, torna in `receive`, e **riprende lo
stesso messaggio**. Se è più prioritario del destinatario, quello non gira mai:
è il livelock di §13.5, stessa forma. E accodare il TCB *prima* di testare il
contatore non lo chiude — mette in lista un messaggio e un TCB insieme con
`count == 0`, cioè esattamente la finestra per cui §8.2 ha **rifiutato** la
convenzione contabile.

La radice: con N riceventi **qualcuno deve scegliere il destinatario**, e il solo
che sa a chi è indirizzato il messaggio è chi lo manda, perché l'indirizzo sta
nel payload che il kernel non guarda (§8.4).

> **IL TAGLIO, deciso dall'utente:** un solo TCB in attesa per mailbox; il
> contatore si incrementa per accodare messaggi.

Ne segue l'invariante `count >= -1` — **−1 è l'unico negativo possibile** — che
regge per costruzione: `receive` decide su un test a tre vie e accoda il proprio
TCB solo nel ramo `count == 0`, e `send` non decrementa mai. Quindi il `li r4,
-1` **è** quell'invariante scritta in un'istruzione, e la proposta di
sostituirlo con un `addi` **è ritirata**: sotto il taglio sarebbe generalità
morta.

Due cose da scrivere: §8.1 ha oggi l'argomento debole («servirebbe l'inserimento
ordinato», che è un costo) e va sostituito con quello forte — *con più riceventi
nessuno può scegliere il destinatario* — e i due `li` vogliono il motivo
accanto. E il taglio regge anche §9: con un ricevente solo il messaggio di
scadenza **non può essere prelevato da nessun altro**.

#### Il codice: `enqueue_dopo_nc`

La primitiva agnostica di §13.6, e **non è codice nuovo**: è una seconda
etichetta sullo stesso indirizzo di `enqueue_testa_nc` — `nm` le dà entrambe a
`31`. Il corpo di `r1` legge solo `fwd`/`bwd`, che sentinella e nodo
condividono, quindi «dopo la sentinella» e «in testa» sono la stessa istruzione:
la proprietà di §13.6 («la primitiva nuova le contiene») non è raccontata in un
commento, è il modo in cui è compilata.

Tre commenti del corpo generalizzati (`next = prec.fwd`, `nodo.bwd = prec`,
`prec.fwd = nodo`), che col contratto vecchio erano veri solo per metà. In testa
al file i tre vincoli di §13.6, e uno va ricordato perché è ciò che si perde:
**la testa non è un argomento**, quindi la primitiva non può verificare che
`prec` appartenga alla lista che il chiamante ha in mente. Verifica ciò che può,
ed è il controllo che conta: l'invariante dei link sul nodo che entra.

Variante contata **non scritta**, con la ragione accanto: l'unico cliente in
vista è la coda del mutex, che per §13.3 potrebbe adottare le `_nc` comunque.

Test: quattro sezioni nuove, sette valori attesi in più (`ctest` 23/23). La (10)
costruisce `[n1, n2]` e infila `n3` con `prec = n1` — l'inserimento in mezzo, che
è l'unica cosa che le due primitive vecchie non sanno esprimere. La (11)
verifica che con `prec` = sentinella si torni all'inserimento in testa: il
«contiene» controllato invece che assunto. In quelle sezioni la testa è gestita
con le `_nc` e il contatore resta a 0 — una sola disciplina per testa, anche in
un test, altrimenti l'esempio insegnerebbe la cosa sbagliata.

#### Il codice: `coda_api.vinc`, e l'interfaccia che pubblica le entry

Osservazione dell'utente: `coda.vinc` non dichiara gli `.extern` delle
primitive, quindi **l'interfaccia non è completa**. Vero, e contraddiceva §8.6
della proposta, che dice che un fornitore pubblica «gli indirizzi delle entry se
è un'API»; la regola opposta stava scritta in `hal.vinc` («qui ci sono solo
costanti, come in ogni `.vinc`»), ed è quella che ha ceduto.

Tre fatti verificati sull'assembler, e il secondo è quello che decide la forma:

- un `.extern` dichiarato e **mai referenziato non costa niente**: assembla,
  linka, non tira dentro nessuna libreria. Quindi mettere gli `.extern` in un
  header incluso da tutti non ha un costo di link;
- `.global X` + `.extern X` nello stesso modulo è un **errore secco**
  (`'X' is both defined and .extern`). E il fornitore include la propria
  interfaccia per gli offset: quindi **un file solo non è scrivibile**;
- `.extern` **non è idempotente** (stesso errore su due `.extern` uguali). È
  precisamente il difetto che `.include` aveva fino a §3.16 — ma la divisione lo
  rende irraggiungibile, quindi la toolchain non si tocca.

Da cui due file e due regole:

```
coda.vinc      i TIPI            lo include il FORNITORE e gli header i cui
                                 nodi sono liste (tcb, messaggio, pool)
coda_api.vinc  i tipi + le ENTRY lo include CHI CHIAMA — un file solo
```

> 1. un modulo include il `.vinc` dei **tipi** del proprio fornitore e l'`_api`
>    **degli altri**. Mai il proprio `_api`;
> 2. un `_api` include solo i tipi del proprio modulo, **mai l'`_api` di un
>    altro** — altrimenti potrebbe rimettere in circolo per via indiretta quello
>    di chi sta compilando, e il messaggio d'errore non dice da quale catena di
>    `.include` arriva.

La (2) ha un effetto voluto: **la lista degli `.include` torna a essere la lista
delle dipendenze**, che era il ruolo degli `.extern` scritti a mano in cima a
ogni sorgente e che si sarebbe perso spostandoli nell'header.

Il guadagno vero non sono gli `.extern`: è il **contratto di chiamata** —
argomenti, ritorno, e quali registri sporca ogni entry — che prima si trovava
solo aprendo l'implementazione. Con due dettagli che stavano fra le righe:
`dequeue_testa` non restituisce un `CODA_*` (la coda vuota è un esito, non un
errore) e lascia `r3` intatto; i wrapper `_s` non sono foglia e sporcano `r5`
oltre a ciò che sporca la raw.

Sei clienti aggiornati (`pool`, `messageHandling`, `scheduler`, la demo,
`test_coda`, `test_mailbox`): una riga di `.include` al posto della lista di
`.extern`. **Due commenti riscritti perché la divisione li ha resi falsi**, ed è
la parte che vale la pena ricordare:

- [`rtos/scheduler/impl/src/CMakeLists.txt`](../rtos/scheduler/impl/src/CMakeLists.txt)
  documentava *questo esatto caso* come la giustificazione di due parole chiave
  invece di una: «il kernel LINKa le code ma non ne dichiara l'interfaccia,
  perché `scheduler.vasm` chiama `enqueue_coda` senza aver bisogno di una sola
  costante di `coda.vinc`». Non è più vero. La distinzione non muore ma si
  sposta: **chi chiama include**, quindi LINK e INTERFACES coincidono per i
  chiamanti diretti e divergono solo su ciò che si linka per conto di qualcun
  altro — oggi `lib_hal`, e solo perché l'HAL non ha ancora il suo `_api`;
- [`hal.vinc`](../hal/interface/hal/hal.vinc) enunciava la regola di tutto il
  progetto. Ora è marcata come **lacuna** e non come regola.

#### La scoperta che ferma il mutex

Il mutex **non è scrivibile oggi**, e il blocco non sta in §13:

```
.struct TCB          fwd, bwd, sp, state    — 16 byte, nessuna priorità
ready:  .res TESTA   UNA sola coda di ready — non una per livello
scheduler            round-robin
```

Il modello di §3 e §4 — i PCB, una coda per livello, `TCB.pcb` (§7.3), lo slot
`preemptato` — **non è costruito**: `PCB` non compare in nessun sorgente, e
[`scheduler.vasm:28`](../rtos/scheduler/impl/src/scheduler.vasm#L28) lo dice al
futuro («si cambia politica RR → priorità riscrivendo solo `scheduler`»).

`mutex_lock` **è** «scrivi la priorità del task corrente al ceiling» e
`prio_prec` **è** «la priorità di prima»: sono operazioni su una grandezza che
non esiste ancora. Scriverlo adesso vorrebbe dire inventare la rappresentazione
della priorità come effetto collaterale. Ferma anche il **semaforo**, per una
via diversa: §13.6 vuole l'inserimento ordinato, e la camminata deve confrontare
le priorità dei TCB accodati.

Che `task_block`/`task_ready` esistano solo come stub in `test_mailbox.vasm`
**non** è il blocco: quel debito la mailbox l'ha già preso, sono `.extern` con
il contratto scritto.

> **Il prossimo passo del kernel è §3/§4, non §13**: i PCB, una coda per
> livello, il campo nel TCB, e la politica dello scheduler da round-robin a «il
> livello non vuoto più alto». Mutex e semaforo vengono dopo, e sono corti,
> perché tutto il resto è deciso.

#### Cosa resta da scrivere (niente di quanto sopra è nella proposta)

| Dove | Cosa |
|---|---|
| §13.8 | riscritta come **deduzione da §9**, più la regola d'uso, i corollari, il tetto chiuso con un no, e la porta lasciata aperta col suo prezzo |
| §7.5 | emendata: la mailbox è l'unico punto di blocco **raggiungibile da un timeout**, e il semaforo è un secondo punto di blocco proprio per questo privo di timeout |
| §8.1 | l'argomento vero: con più riceventi nessuno può scegliere il destinatario |
| §13.5 | il ceiling non ha una direzione conservativa, e perché la sezione deve esistere |
| §13.3 | la forma della dichiarazione del ceiling (`.equ` che nomina l'utente più prioritario, nel `.vinc` del fornitore) |
| §13.1 | «sotto non c'è rete» non chiede una rete |
| ~~`messageHandling.vasm`~~ | ~~il motivo accanto ai due `li` (`count >= -1`)~~ — **FATTO in §3.27** |
| ~~—~~ | ~~il terzo campo di `TESTA` nominato dal proprietario~~ — **DECISO e implementato in §3.27**, e non come `.struct` propria |
| — | estendere gli `_api` a `pool`, `timeout`, `messaggio`, `hal`: l'utente ha detto che il modello convince. L'HAL è quello che rende di più — oggi «`irq_save` restituisce la psw in `r5`» si scopre solo leggendo `machine.vasm` |

---

### 3.38 LO SCHEDULER SENZA IL TICK, e i tre difetti che il test ha stanato (11/09/2026, terza parte)

`ctest` **30/30**, il nuovo è `coop`. L'ha chiesto l'utente con una frase che
inquadra un limite di tutto quello che c'era prima: *«ti sei sempre basato su un
sistema che usa dei timeout, ma questo non è l'unico modo di lavorare — non
esiste solo la preemption da interruzione»*. Aveva ragione: ogni test del kernel
arma il timer, e a furia di leggerli la preemption da interrupt finisce per
sembrare *il* modo in cui il sistema funziona.

#### Cosa dimostra

Un testimone — **un solo `MESSAGGIO`** — che gira A→B→C→A cinque volte, con il
timer **mai armato** e `IE` a zero per tutta la vita del programma. La lista
degli `.extern` è l'asserzione più forte del file: niente `irq_install`, niente
`timer_init`, niente `irq_enable`, niente `sched_isr_exit`.

Il percorso che esercita da solo esiste dal 07/09 e non era mai stato messo alla
prova senza rete: `task_block` chiama `scheduler` e cade nel `dispatcher`
**senza passare dall'ISR**. C'era sempre un tick a coprirlo.

Le **priorità sono dichiarate in ordine diverso dal giro** — il giro è A→B→C, le
priorità sono B(1), C(2), A(3) — ed è ciò che rende il test sensibile: se lo
scheduler scegliesse per priorità invece di seguire il testimone, l'ordine
cambierebbe e `err` lo direbbe.

#### L'idle, che è il vero contenuto — e una precisazione su cosa misura

La domanda dell'utente («la preemption c'è perché l'idle verrà preemptato, no?»)
ha tirato fuori la cosa che vale più del ping-pong: **in un sistema puramente
cooperativo l'idle non è un task di riempimento, è la prova che serve una
sorgente asincrona.** Lì «nessuno è pronto» non significa «aspetta»: significa
che il sistema è finito, perché l'idle non si blocca mai (§4) e nessuno può
togliergli la CPU — un `task_ready` armerebbe `g_resched` e quel flag resterebbe
armato per sempre, visto che l'unico consumatore è `sched_isr_exit`.

Ma il quinto numero **non lo misura**, e sarebbe stato facile ingannarsi: `cntI`
nel dump non può che valere 0, perché se l'idle girasse il dump non verrebbe mai
eseguito. L'osservabile vero è che il test **termini** — la stessa forma di
`cntU` in `test_mutex`, dove il livelock non farebbe fallire il test ma non lo
farebbe finire. Corretto nei commenti invece di lasciar credere il contrario.

#### Difetto 1: `.word` con una costante `.equ` scrive ZERO in silenzio

`ultimo: .word ID_C` assembla senza un fiato e mette **0**. Il manuale è
corretto — `.word` accetta «decimale o esadecimale» (§4.2) — e nel resto del
progetto nessuno ci era inciampato: gli unici riscontri sono commenti che dicono
«`.word` non accetta etichette». Ma accettare un identificatore e azzerarlo senza
dire niente è l'opposto di come si comporta il resto della toolchain. Costava un
`err=1` al primo giro, cioè l'unico controllo che guarda il valore iniziale.

**Non corretto**: rifiutarlo in fase di assemblaggio è una modifica a
`assembler.c`, e va decisa. Nel test il valore si scrive nel boot.

#### Difetti 2 e 3: `traccia.py` non reggeva un programma senza tick

La pagina HTML **non disegnava niente**, e la causa era una riga sola:

```js
["CPU libera", Math.round(100 * (own.idle.task + own.idle.kernel) / gran) + "%"]
```

`D.own` contiene solo `A`, `B`, `C`, `boot` — **l'idle non c'è perché non ha mai
girato** — quindi `own.idle` è `undefined` e quella riga lancia una `TypeError`.
Sta nel masthead, cioè *prima* del disegno: moriva lo script, non il diagramma.
Sotto ce n'era una seconda pronta a scattare: lo zoom cerca l'intervallo fra due
tick, e con zero tick calcolava `z0`/`z1` da `undefined`.

La correzione della seconda è concettuale, non una toppa: **le linee di
riferimento del diagramma sono i punti in cui il kernel riprende la macchina** —
il tick in un sistema preemptivo, il **passaggio di turno** in uno cooperativo.
Quando i tick mancano la pagina disegna quelli (15 confini, escluso il boot), e
il titolo dello zoom cambia, perché «Un tick, fascia per fascia» direbbe il falso.

> Verificata la non regressione su `test_gestore`, che ha 12 tick e prende il
> ramo di prima. Il JS non si è potuto **eseguire** in fase di correzione (non
> c'è `node` sulla macchina, e Firefox era aperto sul profilo dell'utente quindi
> l'istanza headless non parte): **l'utente ha confermato che la pagina si vede**.

#### Quello che la traccia ha mostrato, e due artefatti di attribuzione

Quindici turni in sequenza rigida, `0 tick`, l'idle assente dall'elenco, e un
ritmo sorprendentemente regolare: 686/696/716 cicli a testa, **identici giro dopo
giro**. Senza tick non c'è niente che possa spostare i confini — ogni turno è
esattamente «ricevi, controlla, conta, manda, bloccati».

Due cose vanno lette sapendo cosa sono, e sono limiti dello **strumento**:

- **19 cicli per turno attribuiti a `boot`**: sono `controlla` e `err_piu`,
  procedure locali del test che girano per conto del task chiamante ma stanno
  fisicamente fra `main` e `taskA` nel listato. Stessa famiglia del caso
  `gestore_tick` di §3.34, in una forma che quella correzione non copre — lì il
  rimedio era fermare il corpo al primo simbolo *pubblicato*, e qui il simbolo
  pubblicato che precede è proprio `main`;
- **`_trap_entry` e `sched_isr_exit` compaiono in un programma senza interrupt**.
  Sono etichette di attribuzione: `ctx_save` non è `.global` e finisce contata
  dentro `_trap_entry` (§3.34 lo dichiara), e qui la chiama `hal_ctx_block` sul
  percorso **volontario**; allo stesso modo `scheduler` e `dispatcher` non sono
  pubblicati e cadono dentro `sched_isr_exit`. Il tempo è attribuito al task
  giusto, il nome è fuorviante.

Resta anche, non toccato, che tutto il **testo narrativo** della pagina è scritto
attorno a `test_gestore` (il gestore a priorità 0, §3.29, l'idle che non conta il
doppio): su qualunque altro programma è fuorviante, e adesso si vede.

---

### 3.37 LA MACCHINA HA UN INGRESSO: MMIO e la tastiera in polling (11/09/2026, seconda parte)

`ctest` **29/29**, il nuovo è `kbd`. Fino a oggi il sistema aveva **una sola
sorgente di eventi** — il timer, cioè una cosa periodica che il programma stesso
si era armato — e nessun modo di ricevere niente dal mondo. È la ragione per cui
ogni spiegazione finiva per girare attorno al tick, e l'utente l'ha fatto notare:
*«non esiste solo la preemption da interruzione»*.

#### La proposta è dell'utente, e le ho cambiato il livello

L'utente ha proposto un **thread** che simuli l'hardware, capace di generare
interrupt o di essere letto in polling — *«skip on flag»*, che è il `KSF` del
PDP-8. Il modello è giusto e non è una stravaganza: è come sono fatti QEMU e
SystemC, e il device *è* concorrente alla CPU.

Le obiezioni non erano al thread ma a due conseguenze. **Il determinismo**: 28
test con `EXPECT` esatti smettono di essere verificabili se l'evento arriva
quando decide lo scheduler di Linux. E **il tempo**: la CPU conta cicli, un
thread vive in millisecondi veri, e le due grandezze non sono commensurabili.

Da lì la forma che è stata scritta: **separare lo stato del device dal suo
alimentatore**. Il programma vede un flag, un dato e (domani) una linea di
interrupt; sotto ci può stare una traccia a cicli — tempo simulato,
deterministica, rigiocabile in `ctest` — oppure un thread che legge `stdin`. Il
`.vasm` non distingue i due casi, ed è la proprietà che conta: **la stessa
applicazione potrà essere insieme la demo e il test**, invece di dover scegliere.

#### Le due decisioni di disegno che hanno risparmiato lavoro

**Nessuna istruzione nuova: MMIO.** Su una macchina vera i registri di
periferica si leggono con una load, e qui il punto di innesto **esisteva già** —
ogni accesso scalare passa da `load_i32`/`store_i32`, che avevano già un `if` sui
limiti. Il decoder, l'assembler, la ISA e tutti i `.vasm` esistenti non sono
stati toccati. Lo *skip on flag* diventa `lw` più `beq`, cioè quello che questa
ISA sa già fare.

> Un dettaglio che vale come segnale: `load_i32` prendeva un `const VCpu*`, e
> quella `const` **è caduta**. Non per una ragione tecnica: era la dichiarazione
> che «leggere non ha effetti», vera finché la memoria era solo memoria, e cade
> nel punto esatto in cui smette di esserlo — leggere `KBD_DATA` consuma il
> carattere.

**I registri stanno SOPRA la RAM**, a `0x100000`, non dentro. Non sottraggono un
byte ai programmi, nessun linker deve sapere di doverli evitare — e oggi non
saprebbe, perché concatena in command order senza regioni — e una collisione fra
un dato e un registro è impossibile invece che improbabile. L'`imm` di `Instr` è
un `int64_t`, quindi `li r1, 0x100000` non ha bisogno di niente.

#### Cosa è stato scritto

| Dove | Cosa |
|---|---|
| [`vcpu.h`](../include/vcpu.h) | `MMIO_BASE`/`KBD_*`, `KbdEvent`, i tre campi di stato del device e la traccia |
| [`vcpu.c`](../src/vcpu.c) | `mmio_load`/`mmio_store`, l'intercettazione in `load_i32`/`store_i32`, `kbd_pump` al confine d'istruzione, il parser della traccia |
| [`main.c`](../src/main.c) | `--kbd "ciclo:car,..."`, su entrambi i percorsi (legacy e `run`) |
| [`hal/kbd.vinc`](../hal/interface/hal/kbd.vinc) **(nuovo)** | gli indirizzi e i bit, con il perché di ognuna delle scelte |
| [`tests/test_kbd.vasm`](../tests/test_kbd.vasm) **(nuovo)** | cinque numeri derivabili a mano: `0 294 0 3 121` |
| `cmake/vasm.cmake` | `ARGS` in `vasm_check`: opzioni per la **macchina**, distinte dagli `IFLAGS` che sono per l'**assembler** |
| `docs/manual.md` | §3.1 nuova, e `--kbd` in §2.3 |

I numeri del test: `'a'+'b'+'c' = 294`, e `'y' = 121` è il **secondo** dei due
che arrivano allo stesso ciclo, perché un overrun tiene il più recente. Lo `0` in
mezzo è il flag abbassato **dalla lettura**, cioè il protocollo che funziona
senza handshake.

#### Cosa NON è stato scritto, di proposito

- **nessun interrupt, e nessun registro per abilitarlo.** La macchina ha ancora
  una sorgente sola e un solo vettore; aggiungerne una seconda vuol dire una
  causa leggibile (stile `mcause`) o un secondo vettore, più il problema nuovo
  dell'**annidamento** — oggi l'HAL entra in trap con `IE` a zero, quindi una
  seconda sorgente resterebbe pendente per tutta la durata dell'ISR. È una
  decisione, non un'aggiunta;
- **nessun thread**: è il secondo alimentatore, e si innesta sugli stessi tre
  campi senza toccare né il `.vinc` né i programmi;
- **nessuna libreria di primitive**: leggere la tastiera è `lw` + `beq`, e una
  procedura attorno a due istruzioni sarebbe solo un prologo in più.

#### L'osservazione da tenere

È la **seconda volta in un giorno** che manca il locator: l'indirizzo base dei
device è cablato in un `.equ` perché il linker non conosce regioni. Funziona, ed
è quello che si fa in ogni kernel prima di avere un device tree — ma quando lo
scriverai, i device saranno il suo primo cliente vero.

#### Il seguito della discussione: il sincronizzatore, PROGETTATO E NON SCRITTO

La sera l'utente ha rilanciato, e l'obiezione che gli avevo fatto era **mal
posta**. Avevo detto che un device in un processo separato vive nel tempo di
parete e perde il determinismo: è vero del *thread*, non del processo. Con un
**clock comune** il tempo simulato resta uno solo e i partecipanti avanzano
quando lui lo concede — è come funziona SystemC, dove il tempo è del kernel di
simulazione e non dei moduli. La domanda giusta non è «thread o traccia», è
**chi possiede il tempo**.

E l'utente vuole di più di un device: un sincronizzatore per **più VM**, con due
macchine che si parlano come su una linea seriale. Che per il dominio di ottobre
non è un esercizio — un apparato satcom è fatto di processori che si scambiano
messaggi su un bus.

È nata [`docs/proposta-sincronizzazione.md`](proposta-sincronizzazione.md), che
**congela le decisioni senza implementare niente**. Le tre che contano:

- **il tempo comune è assoluto (nanosecondi simulati), non in cicli** — due VM
  possono avere frequenze diverse, ed è la configurazione normale di un apparato
  vero. Va deciso alla prima riga, perché cambiarlo dopo riscrive ogni messaggio;
- **l'arbitro è un processo separato.** Con una VM sola si poteva farle possedere
  il tempo; con due nessuna può essere arbitro e parte insieme;
- **il quanto di sincronizzazione ≤ latenza minima del canale**, e da qui il
  risultato controintuitivo: più il canale è lento, più veloce va la
  simulazione. Il *lookahead* non è un'approssimazione concessa, è una proprietà
  fisica del sistema simulato. Stessa forma del tick («il tick è il quanto»).

**Sull'ordine non sono d'accordo, e sta scritto in §12 del documento**: un
sincronizzatore è infrastruttura e vale per ciò che ci gira sopra. Costruito
adesso, avrebbe sotto due VM con lo stesso kernel che non sa ancora fare la cosa
per cui esiste — i registri vettoriali volatili. Prima quel fronte, poi
l'applicazione che lavora davvero, e il sincronizzatore nasce con un cliente.

---

### 3.36 LA DISCUSSIONE APERTA SI È CHIUSA: chi è in sezione critica non si ruota (11/09/2026)

`ctest` **28/28**. Il fronte che il 10/09 era rimasto a metà frase è chiuso, e la
decisione **non è quella che io raccomandavo all'inizio della sessione**: ci si è
arrivati per una strada che l'utente ha impostato da zero, ripartendo dai semafori
del 1985.

#### Il chiarimento che ha sciolto il nodo: non erano due problemi, era uno

Il 10/09 l'handoff registrava due cose come «collegate»: il caso dell'affiorante
di priorità **pari** all'ex detentore, e la rotazione che invalida §13.5. Sono
**lo stesso caso**.

All'unlock l'affiorante è pari all'ex detentore esattamente quando
`prio_prec(S) == ceiling` — cioè quando S, prima del lock, stava già al livello
del ceiling. E chi sta a quel livello? Per definizione di come il ceiling si
dichiara, **l'utente più prioritario del mutex**. Possessore promosso e utente
più prioritario abitano lo stesso livello *per costruzione*, non per una
coincidenza del test.

Da lì tutto si allinea. `task_ready` confronta con una `blt` **stretta** e li
tiene separati: quello è il pezzo **corretto**, ed è la premessa su cui §13.5
poggia («maggiore *o uguale* esclude che tu stia girando»). `sp_mio_livello` è
l'unico meccanismo del kernel che agisce **sull'uguale**, e al livello di un
ceiling l'uguale sono gli utenti. Il `0 1 1 1 1` non era un terzo caso sfortunato
accanto ai due di §13.5: era il solo posto in cui quel meccanismo poteva mordere.

#### Il giro storico, che ha cambiato la domanda

L'utente ha riportato il problema al modello che usava prima che ICPP esistesse —
semaforo binario, nessuna promozione, l'ex detentore che continua a girare — e ha
chiesto cosa cambia col mutex. Tre risposte, e la seconda è quella che conta:
sotto un ceiling corretto **il ramo «occupato» non si esegue mai**, perché
l'attesa non è stata accorciata, è stata **spostata prima** della sezione
critica. Il task alto non aspetta in coda: non viene ancora schedulato.

Da cui la domanda vera — *«è giusto far girare sempre e comunque lo
scheduler?»* — e la risposta che ha dato la chiave: **sì per il rescheduling, no
per la rotazione**, e nel nostro kernel sono la stessa `call`. Il rescheduling
all'unlock è obbligatorio (abbassare la propria priorità è indistinguibile,
per lo scheduler, dall'arrivo di uno più prioritario); il round-robin fra pari è
un'aggiunta nostra, **estranea al modello in cui ICPP è dimostrato** — PCP e ICPP
assumono tie-breaking non preemptivo fra pari, e la response-time analysis somma
l'interferenza solo sui task *strettamente* più prioritari. È la ragione per cui
esistono `SCHED_FIFO` e `SCHED_RR` separati.

#### Le due strade percorse, e perché ha vinto la seconda

L'utente ha riproposto il **ceiling universale** — tutti i mutex a priorità 0,
nessun task dichiarabile a 0 — che il 07/09 aveva già proposto e bocciato lui
stesso come «baco concettuale». Riaprirlo era legittimo: il 07/09 il confronto
era fra un ceiling esatto *gratis* e uno universale *caro*, e oggi sappiamo che
quello esatto non è gratis. Ma è caduto su un'**identità**: un mutex che esclude
*tutti* i task invece di una sottoclasse non è un mutex economico, è
`sched_lock`/`sched_unlock` con 8 byte di stato attorno. La domanda diventava
«ci serve un mutex o ci serve `sched_lock`?», e la risposta del progetto è la
prima.

Poi il **ceiling un livello sopra** (`max(utenti) − 1`), che svuota il livello
del possessore dagli utenti. Funziona, e per un po' è sembrata la strada.
L'ha affondata un esempio dell'utente — utenti a 2 e 3, ceiling 1 — che ha fatto
vedere il costo nascosto: un task nominalmente a 1, **estraneo al mutex**, con il
ceiling classico preempta il possessore e non paga niente; col `−1` diventa suo
pari, non lo preempta più, e si prende un termine di blocking per una risorsa che
non usa. Il `−1` sposta il costo dove la teoria non lo prevede.

> Dentro quel ramo è nata anche l'idea dell'utente di accodare il possessore al
> **secondo posto** invece che in fondo: la dilatazione della sezione critica
> passerebbe da `n × quanto` a un quanto, indipendente da quanti pari ci sono.
> È buona, ma paga lo stesso campo nel TCB dell'uscita scelta e non risolve la
> correttezza da sola (il turno ceduto andrebbe comunque a un utente). Resta in
> archivio: serve solo se il livello del ceiling non si può tenere vuoto.

#### La decisione, e cosa è stato scritto

**Ceiling classico + il possessore non è ruotabile.** Il blocking cade esattamente
su chi usa la risorsa, che è ciò che ICPP promette; nessun livello di priorità
consumato; il gestore dei timeout resta a `PRIO_MAX`; §13.5 non si riscrive, le
si aggiunge la **quarta premessa** che era rimasta implicita.

Il campo è un **contatore**, e la ragione è il caso che la scelta ovvia non
copre: dedurre «sono promosso» da `TCB.pcb != nominale` fallisce proprio quando a
prendere il mutex è l'utente più prioritario — la promozione è **vuota**, e il
possessore sarebbe indistinguibile da chiunque altro nel caso peggiore. Quello da
rilevare non è «sei promosso» ma **«tieni un mutex»**.

| Dove | Cosa |
|---|---|
| [`tcb.vinc`](../rtos/scheduler/interface/tcb/tcb.vinc) | `TCB.crit` (+20), e il riquadro sul perché non è la priorità nominale |
| [`scheduler.vasm`](../rtos/scheduler/impl/src/scheduler.vasm) | `sp_mio_livello`: `lw` + `bne` e si va a `sp_solo` |
| [`mutex.vasm`](../rtos/servizi/mutex/impl/src/mutex.vasm) | `lock` incrementa; `unlock` decrementa su di sé e incrementa su chi riceve la consegna diretta |
| [`mutex.vinc`](../rtos/servizi/mutex/interface/mutex/mutex.vinc) | la terza causa che c'è stata dal 07 all'11/09, scritta perché la sua assenza non è gratuita |
| [`test_mutex.vasm`](../rtos/test/test_mutex.vasm) | `call request_preempt` nell'ISR: la storia A prova ora anche la rotazione |
| `CMakeLists.txt`, `rtos/test/CMakeLists.txt` | `TCB.size` 20 → 24; `test_scheduler` 74→73 e 59→58 |
| `proposta-kernel-realtime.md` | §13.5 (la quarta premessa), §2 (la terna, e lo yield che non esiste), §7.4 (la casella «nulla»), §13.7 (perché armare incondizionatamente è giusto), §13.9 |

**Il numero che NON si è mosso è `cntD`** nel test dello scheduler: lì nessuno
tiene un mutex, quindi la rotazione deve funzionare esattamente come prima, e
funziona. Gli altri due scendono di uno perché due istruzioni si pagano.

E la controprova è stata eseguita **prima** della cura: con la sola `call
request_preempt` aggiunta al test, `attOK` passava a 1 — un TCB accodato a un
mutex dichiarato correttamente. Poi la cura, e `0 0 1 1 1`.

#### La coda della sessione: il mutex non è rientrante, e adesso è scritto

Domanda dell'utente a lavoro finito, e la risposta non c'era da nessuna parte.
`mutex_lock` non confronta `owner` con `current`, quindi chi rilocca si accoda
alla coda d'attesa di un mutex che possiede lui e chiama `task_block`: perduto.

Non è un caso nuovo — **è §13.4**: chi rilocca si sta bloccando tenendo un
mutex, la premessa che cade è la (3) di §13.5, e l'osservabile esiste già
(`attese != 0` con `owner` uguale al TCB sospeso). Interessante il corollario
sulla dimostrazione: il passaggio «un eseguibile a priorità ≥ esclude che `T`
giri» è **vacuo quando quel task è `T`**, quindi la non-rientranza non è una
premessa da aggiungere all'elenco, è un'ipotesi sulla forma dell'enunciato.

**Implementare la rientranza è stato valutato e scartato**, e l'argomento non è
il costo: è che la rientranza serve a chi **non sa** se il mutex è già preso,
mentre qui lo si sa per costruzione (il ceiling si dichiara nominando gli utenti,
§8.6 lega chi chiama a chi include). In più delimiterebbe la sezione critica sul
grafo delle chiamate invece che fra `lock` e `unlock`, e darebbe al `lock` un
ramo che nel funzionamento normale **deve** eseguire — mentre oggi ogni
passaggio dal degrado significa «qualcosa è rotto». Nessun `assert`, per il
criterio di §3.26: un rilock non corrompe niente.

Scritto in §13.4 della proposta e in testa a `mutex.vinc`, in forma di
**contratto** e non di avvertimento — sull'osservazione dell'utente che un
ingegnere del software queste cose le sa, cui va aggiunto che pretenderlo è
legittimo solo se il contratto sta scritto.

#### Cosa resta aperto

- **§13.7**, come prima e per la stessa ragione: `task_yield` è kernel e va
  deciso, non dedotto. Oggi la sessione ha *confermato* che armare il flag
  incondizionatamente è giusto, quindi il buco è solo il consumo sincrono;
- **la seconda causa di §13.5 non è testata**: nessun test prova il possessore
  che si blocca volontariamente dentro la sezione critica. La coda non distingue
  le due cause, ed è dichiarato che non le distingue, ma l'osservabile andrebbe
  visto scattare anche per quella;
- la **fascia** al posto della scansione generale (§13.7) resta una proposta non
  decisa, e non è stata scritta.

---

### 3.35 §13 È SCRITTA: semaforo e mutex, e la primitiva che l'utente ha aggiunto a `coda.vasm` (10/09/2026)

**La sessione che chiude §13.** `ctest` **28/28** — le 26 di prima intatte, più
`semaforo` e `mutex`. Il codice sta in `rtos/servizi/`, accanto alla mailbox,
esattamente dove `servizi/CMakeLists.txt` diceva da giorni che sarebbe nato.

E la proposta è stata riallineata: le **cinque voci** che §3.26 lasciava fuori
sono dentro, più le tre correzioni che scrivere il codice ha prodotto.

#### La discussione che ha cambiato il disegno, ed è tutta dell'utente

Ero partito male: stavo per mettere la camminata di §13.6 nello **scheduler**,
come routine condivisa fra `sem_wait` e `mutex_lock`, per non scriverla due
volte. Alla domanda dell'utente — *«cominci dal kernel per fare cosa?»* — non
c'era una risposta buona: §13.6 dice dove va la camminata, ed è *dentro `sem_wait`
o `mutex_lock`*. Il confine che §13.6 protegge è quello verso `generic/coda`, non
verso lo scheduler.

Poi due passaggi, e il secondo ha corretto il primo:

1. **«Perché l'inserimento ordinato non lo metti in `coda.vasm`? Il chiamante
   deve solo selezionare *dove*.»** — È già così, ed è `enqueue_dopo_nc`. Quello
   che mancava non era l'inserimento, era la **scansione**: `enqueue_dopo_nc` dice
   dove mettere il nodo e non c'è niente che permetta di *cercare* quel punto.
   Avevo proposto una `enqueue_ordinato_nc` con l'**offset della chiave** come
   argomento — `coda.vasm` legge una parola a un offset che gli dice il
   chiamante — che è una forma che §13.6 non aveva considerato e che non crea il
   ciclo `generic/ → rtos/`.
2. **«Metti una `peek` in `coda.vasm` e confronti la tua chiave nel
   chiamante.»** — Ed è meglio, perché il confronto è **politica** e con
   l'offset una scheggia di politica rientrava dentro `coda.vasm`. Poi la forma
   finale: **«per fare quello che vuoi tu detectando la testa, il chiamante lo
   può fare con il numero di elementi accodati, no?»**

Sul secondo punto avevo proposto due argomenti (`testa` + `corrente`), così che
la primitiva restituisse `0` a fine giro e il chiamante non vedesse mai la
sentinella. **L'argomento dell'utente è più forte del mio, e va tenuto:**

> Il chiamante il numero di nodi lo sa **per definizione** — è §8.3, «la
> contabilità è del chiamante» — e un ciclo **limitato da un conteggio** è più
> robusto di uno che si ferma su un terminatore. §13.6 chiede che il limite della
> sezione critica sia *dichiarato*: contando diventa **strutturale**. Un ciclo che
> gira N volte non scappa nemmeno su una lista corrotta; uno che cerca la
> sentinella girerebbe per sempre.

Da cui `coda_peek(corrente) -> corrente.fwd`, un argomento, due istruzioni. Sta
**fuori dai due assi dei suffissi**, e va detto perché non è una dimenticanza:
non è `_nc` (il contatore non lo tocca nessuno: legge e basta) e non avrà una
`_s` (la sezione critica deve comprendere tutta la camminata).

**La ragione per cui esiste, e non è il risparmio di istruzioni.** Oggi i quattro
moduli che leggono `LINK.fwd`/`bwd` fuori da `coda.vasm` — `messageHandling`,
`pool`, `test_coda` — lo fanno su un nodo che sta **fuori da ogni lista**, per
verificare `fwd == bwd == 0`. Non attraversano niente. `sem_wait` sarebbe stato il
primo a camminare davvero, e quindi il primo modulo del progetto a dipendere dal
fatto che la lista è **circolare** e che la testa è una **sentinella**: forma
della struttura, non interfaccia.

La camminata vera è poi finita scritta **due volte**, in `sem_wait` e in
`mutex_lock`, ed è una scelta: un terzo modulo che la esportasse costerebbe una
libreria, un `.vinc` e un arco fra due servizi per risparmiare nove istruzioni. E
le due non sono lo stesso codice per caso — quella del semaforo è il percorso
**normale**, quella del mutex è il percorso che sotto un ceiling corretto **non
deve mai eseguire**.

#### `prio_pcb`, e il buco non era del mutex

`mutex_lock` *è* «scrivi `current.pcb = ceiling`», quindi nel mutex il ceiling
dev'essere un **indirizzo di PCB**. Ma per §3.26 si dichiara come **nome di
priorità**, cioè un numero:

```asm
.equ MTX_POOL_CEILING  PRIO_GESTORE_TIMEOUT
```

e fra i due non c'era ponte: `.equ NOME pcb0` non è scrivibile (§3.27 — costanti
ed etichette sono due spazi di nomi risolti in momenti diversi) e nemmeno
`.word pcb0`.

Guardando meglio, **il buco non era del mutex**: `tcb.vinc` pubblica `PRIO_MAX` e
`PRIO_IDLE` da giorni, e niente poteva trasformarli in ciò che il runtime usa —
infatti comparivano solo dentro i commenti mentre ogni boot scriveva
`li r4, pcb2` a mano. Il mutex è stato solo il primo a inciamparci.

Quindi lo scheduler pubblica `prio_pcb(livello) -> &PCB`, quattro istruzioni.
**Sta nel kernel e non nel mutex** perché l'aritmetica che fa *è* l'invariante di
§7.3 — la tabella dei PCB contigua e in ordine — che `scheduler.vasm` dichiara
come la cosa da non rompere: farla uscire significherebbe che un secondo modulo
la conosce senza essere quello che la garantisce. È l'unico punto del sistema in
cui una priorità è un numero.

`test_mutex` lo usa anche per il boot (`li r1, PRIO_S; call prio_pcb`), e si vede
il guadagno: le priorità dei task diventano **nomi** invece di `pcbN` cablati.

#### Il semaforo, e le due frasi di §13.1 che il codice ha smentito

`sem_init`/`sem_wait`/`sem_post`, più `semaforo.vinc` col tipo come `.equ`
derivati da `TESTA` (la forma di §3.27).

**Il decremento si paga sempre, poi si guarda il segno.** Non ci sono due rami:
`risorse--` e basta, e se il risultato è negativo ci si accoda. È il contatore
contabile che si scrive da sé — «ho preso una risorsa» e «mi metto in coda per
una risorsa» sono la stessa riga di contabilità — ed è la ragione per cui al
risveglio non c'è niente da pagare.

Due cose che §13.1 diceva e che non reggono:

- **`sem: .word 0, 0, 10` non è scrivibile.** Una `TESTA` vuota non è fatta di
  zeri (`fwd = bwd = &se stessa`) e un `.word` non accetta etichette. È lo stesso
  motivo per cui `sched_init` esiste, scritto in §6 della proposta da prima di
  §13. Il conteggio iniziale è un **argomento** di `sem_init`. L'osservazione di
  §13.1 resta giusta (è il primo oggetto che non nasce «tutti zeri»), la forma
  no;
- **il ciclo di ricontrollo al risveglio non serve.** §13.1 dava per persi
  entrambi i regali della mailbox; l'ordinamento sì, il ricontrollo no. §13.1
  stava pensando a un semaforo *signal and continue*, in cui il `post` rimette la
  risorsa nel mucchio e sveglia qualcuno perché **riprovi**. Se `sem_post`
  **consegna** — come la `send`, §8.5: sfila l'attendente e incrementa nello
  stesso passo — il conto non torna mai positivo, quindi fra la sveglia e
  l'esecuzione non c'è niente da rubare. La proprietà che la mailbox compra con
  «un ricevente solo» il semaforo la compra con la consegna diretta, e regge con
  N in attesa.

#### Il mutex, e la struttura che non è quella scritta in §13.3

`mutex_init`/`mutex_lock`/`mutex_unlock`. §13.3 disegnava `MUTEX` come «`TESTA` +
tre campi» lasciando aperto se il `count` andasse di disciplina contata o `_nc`.
La forma vera:

- **una `.struct` con la `TESTA` ANNIDATA**, come fa `PCB` in `tcb.vinc` — non
  dei `.equ` derivati come mailbox e semaforo, perché quelli *sono* una `TESTA` e
  il mutex ha tre campi in più; ma la `TESTA` si annida e non si ridichiara, così
  `&mutex == &mutex.coda` e il mutex si passa dritto alle primitive di coda;
- **`_nc` e contatore al mutex**, perché l'inserimento ordinato esiste solo in
  quella variante. Una testa, una sola disciplina. Il nome `MUTEX.attese` non
  cambia il significato (è il numero di nodi, la convenzione generica): serve a
  portarsi dietro l'invariante di §13.5, «vale 0 sempre»;
- **niente contatore diagnostico e niente `assert`**, e §13.5 lo chiedeva. Il
  campo c'è comunque perché la `TESTA` ce l'ha: chi vuole sorvegliare **lo
  legge**, e non costa un'istruzione a nessun altro.

La promozione è un **minimo**: `blt ceiling, current.pcb` e si scrive solo se
scatta. Quando **non** scatta non è un ramo mancante, è il passo 1 di §13.5 — il
possessore è già più prioritario del ceiling, e la protezione ha appena smesso di
funzionare senza che nessuno se ne accorga.

#### I due test, e le due controprove

Sono i primi test della serie in cui **nessun numero dipende dai cicli**, e
nemmeno la sequenza degli eventi: l'ISR non conta i tick, guarda lo **stato**
(`SEMAFORO.risorse`, `MUTEX.owner`, `MUTEX.attese`) e agisce quando il sistema ha
la forma che serve.

**`test_semaforo` — `123456 2 1 -6`.** Sei task che arrivano al semaforo in un
ordine diverso da quello di priorità, e `ordine` è la lista letta con `coda_peek`
e resa in cifre. H e F non sono in nessuna coda al boot: li rende eseguibili
l'ISR, ed è l'unico modo di farli arrivare fuori ordine, visto che lo scheduler
sceglie sempre il più prioritario fra i pronti.

> **La prima stesura aveva un baco che il verde non mostrava.** Le fasi erano a
> tick — H al 2°, F al 4° — e misurando è venuto fuori che al 2° tick E e C non
> si erano ancora bloccati: l'ordine di arrivo vero era `A D H E F C`, quindi F
> finiva **in fondo** e l'inserimento *in mezzo* — il caso che le due enqueue
> vecchie non sanno esprimere, cioè il più interessante — **non veniva mai
> eseguito**. Il test restava verde e verificava una cosa in meno di quella che
> dichiarava. È il motivo per cui le fasi ora guardano lo stato.

Controprova: disattivando il confronto della camminata viene `234615`, che è
l'ordine di arrivo. Le due cifre che si muovono dicono quali casi sono coperti —
H dal 5° posto al 1° (inserimento **in testa**), F dal 6° al 5° (scavalca C,
quindi **in mezzo**), e D E F restano in quest'ordine fra loro (**FIFO fra
pari**). E `sveglia` passa da 1 a 2, cioè dal più prioritario al primo arrivato.

**`test_mutex` — `0 0 1 1 1`.** Due mutex in **un solo programma**, e non due
test, perché il valore sta nel confronto: l'ISR fa la stessa identica mossa nelle
fasi 0 e 2 — rendere eseguibile l'utente più prioritario mentre il mutex è
tenuto — e gli esiti sono opposti. A cambiare è solo la dichiarazione del
ceiling.

- `mtxOK` (ceiling giusto): T diventa `READY` mentre S la tiene, **non lo
  preempta** perché S gira al ceiling, e quando gira trova il mutex **libero**.
  `attOK = 0`;
- `mtxKO` (ceiling dichiarato 5 mentre l'utente più prioritario è a 2): la
  promozione non scatta, U preempta davvero, trova occupato e **si blocca**.
  `attKO = 1`.

`cntU = 1` è la prova negativa che vale di più: U prende il mutex **dopo** essersi
bloccato. Con la risposta sbagliata — riaccodarsi fra gli eseguibili e riprovare —
U tornerebbe subito in esecuzione essendo più prioritario di W, riproverebbe,
fallirebbe, e W non girerebbe mai. **Il test non fallirebbe: non finirebbe**, ed è
la forma che il livelock prende qui dentro.

Controprova sul verso della promozione (`blt r5, r6` invece di `blt r6, r5`,
cioè massimo invece di minimo): `2 1 1 1 1` — `err` sale a 2 e `attOK` diventa 1,
cioè un TCB accodato al mutex **dichiarato bene**, che è l'anomalia di §13.5 che
compare dove non deve mai comparire.

#### Cosa resta aperto: §13.7, e una discussione INTERROTTA A METÀ

> **CHIUSA l'11/09/2026 — vedi §3.36.** Quello che segue è il verbale di com'era
> il problema quando non aveva ancora una risposta, e si legge per capire da dove
> la risposta è venuta. Le «tre uscite, nessuna decisa» adesso sono quattro e una
> è scelta: ceiling classico più `TCB.crit`. §13.7 invece è ancora aperta.

> **⚠ SI RIPRENDE DA QUI.** La sessione è finita nel mezzo di una discussione di
> disegno che non è conclusa, e l'ultima frase dell'utente si è interrotta a
> metà. Il paragrafo «La frase interrotta» qui sotto dice esattamente dove.
> **Niente di questa sottosezione è deciso**, e nessuna delle riscritture
> proposte è stata scritta nella proposta: `mutex_unlock` fa oggi ciò che §13.7
> prescrive alla lettera.

**Il punto di partenza, §13.7.** `mutex_unlock` chiama `request_preempt`, ma da
contesto di task un «percorso di uscita» non c'è: il flag resta armato fino al
**tick successivo**, cioè si ottiene la latenza che §13.7 dice di voler evitare.
Non era un errore il 06/09 — allora l'unico rientro nel kernel era l'ISR — ma da
§3.30 esiste `task_block`, cioè un rientro **sincrono**.

##### Il modello dell'utente, ed è la cosa da tenere

Avevo proposto un `task_yield`. **Bocciato**, e la ragione vale più della
proposta:

> *«Il mutex è una sezione critica simile a disable/enable interrupt. Quando la
> sezione critica è un `di`/`ei`, il task che ne esce continua a detenere la CPU.
> Può perderla solo se preemptato, o se si accoda spontaneamente ad attendere un
> evento o una risorsa.»*

E, di conseguenza, `mutex_unlock` **non è un'occasione per cedere la CPU**:
uscire da una sezione critica non è un atto di scheduling. È solo il momento in
cui una preemption che era stata **sospesa** può scadere.

Prima ancora, l'utente aveva dato il criterio che rende la cosa verificabile
invece che di intenzione:

> *«"Voglio lasciare la CPU" per me significa "sono andato ad accodarmi su
> un'altra coda".»*

Che è, parola per parola, la precondizione già scritta in `task_block`. Ne segue
un criterio che il kernel possiede già e non usa come tale — **l'invariante dei
link**:

| all'uscita dalla CPU | vuol dire | e quindi |
|---|---|---|
| `fwd`/`bwd` **non nulli** | si è accodato lui, e ha detto dove trovarlo | il kernel non deve collocarlo: `task_block` → `scheduler`, la scansione **nuda** |
| `fwd`/`bwd` **nulli** | era `RUNNING`, fuori da ogni lista | qualcuno deve decidere dove va: `sched_preempt`, che mette via l'uscente *e* sceglie |

Che siano due routine diverse smette di essere una scelta di scrittura: è quel
criterio letto dai due lati. Con una precisazione che **spiega §13.5**: «altrove»
deve voler dire *una coda che lo scheduler NON scandisce*. Riaccodarsi alla
propria coda di ready sembra soddisfare il criterio, ma quella è la coda di chi
la CPU la **vuole**: non è andarsene, è richiederla — ed è il livelock.

##### Dove è arrivata l'analisi dell'unlock

Chi può essere eseguibile e più prioritario dell'ex detentore, nell'istante
dell'unlock? Tre popolazioni, e solo una è scoperta:

1. **più prioritario del ceiling** → impossibile: se fosse diventato eseguibile
   durante la sezione critica avrebbe preemptato subito, perché il ceiling non lo
   escludeva;
2. **il primo della coda del mutex** → **già corretto nel codice**, e dipende
   dall'**ordine**: `mutex_unlock` ripristina `TCB.pcb` *prima* del
   `dequeue_testa_nc`, quindi la `blt` di `task_ready` confronta l'affiorante
   con il **nominale** e non col ceiling. Se l'affiorante è più prioritario,
   arma e l'ex detentore verrà preemptato; se è meno prioritario, non arma e
   l'ex detentore **continua a tenere la CPU**. Col ripristino dopo il dequeue il
   primo caso non scatterebbe mai;
3. **la FASCIA fra il ceiling e il nominale** → **è l'unica scoperta.** Un task
   lì dentro, reso eseguibile durante la sezione critica, è stato confrontato da
   `task_ready` con `current` **al ceiling**, non l'ha battuto, e nessuno ha
   armato niente. Sono task che con quella risorsa **non c'entrano**: sono
   quelli che il ceiling ha zittito.

Da cui §13.7 diventa **più piccola e più precisa** di come l'avevo posta: non una
scansione generale «c'è qualcuno che mi batte?», che somiglierebbe a uno yield,
ma

> all'unlock si riapre esattamente la fascia che il ceiling aveva chiuso — dal
> livello del **ceiling** a quello **nominale** — e il mutex conosce entrambi
> gli estremi (`ceiling` e `prio_prec`).

Tre conseguenze: la fascia è **dichiarata**, non congetturata; il costo è
proporzionale a quanto il ceiling ha alzato il detentore, e se non c'era stata
promozione è **vuota**; e dà una **misura** a §13.5 — più si sovradichiara il
ceiling, più larga è la fascia, più task innocenti si zittiscono e più lungo è il
debito da pagare all'unlock.

E `sem_post`/`send_s` **non** hanno una fascia: nessuno è stato zittito, e
`task_ready` confronta contro la priorità vera. Lì il flag è già un testimone
fedele e serve solo un consumatore sincrono. Quindi **non** è una primitiva sola
per tutti e tre, come avevo detto: sono due bisogni diversi.

##### La frase interrotta — (risolta l'11/09: era lo stesso caso del ritrovamento qui sotto, §3.36)

L'utente stava enumerando i casi dell'affiorante dalla coda del mutex e si è
fermato a: *«se il task affiorante dalla…»*. La mia ipotesi di completamento era
il caso «meno prioritario», che è già corretto (punto 2 sopra).

**Il caso che io NON ho considerato, e che potrebbe essere quello vero, è
l'affiorante di priorità PARI all'ex detentore:** la `blt` è stretta, quindi non
preempta e l'ex detentore tiene la CPU — ma la **rotazione fra pari** al tick
successivo gliela toglie lo stesso. Due meccanismi che rispondono in modo
diverso alla stessa situazione.

L'utente ha chiuso con **«questa cosa va chiarita per bene»**.

##### Il ritrovamento collegato, MISURATO: la rotazione contro §13.5

Nasce dalla stessa radice, ed è più grosso di §13.7.

`sched_preempt` ruota fra pari **al livello dell'uscente**, che per un possessore
di mutex è il livello del **ceiling**. I suoi pari a quel livello sono quindi,
per definizione della dichiarazione, **proprio i task che possono chiedere quella
risorsa**.

La dimostrazione di §13.5 poggia su «un eseguibile a priorità maggiore **o
uguale** esclude che tu stia girando»: vero con la sola preemption, che è
stretta; **falso** con la rotazione. Scritta il 06/09, la rotazione è arrivata il
07/09 (§3.30) e nessuna delle due sapeva dell'altra.

**Controprova eseguita:** aggiungendo `call request_preempt` nell'ISR di
`test_mutex` — cioè un'applicazione che chiede la rotazione a ogni tick, uso che
§6 dichiara legittimo — i numeri passano da `0 0 1 1 1` a **`0 1 1 1 1`**:
`attOK` vale 1, cioè un TCB accodato al mutex dichiarato **correttamente**. Il
test normale non lo vede perché nel suo flusso nessuno arma il flag in quella
finestra (`task_ready(T)` non arma proprio perché T non batte S al ceiling).

Ne segue che «un TCB accodato a un mutex è un'anomalia osservabile» ha una
**terza** causa oltre alle due dichiarate in §13.5.

Le uscite discusse, **nessuna decisa**:

1. **ceiling un livello SOPRA l'utente più prioritario.** Non serve che la coda
   al livello del ceiling sia vuota: basta che chi ci sta non sia un utente, e
   con questa dichiarazione non può esserlo. La regola generale che ne esce è
   *«il possessore deve girare strettamente sopra ogni possibile utente»*, e
   allora qualunque cosa lo sposti — preemption o rotazione — è per costruzione
   un non-utente. La rotazione resta possibile ma diventa un costo di **tempo**
   (allunga la sezione critica) e non di correttezza. **Non è
   sovradichiarazione**: la formula classica di ICPP è scritta per un modello
   senza time-slicing fra pari, quindi «un livello sopra» è il valore *esatto*
   per questo scheduler;
2. **chi tiene un mutex non si ruota**, che è §13.2 alla lettera (`cli` non ti fa
   perdere il quanto). Ma richiede che `sched_preempt` sappia che l'uscente tiene
   qualcosa, e quell'informazione non è deducibile — il nominale non è
   memorizzato da nessuna parte, sta in `prio_prec` dentro il mutex. Servirebbe
   **un campo nel TCB**, cioè proprio il costo che §7.4 si compiace di non pagare
   scegliendo il ceiling («Costo nel TCB: nulla»);
3. **togliere la rotazione fra pari.** Restituirebbe §13.5 alla lettera e
   renderebbe inutili sia (1) sia (2). La rotazione serve a far convivere task
   **allo stesso livello che non si bloccano mai**, e in un sistema realtime ben
   dimensionato l'unico che davvero non si blocca mai è l'idle, solo al suo
   livello per costruzione (§4). Prezzo noto: `test_scheduler` misura `cntD`, che
   varrebbe 0 — ma quel test ha due task che non si bloccano mai, che è un
   artefatto del test, non un requisito. §3.30 la rotazione l'ha scelta di
   proposito («il tick è il quanto»), quindi disfarla è una decisione, non una
   deduzione.

##### Cosa NON è stato scritto, e perché

Avevo proposto una riscrittura di **§2** della proposta — dove la tabella a tre
righe dice ancora «l'ha ceduta lui → ha rinunciato al turno», cioè descrive uno
yield che non esiste. L'utente ha visto la bozza e la discussione è proseguita:
**non è stata scritta**, ed è comunque da rifare, perché partiva dallo yield
invece che dalla terna «si accoda altrove / è preemptato / la rotazione, che non
è né l'una né l'altra».

Restano quindi da scrivere, quando la discussione si chiude: §2 (il criterio dei
link e la terna), §13.5 (la terza causa della coda non vuota) e §13.7 (la fascia
al posto della scansione generale).

#### Cosa è stato scritto

| Dove | Cosa |
|---|---|
| [`coda.vasm`](../generic/coda/impl/src/coda.vasm) | `coda_peek`, e il riquadro sul perché un argomento solo |
| [`coda_api.vinc`](../generic/coda/interface/coda/coda_api.vinc) | il contratto di `coda_peek`, e che sta fuori dai due assi |
| [`test_coda.vasm`](../generic/test/test_coda.vasm) | sezioni (12) e (13): la camminata, e che tre `peek` di fila non sono tre `dequeue` |
| [`scheduler.vasm`](../rtos/scheduler/impl/src/scheduler.vasm) | `prio_pcb` |
| `rtos/servizi/semaforo/` **(nuova)** | `semaforo.vinc` + `semaforo.vasm` |
| `rtos/servizi/mutex/` **(nuova)** | `mutex.vinc` + `mutex.vasm` |
| `rtos/test/` | `test_semaforo.vasm`, `test_mutex.vasm` |
| `rtos/servizi/CMakeLists.txt` | i tre servizi, e la tabella di **cosa stai aspettando** che li distingue |
| `proposta-kernel-realtime.md` | §13 da SPECIFICATI a IMPLEMENTATI; §13.1 (due correzioni + «sotto non c'è rete»), §13.3 (struttura vera + dove si dichiara il ceiling + `prio_pcb`), §13.5 (niente direzione conservativa), §13.6 (`coda_peek`), §13.7 (il buco), §13.8 riscritta come **deduzione** con i tre corollari, §13.9 nuova; §8.1 (l'argomento vero), §7.5 (emendata), §1 |

**La tabella in fondo a §3.26 è chiusa**, tranne l'ultima riga — estendere gli
`_api` a `pool`, `timeout`, `messaggio`, `hal` — che non riguarda §13.

---

### 3.34 La traccia temporale: chi gira, e da quando a quando (07/09/2026, nona parte)

Il debito che §3.29 aveva aperto — «senza uno strumento che dica *chi gira e da
quando a quando* ogni numero resta un'osservazione invece di una misura» — è
chiuso. **`tools/traccia.py`**, cartella nuova al primo livello decisa
dall'utente, produce una pagina HTML autosufficiente:

```bash
python3 tools/traccia.py out/vasm/test_gestore.vx      # -> out/traccia.html
```

`out/` è già ignorato da git, quindi la pagina non sporca il working tree; il
modello del disegno è `tools/traccia.template.html`, che si modifica senza
toccare lo script.

#### Il proprietario non è nel trace: si deduce

Il simulatore stampa `pc` e ciclo per ogni istruzione, e basta — `current` vive
in memoria. La regola è una sola: **il proprietario cambia solo quando il `pc`
entra nel corpo di un task o dell'ISR**, e tutto ciò che sta in mezzo è kernel a
carico di chi girava. È corretto perché una libreria si esegue sempre per conto
di chi l'ha chiamata, ma resta un'inferenza, e la pagina lo dice.

Due cose hanno richiesto più cura di quanto sembrasse:

- **`nm` non basta.** Pubblica i soli `.global`, e con quelli `ctx_save` — che
  globale non è — finisce contata dentro `_trap_entry`, e i rami dello scheduler
  dentro il simbolo che li precede. Ogni modulo si ri-assembla con
  `--emit-expanded` per averne le etichette, e la base di ciascuno si ricava da
  un simbolo globale presente sia nel listato sia nel programma linkato;
- **dove finisce un corpo.** Deve fermarsi al primo simbolo **pubblicato** che lo
  segue, non a fine modulo. Il caso che lo dimostra è `gestore_tick`: sta nello
  stesso modulo di `gestore_task` ma gira **dentro l'ISR**, e attribuendolo al
  gestore spostava 1.800 cicli dalla corsia sbagliata. Le etichette interne
  (`loopI`, `gestore_drena`, `isr_manda`) sono locali e restano dentro il corpo,
  che è esattamente la distinzione che serve.

L'applicazione, a differenza delle librerie, non si può indovinare: **ogni test
definisce `main` a indice 0**, quindi prenderle tutte darebbe a tutte la stessa
base. Si ricava dal nome del `.vx`, che è l'unico legame affidabile.

#### Cosa si vede, che i contatori non dicevano

| | `test_gestore` |
|---|---|
| idle | 27.187 (54,7%) |
| gestore | 11.268 (22,7%) |
| ISR | 5.609 (11,3%) |
| A | 4.291 (8,6%) |
| boot | 1.375 (2,8%) |

- **il cambio di contesto è il 9,7%**: `ctx_save` + `ctx_restore` fanno 4.806
  cicli su 49.730, in 29 commutazioni, cioè **~166 cicli l'una**. §3.28 ne aveva
  stimati ~120 per giustificare gli otto livelli di priorità — l'ordine di
  grandezza regge, la stima era ottimista di un terzo;
- **la domanda di §3.29 ha una risposta**: l'idle non «conta il doppio», ha
  **fasce molto più lunghe**. Il suo contatore gira in tratti da migliaia di
  cicli senza una sola chiamata, mentre gli altri task alternano poche istruzioni
  proprie a lunghi tratti di kernel. Non è il kernel che si prende tempo che non
  gli spetta;
- **il gestore spende il 97% del proprio tempo fuori dal proprio corpo** (353
  cicli in `gestore_task`, 11.268 in tutto): il suo ciclo sono quattro chiamate,
  e il lavoro sta dentro quelle.

Funziona anche su `test_catena` (96 fasce, i tre anelli a 13,8/13,4/11,7%) e su
`test_block` (50 fasce, l'ISR al 46,6% perché il programma è corto e il boot pesa).

---

### 3.33 Il gestore dei timeout è un task, e §9.2 aveva un test che non poteva funzionare (07/09/2026, ottava parte)

**26 test. §3.28 è chiusa**: la simulazione completa gira. `test_gestore` è il
primo programma in cui **§8, §9 e §10 girano insieme sotto lo scheduler vero** —
mailbox, timeout e pool erano implementati da giorni e non si erano mai
incontrati nello stesso binario.

#### Il taglio: `generic/timeout` non poteva ospitare il task

La decisione l'ha presa l'utente sulla base di due regole già scritte, e la
seconda è quella che ha aperto una cartella nuova:

- `generic/CMakeLists.txt` dice che **un modulo che nomina un simbolo di
  `rtos/` non è generic**. Il ciclo del gestore chiama `receive` e `send`:
  fuori. Il commento in `timeout.vasm` che si aspettava lì la scansione era
  anteriore alla ristrutturazione di §3.24, ed è la regola nuova a vincere;
- `rtos/servizi/` dice che **un servizio si riconosce da una firma sola: chiama
  `task_ready` e `task_block`**. Il gestore non li chiama — non blocca nessuno,
  si blocca lui sulla propria mailbox come un task qualunque. È il primo **task
  di sistema** del progetto, una terza specie dopo il kernel e i servizi, e ha
  una cartella sua: `rtos/gestore_timeout/`.

| dove | cosa |
|---|---|
| `generic/timeout` | il vettore e la sua disciplina: `timeout_arm`, `timeout_cancel`, **`timeout_scaduto`** |
| `rtos/gestore_timeout` | il task: `gestore_init`, `gestore_task`, `gestore_tick` |

La riga di confine è `timeout_scaduto`, e **formatta senza mandare**. Non è un
compromesso: la transizione `ARMED → FREE` deve stare nella stessa sezione
critica in cui si decide di consegnare — restituire i campi e lasciare al
chiamante la liberazione riaprirebbe il bug del riciclo dell'handle (§9.5) — e
formattare non porta dentro niente di `rtos/`, perché il formato del payload è
`generic/messaggio`.

#### Il buffer prima della scadenza, e un invariante che smette di essere un ramo

`gestore_task` fa `buf_alloc` **prima** di chiedere una scadenza. Così una
scadenza non può essere consumata senza un posto dove metterla, e il «pool vuoto
⇒ il timeout arriva tardi invece di non arrivare» di §9.2 diventa **strutturale**
invece che un ramo da ricordarsi: senza buffer non si arriva nemmeno a guardare
il vettore. Il prezzo è un `alloc` + `free` per ogni tick in cui non scade
niente — due O(1) su una free-list — e vale la pena pagarlo per non avere un
ordine da rispettare a memoria.

#### `count == 0` era il test sbagliato, e sbagliava nel caso normale

§9.2 diceva che l'ISR manda il messaggio di tick «solo se la mailbox del gestore
è vuota — lì arrivano solo tick, quindi `count == 0` è il test». **Non poteva
funzionare**: il caso normale è il gestore **bloccato** nella propria mailbox in
attesa del tick, e allora `count` vale **−1** per la convenzione col segno di
§8.2. Con `count == 0` l'ISR non manda proprio quando c'è un task da svegliare,
cioè sempre.

Non è un ragionamento, è misurato — il test scritto con la versione di §9.2 dà:

```
cntA = 0    cntErr = 0    cntI = 4432
```

Nessuna scadenza consegnata mai, e l'idle che si prende tutta la macchina. Il
test giusto è `count > 0` per coalescare, che comprende i due casi in cui si
manda: mailbox vuota (0) e ricevente in attesa (−1). §9.2 della proposta è
corretta, con il numero accanto.

Nota onesta sulla copertura: il ramo che **coalesce** non viene mai eseguito in
questo test (verificato sulla traccia — le 14 istruzioni di `gestore_tick` girano
tutte 12 volte), perché con il periodo scelto il gestore drena sempre entro il
tick. Quello che il test dimostra è che la condizione *opposta* è quella giusta,
non che la coalescenza funzioni: per quella servirebbe un tick più fitto del giro
del gestore, cioè la saturazione di §3.32 provocata apposta.

#### `5 0 2609`, e un valore atteso più forte degli altri

`cntA = 5` è **derivabile per intero e non dipende dal periodo del timer**: A
arma a +2 tick e riarma dentro lo stesso tick in cui si sveglia, quindi le
scadenze cadono ai tick 2, 4, 6, 8 e 10; quella armata al 10 scadrebbe al 12, ma
è il tick in cui l'ISR ferma tutto. Si conta sull'**orologio logico** (`tmo_now`,
il contatore assoluto), non sui cicli — verificato a 2000, 4000 e 8000 cicli di
periodo: `cntA` resta 5 e si muove solo `cntI`. È una proprietà più forte di
quella di §3.32, dove i tre numeri della catena dipendevano dal drenaggio.

`cntErr = 0` è l'altro numero che non dipende dai cicli, e i tre modi in cui può
salire sono tre difetti diversi: `timeout_arm` rifiutato, `clientTag`
disallineato (§9.4), `buf_free` che non riprende il buffer. E `cntA` è anche la
prova che **non ci sia un leak**: la classe da 16 ha dieci blocchi, quindi un
giro che ne perdesse uno per volta si fermerebbe dopo dieci scadenze.

Sotto i 2000 cicli di periodo `cntA` crolla a 0: è la stessa saturazione di
§3.32, con un giro più lungo perché ci sono dentro anche il pool e una
preemption in più.

---

### 3.32 La catena A→B→C, e la saturazione che si è vista misurando (07/09/2026, settima parte)

**25 test.** [`rtos/test/test_catena.vasm`](../rtos/test/test_catena.vasm) è il
**primo dei due passi** che restavano del test di §3.28: A svegliato dall'ISR
manda a B, B manda a C, C conta. Resta il secondo, il gestore dei timeout come
task, che è l'unico a tirare dentro anche il pool (§10).

#### Tre percorsi di kernel che erano scritti e mai eseguiti

Non è «test_block con un task in più»: ognuno dei tre è codice che nessun
programma aveva mai fatto girare, e il conto sulla traccia lo dice senza
interpretazioni — **9 `task_ready`, 3 `request_preempt`, 6 `send_s`**:

1. **`send_s`, la send da contesto di task.** In `test_block` manda solo l'ISR,
   con `IE` già a 0, che è il caso per cui esiste la raw (§8.5). Il wrapper — la
   `.proc` di §3.5 — non era mai stato eseguito da nessuno: adesso lo è sei
   volte, e i due usi convivono nello stesso programma. La coppia raw/`_s` non è
   una scelta fra varianti, sono due **contesti di chiamata**;
2. **il ramo di `task_ready` che NON preempta.** In `test_block` il risveglio
   vinceva sempre (2 batte il 7 dell'idle), quindi la `blt` sui puntatori ai PCB
   prendeva sempre lo stesso ramo. Qui A sveglia B, che gli sta **sotto**: sei
   risvegli su nove non chiedono niente. È la preemption differita di §6 vista
   dal lato in cui non succede nulla — svegliare un meno prioritario **non è un
   evento di scheduling**, è un enqueue;
3. **la scansione di `task_block` che attraversa livelli popolati** (pcb1 → pcb2
   → pcb3), invece di trovare solo l'idle sotto al bloccato. Tre TCB dormono
   contemporaneamente, ognuno nella propria mailbox a `count = -1`: è anche la
   prima volta che l'ipotesi «una mailbox per task» (§8.1) regge in tre copie.

#### `3 3 3`, e perché tre contatori invece di uno

I primi tre numeri sono **derivabili e devono venire uguali**: i tick pari prima
dell'8 sono 2, 4, 6, quindi tre messaggi entrano nella catena e tre devono
uscirne. Tenerli separati per salto non è zelo — se un anello ne perdesse uno,
i tre numeri direbbero **dove**, mentre un contatore solo direbbe soltanto che
qualcosa non torna. Ed è servito subito: la prima esecuzione ha dato `3 1 0 0`.

#### La saturazione: il primo numero sbagliato non era nel codice

`3 1 0 0` con il periodo di 500 cicli ereditato da `test_block`. Non c'era
nessun bug: **un giro di catena costa quattro commutazioni** (tre volontarie e
una preemption, ~120 cicli l'una solo di `ctx_save`/`ctx_restore`), e se il tick
torna prima che la catena sia drenata riparte A — che è il più prioritario — e
la coda della catena non gira mai. L'idle contava 0 non perché il sistema fosse
occupato a fare qualcosa di utile, ma perché non arrivava mai in fondo.

Misurando la soglia si vede la forma del fenomeno:

| periodo | `cntA cntB cntC cntI` | |
|---|---|---|
| 500 | `3 1 0 0` | il sistema non drena mai |
| 800 | `3 3 0 55` | i due anelli alti tengono, **l'ultimo muore di fame** |
| 1000 | `3 3 3 75` | passa, con margine nullo |
| **2000** | **`3 3 3 778`** | scelto: margine per qualche decina di cicli di kernel |

L'`800` è la riga che vale: è la **saturazione di un sistema a priorità statiche
in miniatura**. Nessuna asserzione scatta, nessun numero è «sbagliato», nessun
errore viene segnalato — semplicemente il carico non ci sta nel periodo, e a
pagare è l'ultimo della catena. È lo stesso motivo per cui `cntI` non è un
controllo di sanità ma **la statistica di CPU libera** (§3.29): un idle a zero
non dice «il sistema lavora», dice «guarda meglio».

Il periodo è quindi un **parametro del test dichiarato**, con la soglia misurata
accanto, e non un numero di comodo trovato finché non diventava verde.

---

### 3.31 Un task che dorme davvero (07/09/2026, sesta parte)

**24 test.** `test_block` è il primo uso *vero* di `task_block` e
`hal_ctx_block`, che fino a §3.30 erano codice scritto e mai eseguito.

#### Perché questo prima della simulazione completa

Il test di §3.28 metterebbe in gioco cinque cose nuove insieme, e il pezzo più
rischioso è il **frame costruito a mano**: se è sbagliato non dà un test rosso,
dà corruzione che si manifesta altrove. Qui c'è un percorso solo — due task, una
mailbox, l'ISR del tick che consegna — e se rompe si sa dove guardare.

Verifica tre cose che nessun test toccava:

1. **il frame sincrono regge**: W riprende esattamente dentro `receive`, e con
   `IE = 1`. Se la `psw` scritta da `hal_ctx_block` fosse sbagliata il sistema si
   fermerebbe al primo blocco — quindi anche l'argomento di §12.5 («si torna a un
   task, quindi IE:1 senza leggerla») è ora verificato e non solo sostenuto;
2. **il risveglio attraversa tutto il kernel**: la `send` sfila il TCB, chiama
   `task_ready`, che accoda al livello 2 e chiede la preemption perché 2 batte il
   7 dell'idle. Nessuno di quei passi è saltabile;
3. **la CPU libera è misurata sul serio**: `cntI` conta mentre W **dorme**. In
   `test_scheduler` l'idle girava solo perché il sistema non era ancora partito —
   qui gira perché non c'è niente da fare, che è la forma vera della statistica.

È anche il primo programma che compone le due metà, `lib_messaggi` per la
mailbox e `lib_kernel` per lo scheduler: la ragione per cui in §3.30
`lib_messaggi` ha smesso di nominare `lib_kernel` nel proprio `LINK`.

#### `3 28`, e un valore atteso derivato male

Il primo numero è derivabile, e la prima stesura lo aveva derivato **sbagliato**:
«8 tick / 2 = 4». L'ISR manda sui tick **pari** e l'8° non manda, ferma — quindi
i tick utili sono 2, 4, 6, cioè **tre**. Il codice era giusto e il conto no, ed è
annotato nel test perché è il modo esatto in cui un valore atteso si sbaglia:
contando un bordo che non c'è. Aggiustare il codice per farlo tornare a 4 avrebbe
rotto una cosa che funzionava.

---

### 3.30 Il blocco, la rotazione, e il criterio che non serviva (07/09/2026, quinta parte)

**Lo scheduler è completo**: priorità, slot, rotazione fra pari e blocco
volontario. `ctest` 23/23. Erano i punti 1, 2 e 4 dei quattro elencati come
percorso critico; resta il 3, la simulazione.

#### `hal_ctx_block`, e la separazione che l'ha resa possibile

§8.8 eseguita: l'HAL scrive `epc` (l'indirizzo di ripresa) ed `epsw` (`PSW_IE`),
poi chiama **`ctx_save` invariata**, che li rilegge come farebbe in una trap. Il
frame è quello di sempre, prodotto da una sorgente diversa.

Ma `task_block` non era scrivibile così com'era il kernel, e la ragione è
istruttiva: **`scheduler` metteva sempre l'uscente nello slot**. Un task che si
blocca è già accodato dove verrà ritrovato, quindi lo slot lo avrebbe rimesso in
gioco — due posizioni per una coppia di link sola (§7.5). La correzione è §2
alla lettera, «dove va l'uscente lo decide chi lo toglie dalla CPU»: lo scheduler
non lo tocca più, e le tre destinazioni le conosce solo chi provoca l'uscita.

Effetto collaterale **misurato**, non previsto: `74 29 59` → `76 30 59`. Il TCB
uscente era già in `r1` e lo scheduler lo rileggeva da `current` con `li`+`lw` —
lo stesso difetto che §12.3 aveva tolto per il dispatcher, ricomparso un piano
sopra.

`task_ready` è il primo posto in cui la convenzione di §4 e l'invariante di §7.3
pagano **insieme**: il confronto di priorità è una `blt` sui puntatori ai PCB,
senza indici da convertire e senza leggere nessun numero.

#### La rotazione: il criterio emerge dalla scansione

La distinzione di §2 sembrava richiedere di sapere *chi* avesse fatto rientrare
nel kernel — il tick o un evento. **Non serve**:

> La scansione trova qualcuno **prima** del livello dell'uscente → esiste un più
> prioritario, era preemption vera, l'uscente non ha consumato il turno: **slot**.
> La scansione arriva al suo livello senza trovare nessuno sopra → l'uscente è
> ancora il più prioritario, quindi nessuno gli ha tolto niente: era il tick,
> cioè fine turno. **In fondo alla sua coda**, e tocca a un pari.

È SCHED_RR, e senza quanto perché **il tick è il quanto**. Nasce `sched_preempt`
(politica completa per un'uscita involontaria); `scheduler` resta la scansione
nuda per chi non ha un uscente da mettere via, cioè `task_block`.

**Un ramo che vale il 34%**: se al proprio livello non c'è nessun pari, l'uscente
non si muove. Ruotare con nessuno sono due `call` sprecate — la coda
restituirebbe lui stesso dopo averlo accodato — ed è il caso *normale* in un
sistema ben dimensionato. Senza quel test di vacuità i contatori scendevano del
34%.

#### `lib_messaggi` non sceglie più il kernel

Il primo link dopo `task_block` è fallito con `duplicate global`, ed era il
momento previsto da giorni nel `CMakeLists` della mailbox: `test_mailbox` **si
fingeva il kernel** e ora il kernel esiste. La soluzione non è stata far usare al
test il kernel vero — resta un test **unitario**, il cui stub di `task_block`
esegue la send che lo sveglia invece di commutare — ma togliere `lib_kernel` dal
`LINK` di `lib_messaggi`: una libreria non deve scegliere quale implementazione
del kernel useranno i suoi clienti. I due simboli restano `.extern`, cioè il
contratto, e chi costruisce il programma decide chi li fornisce. `test_mailbox`
ora si finge il kernel **fino in fondo**: definisce anche `current`.

#### Il test cresce a quattro numeri, e uno vale più degli altri

`74 5 10 59` — H, M, **D**, idle. D è **pari di M**, ed è ciò che rende la
rotazione verificata invece che scritta:

> Senza rotazione `cntD` varrebbe **esattamente zero**: M finirebbe nello slot a
> ogni tick, lo slot batte la coda, e D non uscirebbe mai.

Che `cntD != cntM` non è un difetto: i due turni non sono simmetrici — il primo
si porta dietro l'attivazione di entrambi — e ciò che il test asserisce è che il
**testimone sia passato**, non che le fette siano uguali.

---

### 3.29 Lo scheduler a priorità gira, e il vecchio test è stato ritirato (07/09/2026, quarta parte)

Il modello a PCB non è più fermo in un tree che non compila: **gira, ed è
verificato**. `ctest` 23/23.

#### Il test vecchio si ritira, non si adatta

`scheduler_demo` verificava che due task si **alternassero** a ogni tick, e i
suoi numeri misuravano quello. Con le priorità quel comportamento non esiste
più: al tick l'uscente va nello slot e lo slot batte la coda. Adattarlo avrebbe
conservato un'aspettativa scritta per un'altra politica — e **un test che
sopravvive al modello che verificava passa, e dice il falso**. Al suo posto
[`rtos/test/test_scheduler.vasm`](../rtos/test/test_scheduler.vasm); la cartella
`rtos/demo/` resta vuota, con dentro il perché.

#### Cosa verifica, e l'unico numero che vale davvero

Il sistema **parte con la sola idle**, e i task entrano a scaglioni: M al 2°
tick, H al 4°. Tre fasi disgiunte, e ogni ingresso è una preemption osservabile
invece che dedotta da contatori che crescono insieme.

| | livello | quando conta |
|---|---|---|
| I (idle) | 7 | i primi due tick |
| M | 2 | dal 2° al 4°, poi si ferma: preemptato, resta nello slot di `pcb2` |
| H | 1 | dal 4° in poi |

**Perché il sistema parte dalla sola idle, ed è una correzione dell'utente.** La
prima stesura aveva i tre task eseguibili da subito e dichiarava «`cntI` deve
valere esattamente 0» come se fosse la proprietà più forte. Era una
**tautologia**: se nessuno si blocca e c'è sempre qualcuno di eseguibile, l'idle
non gira per costruzione e il suo contatore non misura niente. Ma quel contatore
non è un controllo di sanità — **è la statistica di CPU libera**, la misura che
in un RTOS dice se il sistema regge. Un idle che non gira non la può dare, e in
più nasconde qualunque errore nell'ultimo livello della scansione.

Nella simulazione completa (§3.28) la CPU libera verrà dai task che **dormono**,
che è la forma vera; qui la si ottiene facendo partire il sistema vuoto.

`76 30 59`: i tre numeri dipendono dai cicli, e ciò che il test asserisce è che
siano **tutti e tre > 0 e prodotti in fasi disgiunte**. Il loro rapporto — l'idle
conta ~30 per tick contro i ~15-18 degli altri — era **non spiegato**, ed è da
guardare con la traccia temporale.

> **Risposto il 07/09/2026 (§3.34).** Non conta il doppio perché gira il doppio:
> ha **fasce molto più lunghe**. Il suo contatore avanza in tratti da migliaia di
> cicli senza una sola chiamata, mentre gli altri task alternano poche istruzioni
> proprie a lunghi tratti di kernel — che è la stessa cosa detta al contrario:
> l'idle è l'unico task il cui lavoro *sia* il proprio corpo.

#### Due bug trovati eseguendo, che il ragionamento non aveva visto

1. **`sched_init` è non-foglia, e il boot la chiamava senza stack.** La vecchia
   demo se la cavava perché la sua unica chiamata di boot (`coda_init`) è foglia;
   con `r14 = 0` il prologo scrive a `-4`. Lo stack va armato **per primo** in
   `main`, e può essere quello del task che partirà a freddo.
2. **La scansione teneva il PCB corrente in `r5`, che `dequeue_testa` sporca** —
   lo dichiara `coda_api.vinc`. Dalla seconda iterazione avanzava da un indirizzo
   spazzatura. Ora sta in `r7`. È un bug che **non si vede con un livello solo**:
   finché in cima c'è un preemptato lo scan si ferma al primo giro, e il test
   dava `0 392 0` — H mai eseguito — senza nessun errore.

Il secondo è la ragione per cui il primo strato del test valeva la pena adesso e
non dopo: nessuna rilettura del codice l'avrebbe trovato.

#### Invarianti mosse, entrambe spiegate

- `include`: `16 12 …` → **`20 16 …`**. Il test non è cambiato: stampa gli offset
  che l'assembler produce, e i due numeri nuovi sono la prova che li prende dalla
  dichiarazione del TCB (cresciuto di `pcb`) e non da una copia;
- `scheduler_demo` sparisce dalle invarianti, `scheduler` la sostituisce — e il
  suo `vasm_check` sta in `rtos/test/`, accanto al programma che lo produce.

---

### 3.28 Il dispatcher prende il TCB in input, e 0 è la priorità più alta (07/09/2026, terza parte)

**Primo passo verso §3/§4**, scelto perché è l'unico che *non* dipende dalle
priorità: si può verificare con i `.vx` prima che il modello a livelli li faccia
cambiare comunque.

#### `dispatcher(TCB)` — §12.3, e il debito era scritto nel codice

`scheduler` ora **restituisce** un TCB in `r1` e non scrive più né `current` né
lo stato: il commit è del **dispatcher**, che è l'unico punto da cui un task
entra in esecuzione e quindi l'unico che possa dichiararlo. `sched_isr_exit`
mette l'uscente in `r1` **prima** di consultare il flag, e quel `mov` è la forma
esatta di «senza preemption riprende chi girava»: se nessuno sceglie, non è lo
scheduler a doverlo dire.

Il guadagno non è estetico. Finché il TCB passava per `current` — scritto dalla
politica e riletto due istruzioni dopo dal meccanismo — il confine era una
convenzione, e **il terzo ritorno da un'ISR di §12.4 non era nemmeno
esprimibile**: non c'era un posto dove mettere il TCB.

#### I numeri si sono mossi, ed è la prima volta in questa serie

`scheduler_demo`: **97/64 → 98/65**, cioè i due task fanno un'iterazione in più
a testa. Non è un cambio di politica (resta round-robin), è di **costo**, e va
letto sapendo che i due numeri misurano ciò che il kernel *non* si prende. Col
modello di costo della macchina (memoria 4 cicli, ALU 1):

| percorso | cicli | perché |
|---|---|---|
| **con** preemption | **−3** | lo scheduler perde due `sw` e due `li` (non scrive più `current` né lo stato) e guadagna un `mov`; il dispatcher ne aggiunge 5; `sched_isr_exit` 1 |
| **senza** preemption | **+6** | il commit nel dispatcher è idempotente e si paga lo stesso |

Il `+6` è il prezzo dell'invariante «`current` lo scrive **solo** il
dispatcher», su un percorso in cui non succede niente: **la latenza che conta è
l'altra**, ed è migliorata. Le istruzioni totali salgono (1787 → 1794) perché
sono le iterazioni in più dei task — lavoro utile, non kernel.

Il numero nuovo sta nel `CMakeLists.txt` col conto accanto, non solo aggiornato.
`test_mailbox.vx` cambia anche lui e non è un mistero: `lib_messaggi` linka
`lib_kernel`, quindi il codice dello scheduler finisce nel suo binario.

#### La numerazione delle priorità: **0 è la più alta** — decisa dall'utente

«Come farebbe uno scheduler realtime serio». È anche ciò che rende la scansione
di §4 un incremento e «più prioritario» una `blt`. Scritta in §4 della proposta,
e ne discende il **verso** che mancava all'invariante di §7.3: la tabella dei PCB
parte dal livello 0, quindi l'indirizzo cresce al calare della priorità e
`blt pcb_a, pcb_b` è letteralmente «`a` è più prioritario di `b`» — un confronto
fra `TCB.pcb`, senza leggere nessun numero.

**§13.5 è stata riscritta**, e non era cosmesi: ragionava con numeri in cui 10
batteva 7, cioè con la convenzione opposta a quella appena decisa. Ora `T` sta a
2, `S` a 4, il ceiling mal dichiarato è 5, e la promozione al ceiling è un
**minimo** invece che un massimo. La dimostrazione usa le parole («almeno
prioritario quanto») invece dei simboli, perché il verso dei confronti è
esattamente la cosa che si sbaglia rileggendo.

#### Il modello a PCB è scritto, e ha rivelato cosa manca

Nel working tree (**non committato: la demo non linka**, vedi in fondo) ci sono
già `TCB` a 20 byte con `pcb`, `PCB` con la `TESTA` **annidata**, `N_LIVELLI 8`,
`PREEMPTED`, la tabella degli otto PCB scritti **uno per uno**, `sched_init` e la
scansione di §4 con lo slot che batte la coda.

Otto livelli, e il numero è una scelta di **costo**: la scansione deve restare
lineare perché **nell'ISA non c'è nessuna istruzione di conteggio bit**
(verificato sul repertorio), quindi una bitmap dei livelli non vuoti vorrebbe
comunque un loop di shift senza diventare O(1), più l'onere di tenerla coerente.
Un livello vuoto costa 12 cicli, quindi il caso peggiore è 8 × 12 = 96 — sotto il
costo di un context switch (~120). A 32 livelli lo dominerebbe di tre volte.

#### Il round-robin fra pari, e un errore mio da non ripetere

Scrivendo la scansione ho concluso che il modello «non prevede» la rotazione fra
task di pari priorità, perché al tick l'uscente va nello slot e lo slot batte la
coda: due pari non si alternerebbero mai. **La conclusione era sbagliata**, e
l'utente l'ha corretta: SCHED_RR *è* uno scheduler a priorità statiche, la
rotazione fra pari non è un'alternativa al modello ma una sua parte, e serve
contro lo stallo. Non era «il modello non lo prevede»: era che **manca**.

Ne segue che §2 fa la distinzione a metà. «Lasciata controvoglia» contiene due
casi con destini opposti:

| il task… | finisce | perché |
|---|---|---|
| **preemptato da uno più prioritario** | `PCB.preemptato` | non ha consumato il turno: non deve pagarlo |
| ha **esaurito il turno** | in fondo a `PCB.coda` | il turno l'ha avuto: tocca a un pari |
| ha **ceduto** o si è **bloccato** | coda / altrove | come già scritto |

**E non serve un quanto**, che è la seconda correzione dell'utente dopo che
avevo proposto un contatore nel TCB: **il tick È il quanto**. Il timer scandisce
già il tempo, quindi non serve un campo per misurare ciò che il periodo del timer
misura da sé. La distinzione diventa una domanda su **chi** ha fatto rientrare nel
kernel — il tick (turno finito, in coda) o un altro evento che ha svegliato
qualcuno di più prioritario (interrotto senza colpa, nello slot). Nessuna
struttura nuova, e §2 resta com'è scritta.

#### Il test: la simulazione, e perché il vecchio era un giocattolo

`97 64` verificava che due contatori arrivassero a due numeri: di uno scheduler
non dimostra niente, e i numeri erano **fotografati**, non derivati. Lo scenario
deciso dall'utente:

- un **idle** che incrementa un contatore e non rilascia mai la CPU;
- **A** e **B** alla massima priorità *applicativa*: A arma un timeout e ogni 10
  scadenze manda un messaggio a B; B ne manda uno a C;
- **C**, priorità immediatamente inferiore, conta i messaggi ricevuti.

Il pregio non è avere più task: è che **il valore atteso diventa derivabile**.
Con un tick ogni N cicli e M tick di simulazione, il contatore di C dev'essere
M/10 — si calcola prima di eseguire. Copre quattro proprietà distinte: la
precedenza per priorità (C gira solo quando A e B dormono); la terminazione della
scansione (l'idle gira solo se nessun altro può, e il suo contatore **misura la
CPU non usata**, quindi un valore troppo basso dice che il kernel si prende tempo
che non gli spetta); lo slot di §2 (C preemptato riprende da dove stava); e la
catena timeout → mailbox → mailbox, cioè §8, §9 e §10 insieme sotto lo scheduler
vero — cosa che non è mai girata nello stesso programma.

Due aggiustamenti annotati: **A e B non possono stare al livello 0**, dove sta il
gestore dei timeout (§9.2, che ci sta per non far dipendere la latenza da quanti
timeout sono armati) — quindi 0 al gestore, 1 ad A e B, 2 a C, 7 all'idle. E
**manca la rotazione**: A e B sono pari ma non competono mai, si passano il
testimone e dormono. Servirebbe un **D** accanto a C, anche lui a contare: se il
tick manda l'uscente in fondo alla coda, i due contatori devono venire quasi
uguali, e quel *quasi* è la proprietà.

#### Il blocco vero: `task_block` non esiste

Il test si regge tutto sul blocco — A che aspetta una scadenza, B e C un
messaggio — e **`task_ready`/`task_block` esistono solo come stub dentro
`test_mailbox.vasm`**. È il debito di §8.7, ed è lo stesso schema di ieri: il
mutex non era scrivibile perché mancavano le priorità, il test non lo è perché
manca il blocco.

**Il disegno è stato deciso e scritto in §8.8 della proposta** (sezione nuova):
`hal_ctx_block(r1 = indirizzo di ripresa) -> r1 = contesto`, che salva i registri
come `ctx_save` ma scrive `epc` = l'indirizzo ricevuto e `psw` = `PSW_IE`. I due
sostituti non sono espedienti — senza istruzione interrotta l'`epc` **è** un
indirizzo di ritorno, e `IE:1` discende da §12.5 perché si torna a un task. E
l'indirizzo di ripresa è un **argomento**, non un dedotto: fra `receive`,
`task_block` e l'HAL ci sono due livelli di `r15` e quello corrente al push è
quello sbagliato.

#### Cosa resta prima di §3/§4

- **quanti livelli**: non è deciso e non è deducibile. È il primo numero del
  sistema, dimensiona il vettore statico dei PCB ed è il caso peggiore della
  scansione. Vincoli noti: il gestore dei timeout a 0 (§9.2), l'idle in fondo;
- **il PCB con la `TESTA` annidata**, che §3 lasciava «da valutare» e che
  §3.27 rende scrivibile: `.field coda TESTA.size`, l'idioma di `LINK`. In più,
  con la testa a offset 0 vale `&pcb == &pcb.coda`, quindi il PCB si passa
  **direttamente** a `enqueue_coda` — il contratto di §7.5 un livello sopra, e
  una `lw` in meno per ogni livello scandito.

---

### 3.27 I link in un posto solo, e il campo che prende il nome del proprietario (07/09/2026, seconda parte)

**Sessione di sola discussione, finita in codice.** Nessuna decisione di kernel:
si è chiuso il punto lasciato aperto da §3.26 — il terzo campo di `TESTA`
nominato dal proprietario — e nel farlo è emerso un difetto più grosso di quello
che si stava discutendo. `ctest` 23/23, i sei `.vx` **identici byte per byte**.

#### La domanda da cui è partita, e la risposta sbagliata

L'utente ha chiesto perché `coda.vasm` non offra `incrementa`/`decrementa`/`testa`
sul contatore, così che chi ne ha una convenzione propria non debba conoscere la
struttura della testa. Non era mai stato deciso — anzi §8.3 dice l'opposto, «la
contabilità è del chiamante».

La risposta è no, e la ragione non è quella che sembra. `coda.vasm` **l'aritmetica
la fa**: `addi 1`, `addi -1`, e `beq r5, r0` per la vacuità. Ma quel test è
corretto solo perché sulle sue teste `count >= 0` — e i due clienti che vorrebbero
l'accessor sono precisamente quelli per cui è falso: sulla mailbox `count == -1`
con la lista **non** vuota, sul semaforo `count == 5` con la lista **vuota**,
perché quelle risorse non sono nodi (§13.1). Un `count_zero` esportato sarebbe
sbagliato in entrambi i punti in cui verrebbe chiamato. Lo stesso vale per
l'incremento: nella `send` accompagna un inserimento, in una `sem_post` con
attendenti accompagna uno **sfilamento**. Stessa istruzione, direzione opposta.

Da cui la formulazione che vale oltre il caso:

> Un modulo non esporta le funzioni che sa calcolare, esporta le operazioni di
> cui **garantisce un'invariante**. In `enqueue_coda` l'incremento non è una
> routine chiamata dallo splice: sono tre istruzioni che ci **cadono dentro** con
> una `j`, apposta perché non esista un percorso in cui una avviene senza l'altro.

#### L'obiezione che ha spostato la discussione

Alla difesa «la `.struct` incapsula già il layout» l'utente ha risposto: *se per
regole di codifica cambiassero i nomi dei campi, perché dovrei cambiare delle
funzioni del kernel?* È giusta, e distingue una cosa che si stava confondendo:
**la `.struct` incapsula l'offset, non il nome.**

E ha aperto il difetto vero, che non riguardava la mailbox: **dieci `.struct`
ridichiaravano `fwd`/`bwd`** — `TESTA`, `TCB`, `MESSAGGIO`, `BLOCCO` e le sei
`BLOCCOnn` — venti righe copiate che dovevano coincidere e che niente verificava.
`messaggio.vinc` lo documentava perfino come idioma. Controprova eseguita:
invertendo l'ordine dentro `TESTA`, una `.struct` parallela ha continuato a dire
8 dove la prima diceva 4, **senza un errore di assemblaggio**.

#### La soluzione è dell'utente, ed è l'annidamento

> *«In C un messaggio avrebbe `t_pointers pointers;` e scriverebbe
> `mex.pointers.fwd`, che è esattamente `altromessaggio.fwd` visto che importi
> una struttura e non il puntatore a una struttura.»*

Scrivibile qui, e verificato: `.field` risolve la dimensione con `const_value`
([`assembler.c:153`](../src/assembler.c#L153)), quindi `.field pointers LINK.size`
riserva il blocco senza ridichiararlo. `.struct` annidate l'assembler le rifiuta
(`nested .struct`); il blocco opaco ottiene la stessa cosa.

```asm
.struct LINK
  .field fwd
  .field bwd
.ends

.struct MESSAGGIO
  .field pointers  LINK.size   ; i link, non ridichiarati
  .field payload
.ends
```

L'accesso ai link di **qualunque** nodo è `LINK.fwd(r2)`, e non aggiunge
indirezione perché il membro è incorporato: `&nodo.pointers == &nodo`. Che è la
stessa frase del contratto di §7.5 — «il puntatore al link *è* il puntatore al
buffer, niente `container_of`».

**Il vincolo, ed è l'unico che l'assembler non verifica:** funziona perché il
blocco è il **primo** campo. Qui non ci sono espressioni (`.equ` accetta un intero
o una costante, non una somma), quindi `LINK.fwd(r)` usa un offset interno al
blocco come offset assoluto. In C l'annidamento funzionerebbe a qualsiasi offset,
perché il compilatore somma: **il modello è più generale della forma in cui lo
possiamo scrivere**, e la restrizione coincide con una decisione già presa invece
che aggiungerne una.

Prova di robustezza: invertendo `fwd`/`bwd` dentro `LINK`, `LINK.fwd` è passato a
4 **in tutte e dieci le strutture insieme**, e `TESTA.count`/`MESSAGGIO.payload`
sono rimasti a 8 perché il blocco conserva la sua dimensione.

#### Il terzo campo: `.equ` derivato, non `.struct` propria

Il punto lasciato aperto da §3.26 si chiude, e **non** nella forma in cui era
stato proposto. Una `.struct MAILBOX` propria sarebbe stata una seconda
dichiarazione dello stesso layout, cioè il difetto appena tolto rimesso dentro
per un campo solo. La forma giusta è la derivazione:

```asm
.equ MAILBOX.count  TESTA.count   ; non copia: E' quel numero con un altro nome
.equ MAILBOX.size   TESTA.size    ; e ".res MAILBOX" alloca
```

L'utente ha chiuso da sé l'obiezione residua: *questo introduce una dipendenza dai
nomi dei campi, ma il kernel dipende già dai nomi delle procedure, quindi è ok*.
Sì — **un'interfaccia è un insieme di nomi** — con la precisazione che il guadagno
non è togliere la dipendenza ma cambiarne la forma: da cablata in sette istruzioni
a **dichiarata in una riga e verificata dal compilatore**, perché se `TESTA`
rinominasse il campo l'`.equ` non assembla (`invalid value`) invece di produrre un
offset sbagliato in silenzio. Ed è la risposta letterale alla domanda di partenza:
le funzioni del kernel non cambiano, cambia la riga che dichiara la derivazione.

#### Perché sui dati sì e sulle procedure no

Chiesto se si potesse aliasare allo stesso modo i nomi delle entry. **No, e non è
un limite aggirabile:** `.equ push pippo` fallisce anche su un'etichetta *locale*
(verificato), perché le costanti e le etichette sono due spazi di nomi risolti in
momenti diversi — numeri all'assemblaggio, indirizzi al link. Il meccanismo esiste
ma sta dal lato del **fornitore**: sono le due `.global` sullo stesso indirizzo di
`enqueue_dopo_nc`/`enqueue_testa_nc` (§13.6). Un campo lo ribattezza il cliente,
una procedura solo chi la implementa.

E non lo si vorrebbe comunque, per una ragione di significato:

> Sui **dati** l'alias aggiunge informazione — `MAILBOX.count` dice di chi è la
> semantica del contatore. Sulle **procedure** la toglierebbe: nel punto di
> chiamata il nome della primitiva è l'unica cosa che dice quale disciplina è in
> vigore, e `mailbox_accoda` nasconderebbe che è la variante non contata.

#### Le vtable, e perché non servono qui

Proposte come modo di non dipendere dai nomi, e la critica dell'utente
all'implementazione C++ è fondata: l'override **lega il nome**, mentre una tabella
in stile C (`{.push = pippo}`) disaccoppia lo slot dall'implementazione, che è ciò
che conta se il contratto è il prototipo. Ma qui non servirebbero: una vtable
disaccoppia *quale implementazione gira*, non *come si chiama un campo*, e a
runtime non esiste più di un'implementazione — il progetto è statico per
decisione. Si pagherebbe indirezione, nel percorso delle code, per una varietà
eliminata apposta.

E il polimorfismo che serve c'è già in due forme, entrambe risolte
all'assemblaggio: quello **di struttura** (`coda.vasm` opera su ogni nodo che
abbia i link al posto convenuto — `LINK` gli dà finalmente un nome invece di
lasciarlo come coincidenza fra dieci dichiarazioni) e quello **di nome** (due
`.global` sullo stesso indirizzo). È il caso raro in cui si prende il vantaggio
del meccanismo senza il suo prezzo.

#### Cosa è stato scritto

| Dove | Cosa |
|---|---|
| [`coda.vinc`](../generic/coda/interface/coda/coda.vinc) | `.struct LINK` e i due vincoli in testa; `TESTA` annida invece di ridichiarare |
| `tcb.vinc`, `messaggio.vinc`, `pool.vinc` | le altre nove strutture annidano `LINK`; i commenti che documentavano l'idioma vecchio riscritti |
| `coda.vasm`, `pool.vasm`, `test_coda.vasm` | 44 accessi da `TESTA.fwd/bwd` (e 2 da `BLOCCO.fwd/bwd`) a `LINK.fwd/bwd` |
| [`mailbox.vinc`](../rtos/servizi/mailbox/interface/mailbox/mailbox.vinc) **(nuovo)** | il tipo `MAILBOX` come `.equ` derivati, col perché della derivazione contro la copia |
| `mailbox/CMakeLists.txt` | nasce `interface/`, e la nota che lo prevedeva («il giorno che la mailbox avesse dei codici di esito propri») aveva indovinato il quando e sbagliato il cosa: non codici, un **tipo** |
| `messageHandling.vasm` | sette accessi a `MAILBOX.count`, e il motivo accanto ai due `li` — la voce che §3.26 lasciava aperta |
| `tests/test_include.vasm` | il commento nominava `duplicate constant TCB.fwd`, che non esiste più |

**Zero cambiamenti binari**: sei `.vx` confrontati byte per byte con quelli di
prima, tutti identici. Era prevedibile e va detto perché è ciò che rende il
passaggio a costo nullo — gli offset non si sono mossi (`TESTA.count` = 8,
`TESTA.size` = 12, `TCB.sp` = 8, `MESSAGGIO.payload` = 8): è cambiato **da dove
vengono**, non quanto valgono.

#### Cosa resta

- ~~**`SEMAFORO.risorse` non è scritto**~~ — **SCRITTO il 10/09/2026 (§3.35)**,
  esattamente nella forma decisa qui: un `.equ` derivato da `TESTA.count`, col
  motivo del nome diverso (contabile, non descrittivo) accanto. Scrivendolo è
  però caduta una frase di §13.1 della proposta: il `sem: .word 0, 0, 10` non è
  scrivibile, perché una `TESTA` vuota non è fatta di zeri;
- **niente di questa sessione è nella proposta.** Sono due voci: `LINK` come
  idioma di dichiarazione dei nodi (tocca §3, che elenca le strutture) e il campo
  nominato dal proprietario (§8.2 e §13.3). La tabella di §3.26 resta valida per
  tutto il resto;
- il campo si chiama `pointers` e la struttura `LINK`, presi dall'esempio in C
  dell'utente e dai test. Nessuno dei due è stato discusso come nome.

#### Coda della sessione: dove sta andando la toolchain, e perché conta

La discussione è finita su tre osservazioni che non riguardano il kernel e che
vale la pena non perdere, perché cambiano l'ordine di ciò che conviene fare.

**1. L'assembler è già mezzo front-end, e si vede dove si ferma.** Le direttive
non sono comodità di scrittura, sono le categorie che un compilatore deve
emettere: `.struct`/`.field` sono tipi record, `.res TIPO` è storage tipizzato,
`.include` idempotente più `-I` è un sistema di header, `.global`/`.extern` è il
linkage, e le due forme di oggi — l'`.equ` derivato e `.field pointers LINK.size`
— sono `typedef` e membro incorporato, cioè roba di front-end. In più la ABI è
**scritta** (`coda_api.vinc`: argomenti, esito, scratch, chi è foglia), ed è la
specifica da cui un generatore di codice partirebbe.

Il confine fra i due strati si vede rotto in un punto preciso, ed è il debito di
§8.7: `.proc` fa liveness analysis sul corpo e salva i registri che scrive
([`assembler.c:815-827`](../src/assembler.c#L815-L827)), ma «*does NOT see what a
callee clobbers*». `send_s` promette più di quanto dia **per quel motivo**. Non è
un baco: è il punto in cui l'assembler ha provato a fare il mestiere del
compilatore senza avere ciò che lo distingue, il grafo delle chiamate.

Cosa manca per salire davvero: i tipi ci sono sulla **memoria** ma non sui
**registri** (niente vieta di leggere `MAILBOX.count` da un TCB), e manca
l'allocazione dei registri — che qui non serve solo perché le variabili vive sono
quelle tenute a mano in `r1`/`r2`/`r3` per convenzione.

**2. C'è un linker, non c'è un locator.** [`toolchain.c:336`](../src/toolchain.c#L336)
assegna le basi dei moduli «*in command order*», concatena, risolve i simboli e
rialloca. Due sezioni cablate, `text` e `data`; nessuna nozione di regione di
memoria, nessun allineamento, nessun controllo di traboccamento, nessuna mappa in
uscita. Su una macchina con memorie a costi diversi — scratchpad veloce, finestra
DMA, program memory — nessuna delle tre cose che servono è esprimibile.

I tre pezzi, nell'ordine in cui converrebbe farli:

- **`.align N`**, e la garanzia che il linker non rompa l'allineamento quando
  concatena i moduli — oggi lo farebbe, perché la base del modulo successivo è la
  fine del precedente. È il pezzo che una macchina **vettoriale** rende
  obbligatorio, ed è piccolo abbastanza da provare su di esso la disciplina dei
  `.vx` byte per byte;
- **`.section nome`** al posto dei due nomi cablati: è il prerequisito di tutto
  il resto (senza un nome un modulo non può dire dove vuole finire) ed è il più
  invasivo, perché tocca assembler, formato `.vo` e linker;
- **regioni e mappa**: un file di collocazione che dichiara le memorie, il link
  che **fallisce** se una regione trabocca invece di scrivere oltre, e un `.map`
  che dice dove è finito ogni simbolo e quanto spazio resta.

Due clienti già pronti nel kernel, che tengono il lavoro fuori dall'astratto: gli
**stack dei task** (una regione propria, riempita di un pattern per misurare il
consumo massimo) e le **sei classi del pool**, che sono già partizionate per
taglia e sarebbero da partizionare per memoria.

**3. Il contesto vettoriale nel context switch è la domanda non fatta.**
[`machine.vasm:17-19`](../hal/impl/src/machine.vasm#L17-L19) dichiara i registri
vettoriali (`v*`, `vl`, `vmask`) **volatili** attraverso la preemption, e lo
motiva con «la trap arriva solo al confine d'istruzione: nessuna corsia viva da
salvare». La motivazione risponde a una domanda diversa da quella che conta:
esclude di dover salvare uno stato **parziale** — vero, ed è il regalo di una
macchina senza pipeline — ma non che ci sia un **valore** vivo. Un task con `v0`
carico dentro un loop, preemptato, al ritorno trova `v0` di qualcun altro.

Quindi «volatili» è un vincolo sul **codice applicativo**: nessun valore
vettoriale attraversa un punto di preemption. Regge qui; su una macchina vera,
dove il file vettoriale è dell'ordine dei kilobyte, diventa la scelta centrale di
latenza — salvarlo sempre domina il costo dello switch, non salvarlo impone una
disciplina che chi scrive il DSP non rispetterà. Le uscite note sono il
**salvataggio pigro** (flag nel TCB, si salva solo se il task entrante lo usa) e
la **partizione dichiarata** fra task vettoriali e non. Nessuna delle due è nel
modello, e sono le uniche parti che **non** si deducono da ciò che è già deciso:
servono i numeri della macchina.

---

## 4. Invarianti di regressione — come verificare che nulla si sia rotto

> ### Si fa con `ctest`, ed è l'unico modo che resta (§3.17, §3.24)
>
> ```bash
> cmake -B out -S . && cmake --build out -j && ctest --test-dir out
> ```
>
> 26 test: le tre invarianti storiche, i nove test mirati (`coda`, `pool`,
> `timeout`, `mailbox`, `scheduler`, `block`, `catena`, `gestore`, più
> `proc`/`include`/`epsw` sulla toolchain), e i 13 programmi di `standalone/`
> che devono continuare a girare da soli. I numeri
> attesi stanno **ognuno accanto al programma che lo produce** — nel
> `CMakeLists.txt` di `generic/test/`, di `rtos/test/`, o in quello di primo
> livello per ciò che resta suo — e comunque in **un posto solo**: è `ctest` a
> confrontarli, non chi legge.
>
> **Questo era il premio della migrazione**, più dei path: fino al 05/09/2026 la
> suite era un commento e la disciplina di chi lo eseguiva — pipeline scritte a
> mano nelle intestazioni dei test e il confronto con le sequenze attese fatto
> **a occhio**. Il confronto è di ciò che questa sezione dichiara invariante — la
> sequenza dei `dumps`, o le tre statistiche — non di tutto l'output, che
> renderebbe i test più fragili del contratto.
>
> #### Le pipeline scritte a mano non ci sono più, e non è una perdita
>
> Fino a §3.24 questa sezione le riportava tutte, come modo di isolare un singolo
> passo. **Nominavano `linked/scheduler/hal/`, `linked/scheduler/kernel/` e
> `linked/scheduler/include/`, che non esistono più.** Riscriverle a mano
> significherebbe ricostruire la doppia verità appena tolta: sarebbero copie
> destinate a divergere in silenzio, con `ctest` verde e il documento che mente.
>
> Per isolare un singolo passo si usa quello che il build già sa dire:
>
> ```bash
> ctest --test-dir out -R coda --output-on-failure   # un test solo, con l'output
> ctest --test-dir out -N                            # elenca i 26 senza eseguirli
> cmake --build out -j --verbose                     # i comandi asm/ld esatti
> ```
>
> L'ultimo è il sostituto vero delle pipeline: stampa la riga di comando che
> CMake esegue davvero, `-I` compresi, e non può divergere da ciò che gira.
>
> **Le stesse intestazioni dei test le contengono ancora**, e sono altrettanto
> false: è l'ultimo pezzo della doppia verità, elencato fra ciò che resta in
> fondo a §3.24.

Le sequenze attese, per chi deve leggerle senza aprire il build:

| verifica | atteso | dove sta il numero |
|---|---|---|
| `saxpy` — istruzioni / vec-elem-ops / cicli | `17 40 94` | `CMakeLists.txt` |
| `scheduler` — priorità, rotazione fra pari, idle > 0 | `74 5 10 59` | `rtos/test/` |
| `multi` — link con inclusione selettiva | `18 40 95` | `CMakeLists.txt` |
| `coda` — invariante dei link (§3.14), `enqueue_dopo_nc` e `coda_peek` (§3.35) | `0 1 1 1 0 0 0 2 1 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0` | `generic/test/` |
| `pool` — sei classi, alloc/free (§3.15) | `10 4 0 0 9 0 32 3 2 0 0 4 4 4 3 0 0 1 4` | `generic/test/` |
| `timeout` — vettore di descrittori (§3.13) | `3 0 150 1 2 0 0 0 3 0 1` | `generic/test/` |
| `mailbox` — send/receive con blocco (§3.10) | `0 1 2 11 22 0 33 0 0` | `rtos/test/` |
| `block` — un task che DORME, e la CPU libera vera | `3 28` | `rtos/test/` |
| `catena` — A→B→C, `send_s` da task, risveglio che non preempta | `3 3 3 778` | `rtos/test/` |
| `gestore` — timeout+pool+mailbox sotto lo scheduler (§3.33) | `5 0 2609` | `rtos/test/` |
| `semaforo` — l'inserimento ordinato di §13.6 (§3.35) | `123456 2 1 -6` | `rtos/test/` |
| `mutex` — ceiling giusto contro ceiling sbagliato, §13.5 (§3.35) | `0 0 1 1 1` | `rtos/test/` |
| `proc` — `.proc`/`.endproc` (§3.6) | `100 200 300` | `CMakeLists.txt` |
| `include` — `-I` e idempotenza (§3.16, §3.24) | `20 16 16 512 1` | `CMakeLists.txt` |
| `epsw` — `mfepsw`/`mtepsw` (§3.19) | `1 0 0 7` | `CMakeLists.txt` |

Più i 13 programmi di `standalone/`, per cui si verifica che nessuno vada in
errore, non cosa stampano.

> Nota: `rtos/demo/scheduler_demo.vasm` **fallisce di proposito** in modalità
> legacy a file singolo, perché ha `.extern ready`: va necessariamente linkato.
> Non è una regressione.

Stato verificato il 06/09/2026 (dopo §3.24, la ristrutturazione dell'albero):
invarianti **immobili**, e verificate più strettamente del solito — a ogni
commit i sei `.vx` sono stati confrontati **byte per byte** con quelli del
commit precedente, ricostruito in una cartella separata. Non «gli stessi
numeri»: gli stessi byte. `ctest` 23/23.

Stato verificato il 05/09/2026 (dopo §3.22, la decomposizione in librerie):
invarianti **immobili** rispetto al passo 3 — le costanti sono le stesse, cambia
solo chi le riceve, e il codice eseguito non cambia. `ctest` 23/23.

Stato verificato il 05/09/2026 (dopo §3.21): (1) 17/40/94, **(2) 97/64 con 8
tick** (98/65 → 94/60 → 97/64 nella stessa giornata: la parola di stato nel frame
costa 6 istruzioni per switch, il percorso di trap nuovo ne toglie di più perché
sparisce un livello di call/ret — §3.20 e §3.21), (3) 18/40/95, e i sei test
mirati di `tests/`. `ctest` dà 23/23.

Stato verificato il 05/09/2026 (dopo §3.19): **tutti i numeri identici a quelli
di prima dell'aggiunta all'ISA**, nemmeno un ciclo di scarto, e i `.vo` identici
byte per byte.

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

---

## 5. Prossimi passi possibili

> ### ⚠ Questa sezione è più vecchia di §0 — leggere prima quello
>
> È stata scritta quando §7.4 era la decisione aperta che bloccava tutto, e
> quella premessa **non vale più dal 07/09/2026**: §7.4 è decisa (priority
> ceiling), lo scheduler a priorità è costruito e verificato, i due debiti di
> §12.3 sono chiusi, e il gestore dei timeout è un task che gira (§3.28–§3.34).
> Quello che resta davvero aperto è in cima, nel blocco di ripresa di §0, e le
> formule per cominciare stanno in §6. Qui sotto sopravvivono le **rifiniture**,
> che sono ancora tutte valide perché non dipendevano da niente di tutto ciò.

Ci sono quattro fronti. La **ristrutturazione dell'albero** è **fatta** (§3.24) e
lascia dietro solo rifiniture, elencate qui sotto. Il fronte del kernel **non ha
più un passo obbligato**: §13 è scritta (§3.35, 10/09/2026) e di lei resta solo
§13.7, il punto di preemption di `mutex_unlock`. Le tre strade che restano sono
elencate nel blocco di ripresa in §0. Resta di vecchia data il front-end `vc`.

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

### Riscrittura dello scheduler a priorità statiche — FATTA (§3.28–§3.30), e §13 sopra di lei è scritta (§3.35)

**È qui che riprende il lavoro.** Discussione del 29/08/2026, verbalizzata per
intero in [`docs/proposta-kernel-realtime.md`](proposta-kernel-realtime.md)
(diagrammi Mermaid inclusi): l'utente ha bocciato il disegno attuale a coda
singola e ha specificato un modello a **priorità statiche con un PCB per
livello**, dove il TCB preemptato viene tenuto in un campo dedicato del PCB
invece di tornare in fondo alla coda.

Il difetto che ha innescato tutto: l'attuale `scheduler`
([`rtos/scheduler/impl/src/scheduler.vasm:118-135`](../rtos/scheduler/impl/src/scheduler.vasm#L118-L135))
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

**§7.4 È DECISA dal 06/09/2026: priority ceiling** (§3.25). Era la questione
aperta più a lungo del progetto. L'argomento che ha chiuso non è quello della
calcolabilità a compile-time che la proposta raccomandava, ma il costo dinamico:
col ceiling la priorità cambia **solo al task che sta girando**, mentre
l'ereditarietà promuove il possessore, che per definizione non gira — e in un
modello a una coda per livello quella promozione è uno spostamento fra teste,
non la scrittura di un campo.

Semafori e mutex sono ora **specificati in §13 della proposta**, degrado del
mutex compreso (§13.5). Quel che resta aperto sta in §13.8, e la voce grossa è
la tensione sui timeout descritta qui sopra al punto 5 di §0.

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

### Ristrutturazione dell'albero: una cartella per libreria (FATTA il 06/09/2026)

**Fatta in dieci commit, §3.24, che va letta per intera** — c'è il criterio
dell'utente che ha deciso la forma, le tre risposte (due delle quali hanno
bocciato le proposte di §3.23), e il modo in cui è stata verificata.

L'albero è a tre strati; ogni libreria ha `impl/src` e `interface/<nome>`; il
`-I` per libreria è un vincolo imposto dalla macchina, non più un commento; il
`CMakeLists.txt` di primo livello è passato da 167 righe a 105 e non conosce più
il percorso di un solo sorgente dell'RTOS.

**Cosa resta, e nessuna delle quattro blocca niente:**

1. **La doppia verità nelle intestazioni dei test.** È il pezzo non fatto di
   §3.24 e l'unico che *peggiora* col tempo: le pipeline scritte a mano nominano
   `linked/scheduler/...`, che non esiste più, e i numeri attesi sono ripetuti
   accanto. §4 di questo documento è già stata ripulita, le intestazioni no. Le
   intestazioni vanno riscritte con **cosa** verifica il test e **perché**;
   pipeline e numeri se ne vanno dove li esegue la macchina. `test_include.vasm`
   è già stato riscritto così, perché doveva cambiare comunque — vale da
   modello.
2. **I difetti minori del build**, elencati in fondo a §3.23: `CMAKE_SOURCE_DIR`
   dove va `PROJECT_SOURCE_DIR`, `add_compile_options(-Wall -Wextra)` globale e
   non guardato dal compilatore (va sul target con
   `$<$<C_COMPILER_ID:GNU,Clang>:...>`), `file(GLOB)` senza `CONFIGURE_DEPENDS`
   per `standalone/`, e la collisione fra il programma `multi` e il test `multi`
   — che funziona solo perché sono due spazi di nomi diversi di CMake.
3. **`examples/`.** Con `linked/scheduler/` sparito, `linked/` ha un solo
   abitante: `linked/multi/`. Insieme a `standalone/` sono entrambi esempi del
   **simulatore**, e starebbero bene sotto un `examples/`. **Proposto e non
   risposto** — è `git mv` e non muove nessun numero (i test si chiamano per
   `NAME_WE`), ma è una decisione dell'utente.
4. **`--emit-deps`** (punto 3 della sezione precedente). Vale più di prima: con
   un `-I` per libreria una dipendenza non dichiarata è già un errore di
   assemblaggio, quindi `--emit-deps` servirebbe ora per la sola correttezza del
   rebuild incrementale, non per la disciplina.

**La regola di verifica, che ha funzionato e va riusata:** non cambiare mai
semantica del build e albero nello stesso commit, e a ogni commit confrontare i
`.vx` byte per byte con quelli del commit precedente, ricostruito in una
cartella separata. È più stretto di «gli stessi numeri» e costa dieci secondi.

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

Aprire Claude Code nella cartella del progetto e scrivere una di queste.

**Per riprendere in generale:**
```
Leggi docs/stato-lavori.md e riprendi da lì.
```

> ### ⚠ Questa sezione si riscrive quando il lavoro si sposta
>
> Il 07/09/2026 conteneva ancora otto formule, e metà mandavano su lavoro già
> fatto: «le priorità non esistono ancora», «riprendi dalla decisione aperta
> §7.4», «alla fine `ctest` deve dare 23/23». Una formula di ripresa che
> istruisce il futuro con il passato è peggio di una assente, perché sembra
> autorevole. Le formule superate sono state tolte, non archiviate: la cronaca
> di come ci si è arrivati sta in §3, che è il posto giusto per il passato.

**Il prossimo passo — semafori e mutex, §13:**
```
Leggi docs/proposta-kernel-realtime.md §13 per intero, poi §8 (la mailbox) e
§7.4 (perche' il ceiling). Come ci si e' arrivati sta in docs/stato-lavori.md
§3.25 e §3.26 -- e §3.26 contiene decisioni che nella proposta NON ci sono
ancora. Nascono in rtos/servizi/, accanto alla mailbox.

DECISO, da non riaprire senza una ragione nuova:
  - priority ceiling, non ereditarieta' (§7.4);
  - il semaforo NON e' una mailbox: contatore contabile contro descrittivo
    (§13.1), e non e' lo stesso tipo con un parametro diverso;
  - il mutex E' la sezione critica, cioe' cli con un limite per risorsa
    (§13.2), e non ci si blocca tenendolo (§13.4);
  - la coda del mutex esiste per far DEGRADARE un ceiling sbagliato invece
    che appendere, non per essere usata (§13.5);
  - l'ordinamento per priorita' e' del chiamante: coda.vasm prende una
    enqueue_dopo agnostica e non sa perche' la si chiama (§13.6);
  - sem_wait NON ha timeout: e' una deduzione da §9 (il timeout e' una
    consegna in mailbox), non una scelta fra due uscite (§3.26);
  - 0 e' la priorita' PIU' ALTA (§3.28), quindi §13.5 promuove al ceiling
    con un MINIMO, non con un massimo: e' il verso che si sbaglia rileggendo;
  - un solo TCB in attesa per mailbox, contatore che si incrementa per i
    messaggi, invariante count >= -1 (§3.26).

APERTO: SEMAFORO.risorse, il terzo campo di TESTA nominato dal proprietario.
Deciso nella FORMA (un .equ derivato da TESTA.count, come MAILBOX.count in
§3.27) e non scritto, perche' il semaforo non ha ancora codice.

Alla fine ctest deve dare 26/26.
```

**Per riportare nella proposta le decisioni del 07/09 (sola scrittura):**
```
Leggi docs/stato-lavori.md §3.26 e la tabella in fondo "Cosa resta da
scrivere". Cinque voci di quel giorno sono verbalizzate solo nell'handoff:
vanno riportate in docs/proposta-kernel-realtime.md nelle sezioni che la
tabella indica. Non c'e' niente da decidere: e' trascrizione.
```

**Per le intestazioni dei test — l'unico debito che peggiora col tempo:**
```
Leggi docs/stato-lavori.md §3.24 e la sezione di §5 "Ristrutturazione
dell'albero". L'albero e' fatto; resta la doppia verita' nelle intestazioni
dei test: le pipeline scritte a mano nominano linked/scheduler/, che non
esiste piu', e ripetono i numeri attesi. Vanno riscritte con COSA verifica
il test e PERCHE'; pipeline e numeri se ne vanno dove li esegue la macchina.
tests/test_include.vasm e' gia' cosi' e vale da modello. I quattro test
dell'RTOS (scheduler, block, catena, gestore) sono gia' scritti bene e
valgono da modello anche loro. Alla fine ctest 26/26, stessi numeri.
```

**Per i difetti minori del build (piccoli e indipendenti):**
```
Leggi docs/stato-lavori.md, punto 2 della sezione di §5 "Ristrutturazione
dell'albero": CMAKE_SOURCE_DIR dove va PROJECT_SOURCE_DIR, -Wall globale e
non guardato dal compilatore, file(GLOB) senza CONFIGURE_DEPENDS, la
collisione fra il programma multi e il test multi. Resta anche --emit-deps
nell'assembler, per avere le dipendenze scoperte invece che dichiarate.
Alla fine ctest 26/26.
```

**Per guardare come girano i task (non e' una modifica, e' uno strumento):**
```
python3 tools/traccia.py out/vasm/test_gestore.vx      # -> out/traccia.html
```
Dice chi gira e in quale intervallo, e quanto di quel tempo è kernel per suo
conto (§3.34). Funziona su qualunque `.vx`; il disegno sta in
`tools/traccia.template.html`.

**Per andare sul linguaggio ad alto livello:**
```
Leggi docs/stato-lavori.md e docs/proposta-linguaggio-alto-livello.md.
Implementa la fase 1 del front-end vc: lexer + parser + il costrutto
a[:] = espr con + - * elementwise, che genera .vasm.
Criterio di successo: saxpy in vc deve dare 17 istruzioni / 40 vec-elem-ops / 94 cicli.
```

Utile da sapere: il modello si cambia con `/model`, ed è ora impostato su Opus
come default in `~/.claude/settings.json`. Il contesto del progetto si
ricostruisce in fretta perché il repo è piccolo e i **quattro** documenti in
`docs/` sono aggiornati: `stato-lavori.md` (questo), `manual.md`,
`proposta-kernel-realtime.md` (fronte attivo) e
`proposta-linguaggio-alto-livello.md`.

I sorgenti C sono a **2 spazi** dal 29/08/2026 (§3.7): scrivere nuovo codice
con la stessa convenzione.