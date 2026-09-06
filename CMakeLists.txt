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

# Le due foglie: non dipendono da nessuno.
#   hal.vinc     la taglia del frame di contesto e i bit della parola di stato
#   coda.vinc    il tipo lista e gli esiti: il .vinc piu' basso e piu' incluso
vasm_interface(vinc_hal  DIR ${VINC_DIR} HEADERS hal.vinc)
vasm_interface(vinc_coda DIR ${VINC_DIR} HEADERS coda.vinc)

# timeout.vinc e' foglia per una proprieta' del modello, non per un limite: il
# DESCRITTORE non entra mai in una lista (§9.3), quindi non ha niente da
# chiedere alle code.
vasm_interface(vinc_timeout DIR ${VINC_DIR} HEADERS timeout.vinc)

# I tre che dipendono da coda.vinc, e sempre per la stessa ragione: TCB,
# MESSAGGIO e BLOCCO sono tutti NODI DI LISTA. Chi dichiara uno di questi in
# INTERFACES si porta dietro vinc_coda senza saperlo -- e' la propagazione
# transitiva FRA INTERFACCE, l'unica che ci sia: attraverso un arco fra librerie
# i -I non passano, altrimenti dichiarare LINK basterebbe a includere. Prima
# non si poteva nemmeno dichiarare, perche' .include non era idempotente e
# un unico types.vinc teneva insieme code, task e messaggi (§12.6, passo 4).
vasm_interface(vinc_tcb       DIR ${VINC_DIR} HEADERS tcb.vinc       INTERFACES vinc_coda)
vasm_interface(vinc_messaggio DIR ${VINC_DIR} HEADERS messaggio.vinc INTERFACES vinc_coda)
vasm_interface(vinc_pool      DIR ${VINC_DIR} HEADERS pool.vinc      INTERFACES vinc_coda)

# ---------------------------------------------------------------------------
#  Le librerie.
#
#  Sei librerie e un grafo che e' un DAG: tutte le frecce vanno verso l'HAL e
#  nessun percorso torna indietro. Fino al 05/09/2026 non era cosi' -- l'HAL
#  nominava sched_dispatch e il kernel nominava ctx_restore, cioe' un ciclo
#  (§12.1 della proposta). Ora `ld machine.vo` da solo si chiude, ed e' quella
#  la prova che libhal e' davvero lo strato piu' basso.
#
#      messaggi ──▶ kernel ──▶ coda ──▶ hal
#          │           │                 ▲
#          └───────────┴─────────────────┘
#                     pool ──▶ coda
#                  timeout ──────────────▶ hal
# ---------------------------------------------------------------------------
set(SCHED ${CMAKE_SOURCE_DIR}/linked/scheduler)

# L'HAL: zero dipendenze, e per un HAL e' la proprieta' che conta.
vasm_library(lib_hal     SOURCES ${SCHED}/hal/machine.vasm  INTERFACES vinc_hal)

# Le code dipendono dall'HAL, ma solo per lo strato _s: i wrapper protetti
# chiamano irq_save/irq_restore. Le primitive raw e lo strato _nc no -- il
# giorno che servisse, il confine per dividerla in due passa di li'.
vasm_library(lib_coda    SOURCES ${SCHED}/kernel/coda.vasm  INTERFACES vinc_coda
                         LINK lib_hal)

# Il kernel: SOLO gli algoritmi di gestione dei task (scheduler, dispatcher,
# uscita da ISR). Mailbox, semafori e mutex NON stanno qui: sono clienti che lo
# chiamano.
vasm_library(lib_kernel  SOURCES ${SCHED}/kernel/scheduler.vasm INTERFACES vinc_tcb
                         LINK lib_coda lib_hal)

vasm_library(lib_pool    SOURCES ${SCHED}/kernel/pool.vasm  INTERFACES vinc_pool vinc_coda
                         LINK lib_coda)

vasm_library(lib_timeout SOURCES ${SCHED}/kernel/timeout.vasm INTERFACES vinc_timeout
                         LINK lib_hal)

# I messaggi sono un CLIENTE del kernel, non parte sua: chiamano task_ready /
# task_block per bloccare e risvegliare. Semafori e mutex nasceranno qui
# accanto, allo stesso livello.
vasm_library(lib_messaggi SOURCES ${SCHED}/kernel/messageHandling.vasm
                          INTERFACES vinc_coda vinc_tcb
                          LINK lib_coda lib_kernel)

# ---------------------------------------------------------------------------
#  I programmi.
#
#  Ognuno dichiara la libreria che usa, non la lista dei moduli da linkare: la
#  chiusura la calcola vasm_program. Prima quella lista stava scritta a mano
#  nell'intestazione di ogni test, e andava tenuta aggiornata a occhio.
# ---------------------------------------------------------------------------
vasm_object(demo_main   SOURCE ${SCHED}/scheduler_demo.vasm INTERFACES vinc_tcb)
vasm_program(scheduler_demo OBJECTS demo_main LINK lib_kernel)

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
