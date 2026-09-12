# Il rename all'inglese — elenco approvato

> ## ✔ APPROVATO il 13/09/2026, senza varianti
>
> L'utente ha approvato l'elenco **così com'è**: tutte e sei le scelte di §1
> (compresa `scadenza` → **`expiry`** e non `deadline`), il vocabolario di §2 e
> l'API di §3. Da qui in avanti questo documento **non è una proposta**: è il
> contratto contro cui i commit si verificano, e una divergenza fra il codice e
> queste tabelle è un difetto del codice.
>
> Se qualcosa va cambiato dopo, si cambia **qui prima** e nel codice poi.

> Deciso il **13/09/2026** (§3.42 di [`stato-lavori.md`](stato-lavori.md)): **i
> nomi passano all'inglese, i commenti restano in italiano**. Non è la
> spiegazione della decisione: quella sta in §3.42.

> ### Una regola che il primo commit ha imposto, e non era nell'elenco
>
> **Un commento che nomina un FILE non è lingua, è un riferimento.** `coda.vasm`
> dentro un commento italiano diventa `queue.vasm`, perché il file si chiama
> così: lasciarlo sarebbe la «doppia verità» che il progetto ha già fra i debiti,
> e sarebbe **creata di nuovo apposta**. La prosa italiana intorno non si tocca.
>
> Distinzione, in una riga: si traduce ciò che **nomina**, non ciò che
> **spiega**.

> ### ⚠ QUESTO FILE VA ESCLUSO DA OGNI RENAME AUTOMATICO
>
> È l'unico documento del progetto in cui il nome **vecchio** deve sopravvivere:
> è fatto di coppie vecchio→nuovo. Un `sed` passato su `docs/*.md` lo riduce a
> `tmgr_init` → `tmgr_init`, cioè a niente — ed è successo, al commit 8, e si è
> recuperato solo perché era già in git.
>
> Vale per chiunque, e per qualunque rename futuro: **`docs/rename-inglese.md`
> si aggiorna a mano o non si aggiorna.**

---

## 0. Come si approva questo elenco

Le coppie vecchio→nuovo sono circa centocinquanta, e approvarle una per una
sarebbe rumore: le etichette interne sono meccaniche e ripetitive
(`arm_uscita`, `alloc_uscita`, `can_uscita`, `sca_uscita`… sono tutte la stessa
parola quattro volte).

Quindi si approvano **tre cose, in ordine di peso**:

1. **le sei scelte vere** (§1) — dove la traduzione giusta non è ovvia e
   sceglierla cambia come si leggerà il codice;
2. **il vocabolario** (§2) — una parola italiana, una inglese, applicata
   ovunque. Approvato il vocabolario, ogni etichetta interna segue da sé;
3. **l'API esportata, le strutture e i file** (§3, §4, §5) — elencati uno per
   uno, perché sono l'**interfaccia**: è ciò che un collega legge per primo e
   non merita una regola, merita una decisione.

E §6 dice cosa **non** si rinomina, che è altrettanto importante.

---

## 1. LE SEI SCELTE VERE

Qui la traduzione letterale esiste ma non è per forza quella giusta. Per ognuna
c'è la mia raccomandazione e il perché; sono le uniche righe di questo documento
che chiedono davvero un sì o un no.

### 1.1 `gestore_timeout` → **`timeout_manager`**, prefisso `tmgr_`

Le tre uscite e perché due si scartano:

| | problema |
|---|---|
| `timeout_handler` | **collide**: «handler» in questo progetto è già l'ISR (`g_handler`, `irq_install`), e il gestore dei timeout è precisamente ciò che **non** è un'ISR — è un task |
| `timeout_server` | **collide con i documenti**: «server» nello scheduling realtime è un concetto preciso (deferrable server, sporadic server), e i quattro documenti ne parleranno |
| **`timeout_manager`** | neutro, e non è già preso da niente |

Da cui `gestore_init` → `tmgr_init`, `gestore_task` → `tmgr_task`,
`gestore_tick` → `tmgr_tick`, e la cartella `rtos/gestore_timeout/` →
`rtos/timeout_manager/`.

### 1.2 `marca` / `marche` → **`marker`** (lo strumento), **`MARK_`** (i canali)

