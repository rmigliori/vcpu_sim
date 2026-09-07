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
