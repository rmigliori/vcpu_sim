# lib_gestore_timeout — il task, e le tre librerie che compone.
#
# E' il modulo con piu' dipendenze del progetto, ed e' inevitabile: un task di
# sistema STA nel punto in cui le parti si incontrano. Il pool per il buffer, i
# messaggi per mandarlo, il timeout per il vettore delle scadenze.
#
# Come lib_messaggi, NON nomina lib_kernel nel LINK pur essendo un task: i
# simboli del kernel che gli servono arrivano attraverso lib_messaggi (receive
# chiama task_block), e chi costruisce il programma decide chi li fornisce.
vasm_library(lib_gestore_timeout
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/gestore_timeout.vasm
  INTERFACES vinc_queue vinc_mailbox vinc_message vinc_pool vinc_timeout
             vinc_gestore_timeout
  LINK       lib_messaggi lib_pool lib_timeout)