Lo strumento è «IL MARCATORE» e quello che scrive sono marche: in inglese la
coppia esiste identica, *marker* e *marks*, ed è anche il vocabolario standard
del tracing (SystemView li chiama markers). `probe` sarebbe l'altra candidata —
e il commento di `marca.vinc` **la usa già** («un probe a costo zero sarebbe
comodo») — ma «probe» è la punta che si attacca al segnale, non il tag scritto
nel codice, e il file cita già l'ITM e le sue stimulus port per quel ruolo.

| vecchio | nuovo |
|---|---|
| `hal/interface/hal/marca.vinc` | `hal/interface/hal/marker.vinc` |
| `marche.conf` | `marks.conf` |
| `tools/marche.py` | `tools/marks.py` |
| `MARCA_ESEC` | `MARK_EXEC` |
| `MARCA_TASTO` | `MARK_KEY` |
| `MARCA_CH2` … `MARCA_CH7` | `MARK_CH2` … `MARK_CH7` |

### 1.3 `scadenza` → **`expiry`**, e NON `deadline` ← *la scelta che conta*

La traduzione naturale di `DESCRITTORE.scadenza` è `deadline`, ed è la scelta
**sbagliata**, per una ragione che si vede solo pensando ai quattro documenti:
là dentro **deadline** è un termine tecnico preciso — il vincolo temporale di un
task, la «D» di *rate/deadline monotonic*, che il documento didattico affronta
di petto. `DESCRIPTOR.deadline` sarebbe la stessa parola per l'istante in cui
scatta un timeout, che non è un vincolo di nessuno.

Sarebbe una collisione peggiore di quella di `coda`, perché nascerebbe **adesso**
e per scelta, invece di essere un residuo.

| vecchio | nuovo |
|---|---|
| `DESCRITTORE.scadenza` | `DESCRIPTOR.expiry` |
| `COD_SCADENZA` | `COD_EXPIRY` |
| `timeout_scaduto` | `timeout_expired` |

### 1.4 `TESTA` → **`HEAD`**

`TESTA` è la testa di lista — `{pointers, count}` — e il commento di
`coda.vinc` nomina già **`list_head`** come l'idioma da cui viene. `HEAD` tiene
quel filo; `LIST` leggerebbe meglio in `LIST.count` ma perderebbe il
riferimento, e quel riferimento è come si spiega la struttura a chi arriva.

### 1.5 `test_mondo` → **`test_events`** (non `test_world`)

Il nome italiano funziona per contrasto con `test_coop`: stesso kernel, con e
senza «qualcosa che può succedere». `test_world` in inglese si legge **hello
world**, cioè *il test banale* — l'esatto opposto, perché è il difficile dei
due. La proprietà che distingue la coppia è che uno è guidato da **eventi** e
l'altro dal solo tick.

Se invece la metafora del «mondo fuori» ti sta a cuore più della leggibilità,
`test_world` è l'alternativa fedele e la scrivo senza discutere.

### 1.6 `DESCRITTORE` → **`DESCRIPTOR`**

Bassa posta, la segnalo per completezza: `TIMEOUT` sarebbe più parlante ma
collide con il nome del modulo e con i `TMO_*`. Dentro un file che si chiama
`timeout.vinc`, `DESCRIPTOR` non è ambiguo.

---

## 2. IL VOCABOLARIO

Una parola, una traduzione, ovunque compaia — nei simboli esportati, nelle
etichette interne, nei campi, nelle costanti. **Approvato questo, §3 e §4
seguono meccanicamente**, e così tutte le etichette locali che non elenco.

### 2.1 Le due parole che portano una collisione

| italiano | inglese | nota |
|---|---|---|
| `coda` | **`queue`** quando è il modulo o la struttura d'attesa; **`tail`** quando è la *posizione* | è la collisione di §3.42: `coda_init` inizializza una **coda**, `enqueue_coda` accoda **in fondo**. Una parola sola per due concetti, e in italiano non si vede |
| `stato` | **`state`** | `DESCRITTORE.stato` e `TCB.state` sono già oggi la stessa parola in due lingue, in due strutture diverse. Il rename li fa coincidere |

