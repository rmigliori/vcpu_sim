cmake_minimum_required(VERSION 3.20)
project(vcpu_sim C)

# ---------------------------------------------------------------------------
#  vcpu_sim — simulatore di CPU vettoriale + toolchain, e i sorgenti .vasm
#  che ci girano sopra decomposti in target.
#
#  Il Makefile continua a esistere e a costruire il solo eseguibile C in
#  build/: i due build non si pestano i piedi finche' CMake sta in un'altra
#  cartella (`cmake -B out`, vedi docs/manual.md §2.2).
#
#  Quello che si ottiene qui e non li':
#    - i .vinc sono librerie di INTERFACCIA, quindi la cartella degli include
#      viaggia con la dipendenza invece di essere riscritta in ogni comando;
#    - le pipeline di asm/ld non stanno piu' scritte a mano in un commento
#      nell'intestazione di ogni test;
#    - le invarianti di regressione (§4 dell'handoff) sono test di ctest.
# ---------------------------------------------------------------------------

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- la toolchain in C ------------------------------------------------------
#
# Un solo target, come il Makefile. Dividere anche il C in librerie richiede
# prima di sistemare i confini degli header — assemble() e' dichiarata in
# vcpu.h, assemble_object() in toolchain.h, ma stanno tutte e due in
# assembler.c, che un header proprio non ce l'ha (§5 dell'handoff).
add_executable(vcpu_sim
  src/vcpu.c
  src/toolchain.c
  src/assembler.c
  src/main.c)
target_include_directories(vcpu_sim PRIVATE include)
target_link_libraries(vcpu_sim PRIVATE m)

include(cmake/vasm.cmake)
enable_testing()

# ---------------------------------------------------------------------------
#  Le librerie che hanno gia' una cartella propria.
#
#  Ristrutturazione in corso (§3.24 dell'handoff): una libreria alla volta
#  passa da una dichiarazione qui sotto a una cartella con impl/src e
#  interface/<nome>, e la sua descrizione di build va a vivere con lei. Cio'
#  che resta piu' in basso in questo file e' quello che non e' ancora passato.
#
#  L'ordine e' un ordine topologico del DAG e non e' negoziabile: in CMake un
#  target dev'essere definito prima di essere nominato.
# ---------------------------------------------------------------------------
add_subdirectory(hal)
add_subdirectory(generic)
add_subdirectory(rtos)

# ---------------------------------------------------------------------------
#  I programmi.
#
#  Ognuno dichiara la libreria che usa, non la lista dei moduli da linkare: la
#  chiusura la calcola vasm_program. Prima quella lista stava scritta a mano
#  nell'intestazione di ogni test, e andava tenuta aggiornata a occhio.
# ---------------------------------------------------------------------------
vasm_object(multi_main  SOURCE ${CMAKE_SOURCE_DIR}/linked/multi/main.vasm)
vasm_object(multi_saxpy SOURCE ${CMAKE_SOURCE_DIR}/linked/multi/saxpy.vasm)
vasm_program(multi      OBJECTS multi_main multi_saxpy)

set(TESTS ${CMAKE_SOURCE_DIR}/tests)
vasm_object(t_coda     SOURCE ${TESTS}/test_coda.vasm    INTERFACES vinc_coda)
vasm_object(t_mailbox  SOURCE ${TESTS}/test_mailbox.vasm INTERFACES vinc_coda vinc_tcb vinc_messaggio)
vasm_object(t_timeout  SOURCE ${TESTS}/test_timeout.vasm INTERFACES vinc_timeout)
vasm_object(t_pool     SOURCE ${TESTS}/test_pool.vasm    INTERFACES vinc_pool vinc_coda)

vasm_program(test_coda    OBJECTS t_coda    LINK lib_coda)
vasm_program(test_mailbox OBJECTS t_mailbox LINK lib_messaggi)
vasm_program(test_timeout OBJECTS t_timeout LINK lib_timeout)
vasm_program(test_pool    OBJECTS t_pool    LINK lib_pool)

# ---------------------------------------------------------------------------
#  Le invarianti di regressione — §4 di docs/stato-lavori.md.
#  Da qui in avanti "verificare che nulla si sia rotto" e' `ctest`.
# ---------------------------------------------------------------------------
vasm_check(saxpy      MODE STATS EXPECT "17 40 94"
           SOURCE ${CMAKE_SOURCE_DIR}/standalone/saxpy.vasm)
vasm_check(multi      MODE STATS EXPECT "18 40 95"  PROGRAM multi)

vasm_check(scheduler_demo MODE DUMPS EXPECT "97 64" PROGRAM scheduler_demo)

vasm_check(proc       MODE DUMPS EXPECT "100 200 300"
           SOURCE ${TESTS}/test_proc.vasm)
# test_include gira in modalita' legacy a file singolo (non ha oggetti ne'
# link), quindi i suoi -I se li scrive a mano invece di riceverli da
# INTERFACES. La lista cresce di uno a ogni libreria che si sposta, ed e' il
# vincolo che funziona: il file nomina tcb.vinc e pool.vinc, e pool.vinc tira
# dentro coda.vinc. Le cartelle si prendono dai target e non a percorso, cosi'
# seguono le librerie se si spostano ancora.
#
# DA REGOLARIZZARE quando i test avranno una cartella loro: diventa un
# vasm_object con INTERFACES e questa lista sparisce.
vasm_check(include    MODE DUMPS EXPECT "16 12 16 512 1"
           IFLAGS -I$<TARGET_PROPERTY:vinc_tcb,INTERFACE_INCLUDE_DIRECTORIES>
                  -I$<TARGET_PROPERTY:vinc_pool,INTERFACE_INCLUDE_DIRECTORIES>
                  -I$<TARGET_PROPERTY:vinc_coda,INTERFACE_INCLUDE_DIRECTORIES>
           SOURCE ${TESTS}/test_include.vasm)
vasm_check(epsw       MODE DUMPS EXPECT "1 0 0 7"
           SOURCE ${TESTS}/test_epsw.vasm)

vasm_check(mailbox MODE DUMPS PROGRAM test_mailbox
           EXPECT "0 1 2 11 22 0 33 0 0")
vasm_check(coda    MODE DUMPS PROGRAM test_coda
           EXPECT "0 1 1 1 0 0 0 2 1 0 0 0 0")
vasm_check(timeout MODE DUMPS PROGRAM test_timeout
           EXPECT "3 0 150 1 2 0 0 0 3 0 1")
vasm_check(pool    MODE DUMPS PROGRAM test_pool
           EXPECT "10 4 0 0 9 0 32 3 2 0 0 4 4 4 3 0 0 1 4")

# I 13 programmi di standalone/ devono continuare a girare da soli: qui si
# verifica che nessuno vada in errore, non cosa stampano (solo saxpy ha
# un'invariante numerica dichiarata, ed e' il test qui sopra).
file(GLOB STANDALONE ${CMAKE_SOURCE_DIR}/standalone/*.vasm)
foreach(src ${STANDALONE})
  get_filename_component(nm ${src} NAME_WE)
  add_test(NAME standalone_${nm} COMMAND vcpu_sim ${src})
endforeach()
