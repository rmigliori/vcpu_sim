# L'HAL: zero dipendenze, e per un HAL e' la proprieta' che conta.
#
# Nessun LINK, e non e' perche' non serve ancora: e' la definizione dello strato
# piu' basso. Il giorno che comparisse qui un LINK verso qualcosa di questo
# progetto, l'HAL avrebbe smesso di essere un HAL.
vasm_library(lib_hal
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/machine.vasm
  INTERFACES vinc_hal)
