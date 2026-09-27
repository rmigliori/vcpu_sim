# Cliente del kernel come il semaforo -- task_block per sospendere, current per
# il guardiano -- e per la stessa ragione NON nomina lib_kernel nel LINK: chi
# costruisce il programma sceglie quale kernel. Non usa code, quindi non linka
# nemmeno lib_queue. irq_save e irq_restore arrivano dall'HAL, attraverso il
# kernel che il programma linka comunque.
vasm_library(lib_wait
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/wait.vasm
  INTERFACES vinc_wait vinc_tcb)
