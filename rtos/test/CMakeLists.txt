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
                      INTERFACES vinc_coda vinc_tcb vinc_messaggio)

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
# ---------------------------------------------------------------------------
vasm_object(t_scheduler SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_scheduler.vasm
                        INTERFACES vinc_coda vinc_tcb)

vasm_program(test_scheduler OBJECTS t_scheduler LINK lib_kernel)

vasm_check(scheduler MODE DUMPS PROGRAM test_scheduler
           EXPECT "74 5 10 59")

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
vasm_object(t_block SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_block.vasm
                    INTERFACES vinc_coda vinc_tcb vinc_mailbox)

vasm_program(test_block OBJECTS t_block LINK lib_messaggi lib_kernel)

vasm_check(block MODE DUMPS PROGRAM test_block
           EXPECT "3 28")

# ---------------------------------------------------------------------------
#  test_catena — la catena A -> B -> C: due salti di mailbox fatti da TASK.
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
vasm_object(t_catena SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_catena.vasm
                     INTERFACES vinc_coda vinc_tcb vinc_mailbox)

vasm_program(test_catena OBJECTS t_catena LINK lib_messaggi lib_kernel)

vasm_check(catena MODE DUMPS PROGRAM test_catena
           EXPECT "3 3 3 778")

# ---------------------------------------------------------------------------
#  test_gestore — il gestore dei timeout come TASK: l'ultimo passo di §3.28.
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
#  e' la saturazione gia' misurata su test_catena.
# ---------------------------------------------------------------------------
vasm_object(t_gestore SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_gestore.vasm
                      INTERFACES vinc_coda vinc_tcb vinc_mailbox vinc_messaggio
                                 vinc_timeout vinc_gestore_timeout)

vasm_program(test_gestore OBJECTS t_gestore
             LINK lib_gestore_timeout lib_messaggi lib_kernel lib_pool
                  lib_timeout)

vasm_check(gestore MODE DUMPS PROGRAM test_gestore
           EXPECT "5 0 2609")
