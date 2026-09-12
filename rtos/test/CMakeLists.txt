# ---------------------------------------------------------------------------
#  I programmi che verificano rtos/.
#
#  test_mailbox e' qui e non in generic/ per il criterio letterale: legge
#  `current`, cioe' usa lo scheduler. Attraversa tre librerie -- code, messaggi
#  e kernel -- ed e' il caso che mostra meglio perche' questi file sono
#  applicazioni e non test unitari di una libreria sola.
#
#  Si porta dentro anche il kernel che non c'e': definisce lui gli stub di
#  task_ready e task_block, che scheduler.vasm non ha ancora. Quando §7.4
#  atterrera' quegli stub vanno via, e sara' quello il momento in cui
#  lib_messaggi comincera' a chiudersi da sola.
# ---------------------------------------------------------------------------
vasm_object(t_mailbox SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_mailbox.vasm
                      INTERFACES vinc_queue vinc_tcb vinc_message)

vasm_program(test_mailbox OBJECTS t_mailbox LINK lib_messaggi)

vasm_check(mailbox MODE DUMPS PROGRAM test_mailbox
           EXPECT "0 1 2 11 22 0 33 0 0")

# ---------------------------------------------------------------------------
#  test_scheduler — lo scheduler a priorita' statiche con PCB (§3, §4).
#
#  Prende il posto di rtos/demo/scheduler_demo, ritirato il 07/09/2026 insieme
#  alla sua invariante: verificava l'alternanza al tick, che con le priorita'
#  non e' piu' il comportamento atteso.
#
#  E' il PRIMO STRATO del test di §3.28: qui nessuno si blocca, perche' il
#  blocco vuole task_block (§8.8) che ancora non c'e'. Verifica la precedenza
#  per priorita', lo slot di §2 (il preemptato non riparte da solo) e che la
#  scansione non raggiunga mai l'idle.
#
#  Il sistema parte con la sola IDLE, e M e H entrano al 2o e al 4o tick. Non e'
#  una complicazione: se tutti fossero eseguibili dall'inizio l'idle non
#  girerebbe mai e il suo contatore -- che e' la STATISTICA DI CPU LIBERA --
#  varrebbe 0 per costruzione, senza misurare niente.
#  I quattro numeri dipendono dai cicli; cio' che il test asserisce e' che siano
#  TUTTI > 0. Il piu' importante e' cntD (il terzo): senza la rotazione fra pari
#  varrebbe esattamente zero, perche' M finirebbe nello slot a ogni tick e lo
#  slot batte la coda.
#
#  "73 ... 58" dall'11/09/2026 (era "74 ... 59"): sp_mio_livello legge TCB.crit
#  prima di ruotare, e le due istruzioni in piu' si pagano sui contatori che
#  dipendono dai cicli. Il numero che NON si e' mosso e' cntD, ed e' la
#  non-regressione che conta: qui nessuno tiene un mutex, quindi la rotazione
#  deve funzionare esattamente come prima.
# ---------------------------------------------------------------------------
#
#  "67 4 8 57" dal 12/09/2026 (era "73 5 10 58"): ctx_save guarda PSW_VDIRTY e spinge una
#  parola di flag anche quando non c'e' niente di vettoriale da salvare, e
#  ctx_restore la rilegge -- ~9 istruzioni per commutazione sul percorso PULITO,
#  cioe' il prezzo che i task scalari pagano perche' quelli vettoriali possano
#  esistere. Nessuno dei quattro e' derivabile; cio' che si asserisce e' che siano tutti > 0, e il piu' importante resta cntD (il terzo), che senza la rotazione fra pari varrebbe zero.
# ---------------------------------------------------------------------------
vasm_object(t_scheduler SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_scheduler.vasm
                        INTERFACES vinc_queue vinc_tcb)

vasm_program(test_scheduler OBJECTS t_scheduler LINK lib_kernel)

vasm_check(scheduler MODE DUMPS PROGRAM test_scheduler
           EXPECT "67 4 8 57")

