# ---------------------------------------------------------------------------
#  lib_kernel — SOLO gli algoritmi di gestione dei task.
#
#  Scheduler, dispatcher, uscita da ISR. Mailbox, semafori e mutex NON stanno
#  qui: sono clienti che lo chiamano, e vivono in services/.
#
#  La cartella dell'interfaccia si chiama tcb/ e non scheduler/: il nome che
#  conta e' quello del file da includere, e chi ha bisogno del kernel ha
#  bisogno del TCB. Un secondo header del kernel nascerebbe qui accanto.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
