# Nessun LINK verso lib_hal, e non e' una dimenticanza: pool.vasm non nomina un
# simbolo dell'HAL. Chiama enqueue_coda_s e dequeue_testa_s, cioe' primitive
# che la sezione critica se la chiudono dentro. L'HAL arriva per transitivita'
# attraverso lib_queue, che e' l'unico posto dove serve saperlo.
vasm_library(lib_pool
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/pool.vasm
  INTERFACES vinc_pool vinc_queue
  LINK       lib_queue)
