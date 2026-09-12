# ---------------------------------------------------------------------------
#  messaggio — SOLA INTERFACCIA: nessun impl/, e non e' un ramo mancante.
#
#  Il formato di un messaggio e' una convenzione fra chi manda e chi riceve, e
#  non ha un'implementazione: non c'e' un byte da assemblare. Sta in generic/
#  perche' dipende dalle sole code (un messaggio E' un nodo di lista, una
#  mailbox E' una TESTA) e non nomina lo scheduler.
#
#  Il SERVIZIO che muove i messaggi e' un'altra cosa e sta altrove:
#  messageHandling.vasm chiama task_ready e task_block, quindi usa lo
#  scheduler, quindi e' rtos. E' la distinzione che ha diviso in due quella che
#  sembrava una libreria sola -- e che tornera' identica per semafori e mutex,
#  formato generico piu' servizio che blocca.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