### 2.2 Struttura dati e dominio

| | | | | | |
|---|---|---|---|---|---|
| `testa` → `head` | `nodo` → `node` | `blocco` → `block` | `dati` → `data` | `opaco` → `opaque` | `descrittore` → `descriptor` |
| `messaggio` → `message` | `carattere` → `char` | `specifiche` → `spec` | `attese` → `waiters` | `risorse` → `resources` | `owner` *(già inglese)* |
| `scadenza` → `expiry` | `vettore` → `vector` | `periodo` → `period` | `giri` → `rounds` | `ritardo` → `delay` | `prec` → `prev` |
| `preemptato` → `preempted` | `classe` → `class` | `livello` → `level` | `semaforo` → `semaphore` | `servizi` → `services` | `gestore` → `tmgr` |

### 2.3 Verbi e stati (le etichette interne)

| | | | | | |
|---|---|---|---|---|---|
| `fine` → `end` | `uscita` → `exit` | `avanti` → `next` | `giro` → `round` | `fase` → `phase` | `ordine` → `order` |
| `vuoto` → `empty` | `pieno` → `full` | `libero` → `free` | `estraneo` → `foreign` | `enorme` → `huge` | `esito` → `result` |
| `riempi` → `fill` | `svuota` → `drain` | `sveglia` → `wake` | `svegliato` → `woken` | `inviato` → `sent` | `manda` → `send` |
| `promuovi` → `promote` | `promosso` → `promoted` | `ripristinato` → `restored` | `degrado` → `degrade` | `attendi` → `block` | `attesa` → `wait` |
| `somma` → `sum` | `apri` → `open` | `controlla` → `check` | `scrivi` → `write` | `cammina` → `walk` | `avanza` → `advance` |
| `parcheggio` → `park` | `nessuno` → `none` | `ce_ne_uno` → `found` | `solo` → `alone` | `su` → `up` | `ultimo` → `last` |
| `testimone` → `witness` | `scalare` → `scalar` | `pulito` → `clean` | `consegna` → `deliver` | `preleva` → `take` | `doppio` → `double` |
| `armato` → `armed` | `liberato` → `freed` | `giusto` → `right` | `subito` → `now` | `riparti` → `restart` | `osservato` → `obs` |

---

## 3. L'API ESPORTATA — 18 simboli su 72

