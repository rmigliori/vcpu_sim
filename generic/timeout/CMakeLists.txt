# ---------------------------------------------------------------------------
#  lib_timeout — il gestore dei timeout a messaggio (§9).
#
#  Sta in generic/ per il criterio letterale: timeout.vasm externa irq_save e
#  irq_restore e nient'altro, la stessa firma esatta di coda.vasm. Non nomina
#  lo scheduler, quindi non e' rtos.
#
#  Una cosa da sapere e non da scoprire: tmo_now e' un contatore di tick, e
#  l'incremento appartiene al percorso del tick, che non e' ancora scritto.
#  Quando esistera' sara' il timer ISR a muovere quella parola -- resta un
#  legame verso l'HAL e non verso il kernel, quindi la collocazione non
#  cambia, ma questo restera' l'unico modulo di generic/ con uno stato che
#  qualcun altro scrive.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
