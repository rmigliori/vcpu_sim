# I messaggi sono un CLIENTE del kernel, non parte sua: chiamano task_ready e
# task_block per bloccare e risvegliare, e leggono current.
#
# task_ready e task_block ESISTONO dal 07/09/2026 (§8.8), e da allora questa
# libreria NON nomina piu' lib_kernel nel LINK. Non e' una dipendenza persa: e'
# che una libreria non deve scegliere quale implementazione del kernel useranno
# i suoi clienti. I due simboli restano .extern -- il contratto -- e chi
# costruisce il programma decide chi li fornisce:
#
#   test_mailbox      i propri stub, perche' e' un test UNITARIO della mailbox:
#                     lo stub di task_block esegue la send che ci sveglia e
#                     ritorna, invece di commutare davvero
#   la simulazione    lib_kernel, cioe' il context switch vero
#
# Finche' il LINK era cablato qui, le due cose non potevano coesistere: il
# linker vedeva due definizioni di task_block e si fermava.
#
# INTERFACES non nomina vinc_messaggio: messageHandling.vasm muove i messaggi
# ma non ne legge un byte (§8.4), quindi del formato non gli serve niente. Chi
# lo dichiara e' il test, che il payload lo compone davvero.
vasm_library(lib_messaggi
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/messageHandling.vasm
  INTERFACES vinc_coda vinc_mailbox vinc_tcb
  LINK       lib_coda)