Elencati uno per uno perché sono l'interfaccia. Gli altri 54 `.global` sono già
inglesi e **non si toccano** (`task_yield`, `mutex_lock`, `sem_post`,
`irq_save`, `request_preempt`, `sched_isr_exit`, tutto l'HAL…).

### `generic/coda` → `generic/queue` (12 simboli)

| vecchio | nuovo | |
|---|---|---|
| `coda_init` | `queue_init` | |
| `coda_peek` | `queue_peek` | |
| `enqueue_coda` | `enqueue_tail` | ← qui la collisione si scioglie |
| `enqueue_coda_nc` | `enqueue_tail_nc` | |
| `enqueue_coda_s` | `enqueue_tail_s` | |
| `enqueue_testa` | `enqueue_head` | |
| `enqueue_testa_nc` | `enqueue_head_nc` | |
| `enqueue_testa_s` | `enqueue_head_s` | |
| `enqueue_dopo_nc` | `enqueue_after_nc` | |
| `dequeue_testa` | `dequeue_head` | ← «toglie dalla TESTA», che oggi si legge «dalla coda» |
| `dequeue_testa_nc` | `dequeue_head_nc` | |
| `dequeue_testa_s` | `dequeue_head_s` | |

### `rtos/gestore_timeout` → `rtos/timeout_manager` (3)

| vecchio | nuovo |
|---|---|
| `gestore_init` | `tmgr_init` |
| `gestore_task` | `tmgr_task` |
| `gestore_tick` | `tmgr_tick` |

### `generic/timeout` (2) e `rtos/scheduler` (1)

| vecchio | nuovo |
|---|---|
| `timeout_scaduto` | `timeout_expired` |
| `tmo_vettore` | `tmo_vector` |
| `sched_pronto_sopra` | `sched_ready_above` |

---

## 4. STRUTTURE, CAMPI E COSTANTI

### 4.1 Tipi

| vecchio | nuovo | | vecchio | nuovo |
|---|---|---|---|---|
| `TESTA` | `HEAD` | | `MESSAGGIO` | `MESSAGE` |
| `NODO` | `NODE` | | `CARATTERE` | `CHAR` |
| `DESCRITTORE` | `DESCRIPTOR` | | `BLOCCO` | `BLOCK` |
| `BLOCCO16`…`BLOCCO512` | `BLOCK16`…`BLOCK512` | | *(`TCB`, `PCB`, `LINK`, `MUTEX`, `PAYLOAD` restano)* | |

### 4.2 Campi

| vecchio | nuovo | perché |
|---|---|---|
| `PCB.coda` | `PCB.queue` | la coda dei pronti di quel livello |
| `MUTEX.coda` | `MUTEX.queue` | la coda degli attesi |
| `PCB.preemptato` | `PCB.preempted` | |
| `MUTEX.prio_prec` | `MUTEX.prio_prev` | |
| `DESCRITTORE.stato` | `DESCRIPTOR.state` | coincide con `TCB.state`, che è già inglese |
| `DESCRITTORE.scadenza` | `DESCRIPTOR.expiry` | **non** `deadline`: §1.3 |
| `BLOCCO.dati` | `BLOCK.data` | |
| `PAYLOAD.specifiche` | `PAYLOAD.spec` | |

*(`pointers`, `count`, `fwd`, `bwd`, `sp`, `pcb`, `state`, `crit`, `owner`,
`ceiling`, `pool`, `payload`, `ch`, `clientTag`, `messageCode`, `messageType`,
`replyMailbox` sono già inglesi.)*

### 4.3 Costanti `.equ`

| vecchio | nuovo | | vecchio | nuovo |
|---|---|---|---|---|
| `POOL_VUOTO` | `POOL_EMPTY` | | `GIRI` | `ROUNDS` |
| `POOL_ESTRANEO` | `POOL_FOREIGN` | | `PERIODO` | `PERIOD` |
| `POOL_ENORME` | `POOL_HUGE` | | `RITARDO` | `DELAY` |
| `MUTEX.attese` | `MUTEX.waiters` | | `COD_SCADENZA` | `COD_EXPIRY` |
| `SEMAFORO.risorse` | `SEMAPHORE.resources` | | `MARCA_*` | `MARK_*` (§1.2) |
| `SEMAFORO.size` | `SEMAPHORE.size` | | `CODA_OK`/`_LINKED`/`_UNLINKED` | `QUEUE_OK`/`_LINKED`/`_UNLINKED` |

---

## 5. FILE E CARTELLE

Questo è il gruppo che tocca il **build**: 39 `CMakeLists.txt`/`.cmake`
nominano queste cartelle nei `-I` e nelle librerie. Va da sé, ma va detto: è un
commit a parte dal rename dei simboli.

| vecchio | nuovo |
|---|---|
| `generic/coda/` (+ `coda.vasm`, `coda.vinc`, `coda_api.vinc`) | `generic/queue/` (+ `queue.vasm`, `queue.vinc`, `queue_api.vinc`) |
| `generic/messaggio/` (+ `messaggio.vinc`) | `generic/message/` (+ `message.vinc`) |
| `rtos/servizi/` | `rtos/services/` |
| `rtos/servizi/semaforo/` (+ `semaforo.vasm`, `semaforo.vinc`) | `rtos/services/semaphore/` (+ `semaphore.vasm`, `semaphore.vinc`) |
| `rtos/gestore_timeout/` (+ `gestore_timeout.vasm`, `.vinc`) | `rtos/timeout_manager/` (+ `timeout_manager.vasm`, `.vinc`) |
| `hal/interface/hal/marca.vinc` | `hal/interface/hal/marker.vinc` |
| `marche.conf` | `marks.conf` |
| `tools/marche.py` | `tools/marks.py` |
| `tools/traccia.py` | `tools/trace.py` |
| `tools/traccia.template.html` | `tools/trace.template.html` |
| `tools/traccia_dom.js` | `tools/trace_dom.js` |
| `generic/test/test_coda.vasm` | `generic/test/test_queue.vasm` |
| `rtos/test/test_semaforo.vasm` | `rtos/test/test_semaphore.vasm` |
| `rtos/test/test_gestore.vasm` | `rtos/test/test_tmgr.vasm` |
| `rtos/test/test_catena.vasm` | `rtos/test/test_chain.vasm` |
| `rtos/test/test_vettori.vasm` | `rtos/test/test_vectors.vasm` |
| `rtos/test/test_mondo.vasm` | `rtos/test/test_events.vasm` (§1.5) |

I nomi dei test in `ctest` seguono i file: `coda` → `queue`, `semaforo` →
`semaphore`, `gestore` → `tmgr`, `catena` → `chain`, `vettori` → `vectors`,
`mondo` → `events`.

Tutti i rename di file si fanno con **`git mv`**, così la storia di ogni file
resta seguibile con `git log --follow`.

---

## 6. COSA NON SI RINOMINA

Altrettanto deciso quanto il resto:

- **i commenti**, che restano in italiano (§3.42). L'inglese ci arriva solo dove
  un documento inglese li cita, e **dopo** il nucleo fattuale;
- **i documenti in `docs/`** — nomi e prosa. Sono in italiano e ci restano; i
  quattro documenti sullo scheduler nascono bilingui per decisione loro. Ma le
  **citazioni di codice** dentro di essi si aggiornano — **106 occorrenze in 7
  file** — perché citano codice e non prosa: se `stato-lavori.md` dice
  `enqueue_coda` e il codice dice `enqueue_tail`, il documento è **sbagliato**;
- **i 54 `.global` già inglesi**, e i campi già inglesi di §4.2;
- **i sorgenti C** in `src/` e `include/`. Sono già in gran parte inglesi, e
  quello che è italiano lì dentro sono **commenti**, non nomi;
- **`git`**: il ramo `mutex-cede-solo-se-serve` resta com'è. È storia, e la
  storia non si riscrive per coerenza di lingua.

---

## 7. IL PIANO DEI COMMIT, E LA VERIFICA

Otto commit, ognuno verde e ognuno verificato. **Piccoli e in quest'ordine**,
perché i file si spostano prima che i simboli cambino e il build non deve mai
inseguire due cose insieme.

| | commit | tocca |
|---|---|---|
| 1 | `generic/coda` → `generic/queue`, file e cartelle | `git mv` + i `CMakeLists` |
| 2 | i 12 simboli della coda, e la collisione `coda`/`testa` si scioglie | 34 file |
| 3 | `generic/messaggio` → `message`, `rtos/servizi` → `services`, `semaforo` → `semaphore` | `git mv` + `CMakeLists` |
| 4 | `gestore_timeout` → `timeout_manager` e i tre `tmgr_*` | |
| 5 | `timeout_expired`, `tmo_vector`, `DESCRIPTOR` e `expiry` | |
| 6 | `sched_ready_above`, strutture, campi, costanti | |
| 7 | `marca`/`marche` → `marker`/`marks`, `traccia` → `trace` | tools + il `.vinc` generato |
| 8 | i nomi dei test e le citazioni nei documenti | |

**La verifica, a ogni singolo commit**, e sono due cose che falliscono su fatti
diversi:

```
cmake --build build && (cd build && ctest)        # 32/32
tools/fingerprint.sh | diff prima.txt -           # vuoto
```

Il secondo è [`tools/fingerprint.sh`](../tools/fingerprint.sh), scritto e
**provato in tutte e due le direzioni** prima di fidarsene (commit `3fba9fe`):
su un rename vero il diff è vuoto, e su **una sola istruzione in più** in
`scheduler.vasm` — piazzata per giunta su un ramo mai eseguito — il diff scatta
su **dieci programmi su quindici** mentre `ctest` resta 32/32.

Quest'ultimo numero è la ragione per cui lo strumento esiste: la suite di test
da sola **non avrebbe visto** quel cambiamento. Un diff vuoto dice che
l'eseguibile linkato è lo stesso programma con etichette diverse, ed è una
prova, non un argomento.

La base di riferimento si prende **una volta**, sul commit `3fba9fe`, prima del
primo rename, e vale per tutti e otto.