# ---------------------------------------------------------------------------
#  test_block — il blocco volontario: un task che dorme davvero.
#
#  Primo uso vero di task_block/hal_ctx_block (§8.8). E' il passo piu' piccolo
#  che li mette in gioco -- due task, una mailbox, l'ISR che consegna -- prima
#  della simulazione completa di §3.28, che ne metterebbe cinque insieme.
#
#  E' anche il primo programma che linka le DUE librerie: lib_messaggi per la
#  mailbox e lib_kernel per lo scheduler. Da quando lib_messaggi non nomina piu'
#  lib_kernel nel proprio LINK, e' il programma a comporre le due meta' -- ed e'
#  la ragione per cui quella separazione andava fatta.
#
#  cntW e' DERIVABILE (i tick pari prima dell'8: 2, 4, 6 -> 3); cntI dipende dai
#  cicli, e cio' che conta
#  e' che sia > 0: e' la CPU libera misurata mentre un task DORME, non mentre il
#  sistema non e' ancora partito.
# ---------------------------------------------------------------------------
#
#  "3 25" dal 12/09/2026 (era "3 28"): ctx_save guarda PSW_VDIRTY e spinge una
#  parola di flag anche quando non c'e' niente di vettoriale da salvare, e
#  ctx_restore la rilegge -- ~9 istruzioni per commutazione sul percorso PULITO,
#  cioe' il prezzo che i task scalari pagano perche' quelli vettoriali possano
#  esistere. cntW resta 3, che e' il numero DERIVABILE: si muove solo la CPU libera.
# ---------------------------------------------------------------------------
vasm_object(t_block SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_block.vasm
                    INTERFACES vinc_queue vinc_tcb vinc_mailbox)

vasm_program(test_block OBJECTS t_block LINK lib_messaggi lib_kernel)

vasm_check(block MODE DUMPS PROGRAM test_block
           EXPECT "3 25")

# ---------------------------------------------------------------------------
#  test_chain — la catena A -> B -> C: due salti di mailbox fatti da TASK.
#
#  Primo dei due passi che restano del test di §3.28 (l'altro e' il gestore dei
#  timeout come task, che tira dentro anche il pool). Mette in gioco tre pezzi
#  che test_block non toccava:
#
#    - send_s, la send da contesto di task con IE=1. In test_block manda solo
#      l'ISR, quindi il wrapper non era mai stato eseguito;
#    - il ramo di task_ready che NON preempta: A sveglia B, che gli sta sotto,
#      e la blt sui puntatori ai PCB prende per la prima volta l'altro ramo;
#    - la scansione di task_block che attraversa livelli POPOLATI (pcb1 -> pcb2
#      -> pcb3), invece di trovare solo l'idle sotto al bloccato.
#
#  I primi tre numeri sono DERIVABILI e devono venire UGUALI: tre tick pari
#  prima dell'8 (2, 4, 6), quindi tre messaggi entrano nella catena e tre devono
#  uscirne. Sono tenuti separati per salto perche' se uno si perdesse direbbero
#  DOVE, mentre un contatore solo direbbe soltanto che qualcosa non torna.
#  Il quarto dipende dai cicli: e' la CPU libera con tre task che dormono.
#
#  Il periodo del timer e' 2000 cicli e non i 500 di test_block, perche' un giro
#  di catena costa quattro commutazioni: a 800 cicli cntC viene 0 -- l'ultimo
#  anello muore di fame mentre i primi due tengono il passo, che e' la
#  saturazione di un sistema a priorita' statiche in miniatura. Il commento in
#  cima al test riporta la soglia misurata.
# ---------------------------------------------------------------------------
#
#  "3 3 3 632" dal 12/09/2026 (era "3 3 3 778"): ctx_save guarda PSW_VDIRTY e spinge una
#  parola di flag anche quando non c'e' niente di vettoriale da salvare, e
#  ctx_restore la rilegge -- ~9 istruzioni per commutazione sul percorso PULITO,
#  cioe' il prezzo che i task scalari pagano perche' quelli vettoriali possano
#  esistere. I tre derivabili restano 3 3 3. Il quarto cala del 19%, ed e' il test con piu' commutazioni per giro: quattro.
# ---------------------------------------------------------------------------
vasm_object(t_chain SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_chain.vasm
                     INTERFACES vinc_queue vinc_tcb vinc_mailbox)

