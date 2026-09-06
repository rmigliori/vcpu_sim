# Come le code: l'unica dipendenza e' la sezione critica dell'HAL, obbligatoria
# perche' armare e cancellare un descrittore corre contro la scansione del tick.
vasm_library(lib_timeout
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/timeout.vasm
  INTERFACES vinc_timeout
  LINK       lib_hal)
