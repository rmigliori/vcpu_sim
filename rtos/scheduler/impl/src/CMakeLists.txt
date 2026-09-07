# Fino al 07/09/2026 questo era IL caso che giustificava due parole chiave
# invece di una lista sola: il kernel LINKava le code senza dichiararne
# l'interfaccia, perche' scheduler.vasm chiama enqueue_coda e dequeue_testa
# senza aver bisogno di una sola costante di coda.vinc.
#
# Non e' piu' vero, e la ragione e' la divisione di coda.vinc in tipi + _api:
# ora l'interfaccia pubblica anche le ENTRY, quindi chi CHIAMA include. Da cui
# vinc_coda qui sotto.
#
# La distinzione fra le due parole chiave resta viva, ma sui link INDIRETTI:
# lib_hal e' linkata (ctx_restore) e vinc_hal non e' dichiarata, perche'
# l'HAL non ha ancora il suo _api. Il giorno che ce l'abbia, LINK e INTERFACES
# coincideranno per ogni chiamante diretto e differiranno solo per cio' che si
# linka per conto di qualcun altro.
vasm_library(lib_kernel
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.vasm
  INTERFACES vinc_tcb vinc_coda
  LINK       lib_coda lib_hal)