vasm_program(test_chain OBJECTS t_chain LINK lib_messaggi lib_kernel)

vasm_check(chain MODE DUMPS PROGRAM test_chain
           EXPECT "3 3 3 632")

# ---------------------------------------------------------------------------
#  test_tmgr — il gestore dei timeout come TASK: l'ultimo passo di §3.28.
#
#  Tenuto per ultimo apposta: e' l'unico che tira dentro anche il pool (§10),
#  quindi arriva quando lo scheduler a priorita', il blocco volontario e la
#  catena di mailbox sono gia' verificati da tre test loro.
#
#  E' il primo programma che compone QUATTRO librerie -- kernel, messaggi,
#  timeout e pool -- ed e' l'unico posto in cui §8, §9 e §10 girano insieme
#  sotto lo scheduler vero: A arma, si blocca, l'ISR sveglia il gestore, il
#  gestore prende un buffer dal pool e consegna, A riconosce il tag e rende il
#  buffer.
#
#  Due numeri su tre non dipendono dai cicli, ed e' il test piu' stretto della
#  serie. cntA vale 5 e si DERIVA sull'orologio logico: A arma a +2 tick, le
#  scadenze cadono ai tick 2, 4, 6, 8, 10, e la successiva non fa in tempo
#  perche' al 12o l'ISR ferma tutto. Verificato che resti 5 a 2000, 4000 e 8000
#  cicli di periodo -- si muove solo cntI. cntErr deve essere esattamente 0, e i
#  tre modi in cui puo' salire sono tre difetti diversi (timeout_arm rifiutato,
#  clientTag disallineato, buf_free che non riprende il buffer).
#
#  cntA e' anche la prova che non ci sia un leak: la classe da 16 ha dieci
#  blocchi, e un giro che ne perdesse uno per volta si fermerebbe dopo dieci
#  scadenze.
#
#  Il periodo e' 4000 cicli: sotto i 2000 il giro (tick -> gestore -> pool ->
#  mailbox -> A -> riarmo) non si chiude dentro un tick e cntA crolla a 0, che
#  e' la saturazione gia' misurata su test_chain.
# ---------------------------------------------------------------------------
#
#  "5 0 2566" dal 12/09/2026 (era "5 0 2609"): ctx_save guarda PSW_VDIRTY e spinge una
#  parola di flag anche quando non c'e' niente di vettoriale da salvare, e
#  ctx_restore la rilegge -- ~9 istruzioni per commutazione sul percorso PULITO,
#  cioe' il prezzo che i task scalari pagano perche' quelli vettoriali possano
#  esistere. cntA resta 5 e cntErr resta 0, cioe' i due numeri che non dipendono dai cicli.
# ---------------------------------------------------------------------------
vasm_object(t_tmgr SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_tmgr.vasm
                      INTERFACES vinc_queue vinc_tcb vinc_mailbox vinc_message
                                 vinc_timeout vinc_timeout_manager)

vasm_program(test_tmgr OBJECTS t_tmgr
             LINK lib_timeout_manager lib_messaggi lib_kernel lib_pool
                  lib_timeout)

vasm_check(tmgr MODE DUMPS PROGRAM test_tmgr
           EXPECT "5 0 2566")

