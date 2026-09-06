# Le code dipendono dall'HAL, ma SOLO per lo strato _s: i wrapper protetti
# chiamano irq_save/irq_restore. Le primitive raw e lo strato _nc no -- il
# giorno che servisse dividere questa libreria in due, il confine passa di li'.
#
# E' l'unica dipendenza che generic/ ha verso il basso, ed e' un contratto di
# due simboli: e' questo che rende la cartella sollevabile altrove.
vasm_library(lib_coda
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/coda.vasm
  INTERFACES vinc_coda
  LINK       lib_hal)
