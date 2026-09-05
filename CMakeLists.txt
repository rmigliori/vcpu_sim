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
#  Le interfacce: un .vinc per fornitore (§8.6 della proposta).
# ---------------------------------------------------------------------------
set(VINC_DIR ${CMAKE_SOURCE_DIR}/linked/scheduler/include)

# L'HAL pubblica la taglia del frame di contesto e i bit della parola di stato:
# senza, finirebbero come numeri magici dentro l'HAL stesso o duplicati fuori.
vasm_interface(vinc_hal     DIR ${VINC_DIR} HEADERS hal.vinc)
vasm_interface(vinc_types   DIR ${VINC_DIR} HEADERS types.vinc)
vasm_interface(vinc_timeout DIR ${VINC_DIR} HEADERS timeout.vinc)

# pool.vinc include types.vinc: BLOCCO *e'* un nodo di lista, e una free-list e'
# una TESTA con i blocchi come nodi. E' la dipendenza fra interfacce che §5
# indicava come modellazione giusta, scrivibile da quando .include e'
# idempotente (§3.16): chi linka vinc_pool si porta dietro vinc_types.
vasm_interface(vinc_pool DIR ${VINC_DIR} HEADERS pool.vinc LINK vinc_types)

# ---------------------------------------------------------------------------
#  I moduli.
# ---------------------------------------------------------------------------
set(SCHED ${CMAKE_SOURCE_DIR}/linked/scheduler)

vasm_object(hal_machine  SOURCE ${SCHED}/hal/machine.vasm            LIBS vinc_hal)

vasm_object(k_coda       SOURCE ${SCHED}/kernel/coda.vasm            LIBS vinc_types)
vasm_object(k_scheduler  SOURCE ${SCHED}/kernel/scheduler.vasm       LIBS vinc_types)
vasm_object(k_message    SOURCE ${SCHED}/kernel/messageHandling.vasm LIBS vinc_types)
vasm_object(k_timeout    SOURCE ${SCHED}/kernel/timeout.vasm         LIBS vinc_timeout)
# pool.vasm nomina TESTA (le sei free-list sono `.res TESTA`) oltre a BLOCCO,
# quindi dichiara entrambe: include-what-you-use, anche se vinc_types
# arriverebbe comunque per transitivita' da vinc_pool.
vasm_object(k_pool       SOURCE ${SCHED}/kernel/pool.vasm            LIBS vinc_pool vinc_types)

vasm_object(demo_main    SOURCE ${SCHED}/scheduler_demo.vasm         LIBS vinc_types)

set(TESTS ${CMAKE_SOURCE_DIR}/tests)
vasm_object(t_coda     SOURCE ${TESTS}/test_coda.vasm    LIBS vinc_types)
vasm_object(t_mailbox  SOURCE ${TESTS}/test_mailbox.vasm LIBS vinc_types)
vasm_object(t_timeout  SOURCE ${TESTS}/test_timeout.vasm LIBS vinc_timeout)
vasm_object(t_pool     SOURCE ${TESTS}/test_pool.vasm    LIBS vinc_pool vinc_types)

vasm_object(multi_main  SOURCE ${CMAKE_SOURCE_DIR}/linked/multi/main.vasm)
vasm_object(multi_saxpy SOURCE ${CMAKE_SOURCE_DIR}/linked/multi/saxpy.vasm)

# ---------------------------------------------------------------------------
#  Gli eseguibili.
#
#  L'ordine degli oggetti e' quello delle pipeline scritte a mano che questi
#  target sostituiscono, e va tenuto: cambiarlo cambia il layout dell'immagine.
#  Il vincolo della migrazione e' che le invarianti diano gli stessi identici
#  numeri, quindi qui si riproduce esattamente cio' che c'era.
# ---------------------------------------------------------------------------
vasm_program(scheduler_demo OBJECTS demo_main k_scheduler k_coda hal_machine)
vasm_program(multi          OBJECTS multi_main multi_saxpy)

# Il kernel come UNA libreria, non come una lista da ricopiare in ogni test.
#
# Il linker ne pesca solo i membri che servono a chiudere i riferimenti
# (inclusione selettiva, dal commit 2111646). La chiusura non sparisce —
# machine.vo serve per irq_save/irq_restore e referenzia sched_dispatch, quindi
# si tira dietro scheduler.vo comunque — ma diventa una conseguenza del codice
# invece che una lista da mantenere in quattro intestazioni.
vasm_archive(libkernel OBJECTS k_coda k_scheduler k_message k_timeout k_pool hal_machine)

vasm_program(test_coda    OBJECTS t_coda    libkernel)
vasm_program(test_mailbox OBJECTS t_mailbox libkernel)
vasm_program(test_timeout OBJECTS t_timeout libkernel)
vasm_program(test_pool    OBJECTS t_pool    libkernel)

# ---------------------------------------------------------------------------
#  Le invarianti di regressione — §4 di docs/stato-lavori.md.
#  Da qui in avanti "verificare che nulla si sia rotto" e' `ctest`.
# ---------------------------------------------------------------------------
vasm_check(saxpy      MODE STATS EXPECT "17 40 94"
           SOURCE ${CMAKE_SOURCE_DIR}/standalone/saxpy.vasm)
vasm_check(multi      MODE STATS EXPECT "18 40 95"  PROGRAM multi)

vasm_check(scheduler_demo MODE DUMPS EXPECT "94 60" PROGRAM scheduler_demo)

vasm_check(proc       MODE DUMPS EXPECT "100 200 300"
           SOURCE ${TESTS}/test_proc.vasm)
vasm_check(include    MODE DUMPS EXPECT "16 12 16 512 1"
           IFLAGS -I${VINC_DIR} SOURCE ${TESTS}/test_include.vasm)
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