# ---------------------------------------------------------------------------
#  test_semaphore — il semaforo contatore, e l'INSERIMENTO ORDINATO di §13.6.
#
#  E' il primo test della serie in cui il numero che conta non misura QUANTO ma
#  IN CHE ORDINE: 'ordine' e' la lista d'attesa letta con queue_peek e resa in
#  cifre. Serviva perche' un risveglio FIFO su un semaforo e' un'inversione di
#  priorita' silenziosa — nessun contatore devia, nessuna asserzione scatta, e
#  a peggiorare e' solo la latenza del task piu' importante.
#
#  Sei task, e due di loro (H e F) diventano eseguibili solo quando l'ISR li
#  desta: e' l'unico modo di farli arrivare al semaforo FUORI dall'ordine di
#  priorita', visto che lo scheduler sceglie sempre il piu' prioritario fra i
#  pronti. Con tutti eseguibili dal boot arriverebbero gia' ordinati e la
#  camminata non farebbe niente, cioe' il test non proverebbe niente.
#
#  Tutti e quattro i numeri sono DERIVABILI, nessuno dipende dai cicli — ed e'
#  il primo test della serie di cui si possa dire. Il periodo del timer non
#  entra in nessuno di loro: sposta solo QUANDO le cose accadono, non quali.
# ---------------------------------------------------------------------------
vasm_object(t_semaphore SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_semaphore.vasm
                       INTERFACES vinc_queue vinc_tcb vinc_semaphore)

vasm_program(test_semaphore OBJECTS t_semaphore LINK lib_semaphore lib_kernel)

vasm_check(semaphore MODE DUMPS PROGRAM test_semaphore
           EXPECT "123456 2 1 -6")

# ---------------------------------------------------------------------------
#  test_mutex — il priority ceiling, e le DUE storie di §13.
#
#  Un solo programma con due mutex, e non due test: il valore sta nel
#  CONFRONTO. L'ISR fa la stessa identica mossa nelle fasi 0 e 2 — rendere
#  eseguibile l'utente piu' prioritario mentre il mutex e' tenuto — e gli esiti
#  sono opposti. A cambiare e' solo la dichiarazione del ceiling, che e'
#  esattamente la tesi di §13.5: il ceiling e' una grandezza che deve essere
#  ESATTA, e il modo in cui degrada e' parte della specifica.
#
#  attOK = 0 e attKO = 1 sono lo STESSO osservabile letto sui due mutex — la
#  coda d'attesa mentre il contendente e' gia' eseguibile. E' l'anomalia a costo
#  zero che §13.5 chiede di esporre, e qui e' la misura della differenza fra i
#  due ceiling invece di due prove separate.
#
#  cntU = 1 e' la prova negativa che vale di piu': U prende mtxKO DOPO essersi
#  bloccato. Con la risposta sbagliata — riaccodarsi fra gli eseguibili e
#  riprovare — U tornerebbe subito in esecuzione essendo piu' prioritario di W,
#  riproverebbe, fallirebbe, e W non girerebbe mai. Il test non fallirebbe: non
#  FINIREBBE, ed e' la forma che il livelock prende qui dentro.
#
#  Anche qui le fasi guardano lo stato (MUTEX.owner, MUTEX.waiters) e non i tick,
#  e le due sezioni critiche non hanno durata: S e W girano su un flag che l'ISR
#  arma. Nessuno dei cinque numeri dipende dal periodo del timer.
# ---------------------------------------------------------------------------
vasm_object(t_mutex SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_mutex.vasm
                    INTERFACES vinc_queue vinc_tcb vinc_mutex vinc_mailbox)

vasm_program(test_mutex OBJECTS t_mutex LINK lib_mutex lib_messaggi lib_kernel)

vasm_check(mutex MODE DUMPS PROGRAM test_mutex
           EXPECT "0 0 1 1 1")

