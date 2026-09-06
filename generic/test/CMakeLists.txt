# ---------------------------------------------------------------------------
#  I programmi che verificano generic/.
#
#  Sono APPLICAZIONI, non pezzi delle librerie, e stanno in cima al DAG: ogni
#  simbolo che nominano e' .global nel fornitore, quindi nessuno di loro chiede
#  un accesso che la libreria non pubblichi. E' il criterio che decide dove va
#  un test -- dentro la libreria solo se gli serve vedere cio' che non e'
#  pubblicato, fuori altrimenti -- e qui vale per tutti e tre.
#
#  Stanno pero' DENTRO generic/, non in un tests/ comune, perche' generic/ deve
#  potersi sollevare in un altro progetto: portarsela via senza le sue verifiche
#  vorrebbe dire portarsi via meno di quello che c'e'. Ne segue il vincolo che
#  ha fatto riscrivere test_coda prima di cominciare: un test di generic/ non
#  puo' nominare un header di rtos/, altrimenti generic/ non e' sollevabile.
#
#  I valori attesi stanno qui, accanto al programma che li produce, e in un
#  posto solo: e' `ctest` a confrontarli, non chi legge.
# ---------------------------------------------------------------------------
vasm_object(t_coda    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_coda.vasm
                      INTERFACES vinc_coda)
vasm_object(t_pool    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_pool.vasm
                      INTERFACES vinc_pool vinc_coda)
vasm_object(t_timeout SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_timeout.vasm
                      INTERFACES vinc_timeout)

vasm_program(test_coda    OBJECTS t_coda    LINK lib_coda)
vasm_program(test_pool    OBJECTS t_pool    LINK lib_pool)
vasm_program(test_timeout OBJECTS t_timeout LINK lib_timeout)

vasm_check(coda    MODE DUMPS PROGRAM test_coda
           EXPECT "0 1 1 1 0 0 0 2 1 0 0 0 0")
vasm_check(pool    MODE DUMPS PROGRAM test_pool
           EXPECT "10 4 0 0 9 0 32 3 2 0 0 4 4 4 3 0 0 1 4")
vasm_check(timeout MODE DUMPS PROGRAM test_timeout
           EXPECT "3 0 150 1 2 0 0 0 3 0 1")
