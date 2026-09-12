# ---------------------------------------------------------------------------
#  servizi — i clienti del kernel: chi blocca e risveglia i task.
#
#  Un servizio si riconosce da una firma sola: chiama task_ready e task_block.
#  Non e' parte del kernel (§3.22) e non e' generic (usa lo scheduler), quindi
#  ha un livello suo.
#
#  Dal 10/09/2026 sono tre, e la nota che stava qui ("semafori e mutex
#  nasceranno qui accanto") ha indovinato il posto. I tre punti di blocco del
#  sistema sono questi e nient'altro, e la differenza fra loro non e' di
#  implementazione ma di COSA STAI ASPETTANDO:
#
#    mailbox   il MONDO, che puo' non rispondere      -> vuole un orologio (§9)
#    semaforo  una RISORSA che un altro task qui
#              dentro restituira' entro un tempo
#              calcolabile                            -> nessun timeout (§13.8)
#    mutex     nessuno: sotto un ceiling corretto
#              non ci si blocca mai, e la coda esiste
#              solo per far DEGRADARE un ceiling
#              dichiarato male                        -> §13.5
# ---------------------------------------------------------------------------
add_subdirectory(mailbox)
add_subdirectory(semaphore)
add_subdirectory(mutex)
