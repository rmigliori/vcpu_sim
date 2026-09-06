# Il kernel LINKa le code ma NON ne dichiara l'interfaccia, ed e' il caso che
# giustifica due parole chiave invece di una lista sola: scheduler.vasm chiama
# enqueue_coda e dequeue_testa senza aver bisogno di una sola costante di
# coda.vinc. Il TCB se lo pubblica lui.
vasm_library(lib_kernel
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.vasm
  INTERFACES vinc_tcb
  LINK       lib_coda lib_hal)
