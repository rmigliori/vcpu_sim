# ---------------------------------------------------------------------------
#  lib_semaphore — il semaforo contatore: prendere e restituire risorse, e
#  bloccare chi arriva quando non ce ne sono.
#
#  Stessa forma della mailbox, che e' quella che services/ dichiara: un
#  interface/ col TIPO e un impl/ col codice. Il tipo e' un .equ derivato da
#  TESTA, non una .struct propria (§3.27), e il .vinc dice perche'.
#
#  Non c'e' un semaforo_api.vinc, come non c'e' per la mailbox: le tre entry
#  sono simboli che viaggiano nel .va, e chi le chiama le dichiara .extern. Il
#  giorno che nascesse, sarebbe per pubblicare il contratto di chiamata — quali
#  registri sporcano, e soprattutto che sem_wait puo' bloccare e sem_post no.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
