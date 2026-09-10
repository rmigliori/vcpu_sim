# Il mutex e' un CLIENTE del kernel come mailbox e semaforo, e ne chiama un
# simbolo in piu' degli altri due: prio_pcb, per convertire una volta sola il
# LIVELLO del ceiling nell'indirizzo di PCB che il runtime confronta. Resta
# .extern come gli altri — una libreria non sceglie quale implementazione del
# kernel useranno i suoi clienti.
#
# request_preempt e' l'altro simbolo che la mailbox non nomina: mutex_unlock e'
# un punto di preemption (§13.7), perche' abbassando la priorita' dal ceiling
# puo' diventare eseguibile qualcuno che durante la sezione critica non poteva.
vasm_library(lib_mutex
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/mutex.vasm
  INTERFACES vinc_coda vinc_mutex vinc_tcb
  LINK       lib_coda)
