# Il semaforo e' un CLIENTE del kernel esattamente come la mailbox: chiama
# task_ready e task_block per bloccare e risvegliare, e legge current. Per la
# stessa ragione NON nomina lib_kernel nel LINK — una libreria non deve
# scegliere quale implementazione del kernel useranno i suoi clienti. I tre
# simboli restano .extern (il contratto) e chi costruisce il programma decide
# chi li fornisce.
#
# INTERFACES nomina vinc_tcb perche' qui i TCB si guardano davvero, e non solo
# per passarli al kernel: la camminata di §13.6 legge TCB.pcb per confrontare le
# priorita'. E' la differenza con lib_messaggi, che di un TCB non legge nessun
# campo tranne lo state.
vasm_library(lib_semaforo
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/semaforo.vasm
  INTERFACES vinc_queue vinc_semaforo vinc_tcb
  LINK       lib_queue)