# ---------------------------------------------------------------------------
#  test_coop — LO SCHEDULER SENZA IL TICK (11/09/2026).
#
#  Ogni altro test di questa cartella arma il timer, e a furia di leggerli la
#  preemption da interrupt finisce per sembrare IL modo in cui il sistema
#  funziona. Non lo e': e' un modo. Qui non c'e' un solo interrupt, IE resta a
#  zero per tutta la vita del programma, e la CPU cambia mano solo per blocco
#  volontario -- un testimone (un unico MESSAGGIO) che gira A -> B -> C -> A.
#
#  Il percorso che esercita da solo esiste dal 07/09 e non era mai stato messo
#  alla prova senza rete: task_block chiama scheduler e cade nel dispatcher
#  SENZA passare dall'ISR. C'era sempre un tick a coprirlo.
#
#  LE PRIORITA' SONO DICHIARATE IN ORDINE DIVERSO DAL GIRO -- il giro e'
#  A->B->C, le priorita' sono B(1), C(2), A(3) -- ed e' il modo di rendere il
#  test sensibile: se lo scheduler scegliesse per priorita' invece di seguire il
#  testimone, l'ordine cambierebbe e `err` lo direbbe. In un sistema cooperativo
#  il pronto e' sempre uno solo, e chi sceglie non ha niente da scegliere.
#
#  L'IDLE QUI E' RAGGIUNGIBILE -- e' accodato come gli altri -- e non deve
#  essere raggiunto: senza interrupt da li' non si esce, perche' l'idle non si
#  blocca mai (§4) e nessuno puo' togliergli la CPU. Un task_ready armerebbe
#  g_resched e quel flag resterebbe armato per sempre, visto che l'unico
#  consumatore e' sched_isr_exit. In un sistema puramente cooperativo "nessuno
#  e' pronto" non significa "aspetta": significa che il sistema e' finito.
#
#  Ma il quinto numero non lo misura, e va detto: cntI nel dump NON PUO' CHE
#  VALERE 0, perche' se l'idle girasse il dump non verrebbe mai eseguito.
#  L'osservabile vero e' che il test TERMINI -- stessa forma di cntU in
#  test_mutex, dove il livelock non farebbe fallire il test ma non lo farebbe
#  finire.
#
#  I cinque numeri non dipendono dai cicli: non c'e' niente, qui, che misuri il
#  tempo.
# ---------------------------------------------------------------------------
vasm_object(t_coop SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_coop.vasm
                   INTERFACES vinc_queue vinc_tcb vinc_mailbox vinc_message)

vasm_program(test_coop OBJECTS t_coop LINK lib_messaggi lib_kernel)

vasm_check(coop MODE DUMPS PROGRAM test_coop
           EXPECT "0 5 5 5 0")

# ---------------------------------------------------------------------------
#  test_events — LO SCHEDULER GUIDATO DAL MONDO, e il primo cliente di
#  task_yield (12/09/2026).
#
#  Il gemello di test_coop, e la coppia va letta insieme: stesso kernel, stesso
#  modello cooperativo, zero interrupt, zero tick. Cambia una cosa sola -- qui
#  fuori c'e' qualcosa che puo' succedere -- e a cambiare e' chi mette in moto
#  il sistema: in test_coop il boot, qui la tastiera.
#
#  E' anche il primo programma del progetto in cui l'RTOS e un DEVICE girano
#  insieme: fino a ieri la tastiera era esercitata da tests/test_kbd.vasm, che
#  non ha ne' task ne' scheduler.
#
#  L'IDLE E' IL LETTORE, e non e' una stranezza: senza linea di interruzione la
#  tastiera si aspetta in polling, e l'unico posto dove consumare CPU aspettando
#  non toglie niente a nessuno e' l'ultimo livello. L'idle fa tre cose -- polla,
#  manda, CEDE -- e nessuna e' un'attesa bloccante.
#
#  CEDE CON task_yield, e li' sta il valore di questo test per il kernel. La
#  prima stesura faceva cedere l'idle con una receive su una mailbox di ack:
#  funzionava, e non andava bene -- un blocco toglie il TCB dalle code di ready,
#  e §4 non dice "l'idle non aspetta", dice che l'ULTIMO LIVELLO NON E' MAI
#  VUOTO, che e' cio' su cui poggia la terminazione della scansione. task_yield
#  rimette il chiamante in fondo alla coda del PROPRIO livello, quindi §4 resta
#  vera alla lettera. Con lui sparisce anche la mailbox di ack: il controllo di
#  flusso cade fuori dall'ordine.
#
#  LA SPAZIATURA DELLA TRACCIA e' l'unico parametro delicato, ed e' anche una
#  misura: fra due tasti deve starci un giro completo (I -> E -> I) piu' un
#  tratto di attesa. Il device ha un registro di ricezione solo e nessun buffer,
#  quindi il ritmo massimo che il sistema regge E' il suo giro.
#
#  Quattro numeri su cinque sono derivabili. Il quinto (cntP, i giri di polling
#  a vuoto) dipende dai cicli, ed e' il tempo in cui non c'era lavoro: in
#  test_coop il numero omologo non poteva che valere 0, perche' senza un mondo
#  esterno "nessuno e' pronto" significa che il sistema e' finito.
# ---------------------------------------------------------------------------
#  Dal 12/09 porta anche i TAG del marcatore (hal/marker.vinc): due categorie,
#  una interna all'idle e una che ATTRAVERSA task_yield. Costano 10 istruzioni
#  per giro, e si vede: cntP si sposta, gli altri quattro numeri no, perche'
#  non dipendono dai cicli. E' il prezzo dichiarato della strumentazione.
#
#  "0 5 495 5 714" dal 12/09/2026 (era "0 5 495 5 720"): ctx_save guarda PSW_VDIRTY e spinge una
#  parola di flag anche quando non c'e' niente di vettoriale da salvare, e
#  ctx_restore la rilegge -- ~9 istruzioni per commutazione sul percorso PULITO,
#  cioe' il prezzo che i task scalari pagano perche' quelli vettoriali possano
#  esistere. Quattro numeri su cinque immobili: si muove solo cntP.
# ---------------------------------------------------------------------------
vasm_object(t_events SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_events.vasm
                    INTERFACES vinc_queue vinc_tcb vinc_mailbox vinc_message
                               vinc_hal vinc_marks)

