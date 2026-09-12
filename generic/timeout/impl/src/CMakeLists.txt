# Come le code: l'unica dipendenza e' la sezione critica dell'HAL, obbligatoria
# perche' armare e cancellare un descrittore corre contro la scansione del tick.
#
# vinc_message dal 07/09/2026: timeout_scaduto FORMATTA il messaggio di
# scadenza nel buffer che le si porta, e il formato e' generic (§8.4). E' una
# dipendenza di sole costanti, e non intacca il criterio della cartella -- gli
# unici simboli che questo modulo externa restano irq_save e irq_restore. La
# CONSEGNA (send) sta in rtos/timeout_manager, che e' dove nomina il kernel.
vasm_library(lib_timeout
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/timeout.vasm
  INTERFACES vinc_timeout vinc_message
  LINK       lib_hal)
