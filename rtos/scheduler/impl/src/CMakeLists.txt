# Fino al 07/09/2026 questo era IL caso che giustificava due parole chiave
# invece di una lista sola: il kernel LINKava le code senza dichiararne
# l'interfaccia, perche' scheduler.vasm chiama enqueue_tail e dequeue_head
# senza aver bisogno di una sola costante di queue.vinc.
#
# Non e' piu' vero, e la ragione e' la divisione di queue.vinc in tipi + _api:
# ora l'interfaccia pubblica anche le ENTRY, quindi chi CHIAMA include. Da cui
# vinc_queue qui sotto.
#
# La distinzione fra le due parole chiave resta viva, ma sui link INDIRETTI:
# lib_hal e' linkata (ctx_restore) e vinc_hal non e' dichiarata, perche'
# l'HAL non ha ancora il suo _api. Il giorno che ce l'abbia, LINK e INTERFACES
# coincideranno per ogni chiamante diretto e differiranno solo per cio' che si
# linka per conto di qualcun altro.
#
# CONFIGS marks: lo stesso sorgente produce anche lib_kernel_marks, in cui i
# `.ifdef MARKS` di scheduler.vasm entrano. Un programma ne linka UNA -- metterle
# entrambe sulla stessa riga di `ld` e' un errore rumoroso, "duplicate global
# 'pcb0'". Cosa significhi "marks" lo dice `vasm_config` nel CMakeLists di primo
# livello, in un posto solo.
vasm_library(lib_kernel
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.vasm
  INTERFACES vinc_tcb vinc_queue
  LINK       lib_queue lib_hal
  CONFIGS    marks)
