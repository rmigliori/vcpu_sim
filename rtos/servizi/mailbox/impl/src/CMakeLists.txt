# I messaggi sono un CLIENTE del kernel, non parte sua: chiamano task_ready e
# task_block per bloccare e risvegliare, e leggono current.
#
# ATTENZIONE, e' il debito noto: task_ready e task_block NON esistono ancora in
# scheduler.vasm -- sono .extern qui, e le uniche definizioni sono gli stub
# dentro tests/test_mailbox.vasm. Finche' §7.4 non atterra, questa libreria non
# si chiude da sola: l'unico programma che la linka e' il test, che il kernel
# se lo finge.
#
# INTERFACES non nomina vinc_messaggio: messageHandling.vasm muove i messaggi
# ma non ne legge un byte (§8.4), quindi del formato non gli serve niente. Chi
# lo dichiara e' il test, che il payload lo compone davvero.
vasm_library(lib_messaggi
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/messageHandling.vasm
  INTERFACES vinc_coda vinc_mailbox vinc_tcb
  LINK       lib_coda lib_kernel)