vasm_program(test_events OBJECTS t_events LINK lib_messaggi lib_kernel)

vasm_check(events MODE DUMPS PROGRAM test_events
           EXPECT "0 5 495 5 714"
           ARGS   --kbd "2000:a,6000:b,10000:c,14000:d,18000:e")

# ---------------------------------------------------------------------------
#  test_vectors — LE DUE META' DEL PROGETTO SI INCONTRANO (12/09/2026).
#
#  Il primo programma in cui un TASK usa i registri vettoriali. Fino a oggi
#  sotto rtos/ e hal/ non c'era una sola istruzione vettoriale, e i calcoli
#  vettoriali giravano senza kernel: il buco era dichiarato in machine.vasm
#  ("i registri vettoriali sono VOLATILI attraverso la preemption") e non
#  poteva manifestarsi, perche' nessun task li usava.
#
#  A e B tengono un valore in v0 attraverso un tratto interrompibile, allo
#  STESSO livello di priorita' -- quindi il tick li fa ruotare fra pari, e ogni
#  rotazione e' una commutazione con stato vettoriale vivo. Il difetto esiste
#  SOLO sotto preemption: cooperativamente i vettori si riporrebbero da soli.
#
#  errA ed errB devono essere ZERO. Disattivando il salvataggio nel simulatore
#  l'esito e' "19 0 20 19 25": A se ne accorge ogni giro tranne il primo, B mai
#  -- ed e' la FASE, non una differenza fra i due task. A rilevare il difetto e'
#  errA; errB in questa fase e' un osservabile, non un'asserzione, e il .vasm lo
#  dice invece di lasciarlo credere.
#
#  IL PERIODO DEL TIMER NON E' LIBERO: a 300 cicli il sistema LIVELLOCKA, perche'
#  una commutazione con stato vettoriale costa ~1650 cicli e il task veniva
#  ritrappato prima di eseguire un'istruzione. E' la saturazione di §3.32
#  amplificata di un ordine di grandezza -- un sistema che usa i vettori non puo'
#  tenere il tick che aveva prima.
# ---------------------------------------------------------------------------
vasm_object(t_vectors SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_vectors.vasm
                      INTERFACES vinc_queue vinc_tcb vinc_hal)

vasm_program(test_vectors OBJECTS t_vectors LINK lib_kernel)

vasm_check(vectors MODE DUMPS PROGRAM test_vectors
           EXPECT "0 0 20 19 39")
