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
#  I tre strati, e questo file non sa altro di loro che il nome (§3.24).
#
#    hal/      lo strato che conosce la macchina, e il solo
#    generic/  tutto cio' che non usa lo scheduler: code, pool, timeout, il
#              formato dei messaggi. Si solleva in un progetto senza RTOS,
#              purche' fornisca irq_save e irq_restore
#    rtos/     cio' che USA lo scheduler: il kernel, i servizi che bloccano
#              i task, la demo
#
#  hal/ e generic/ NON stanno sotto rtos/, e il criterio e' letterale: un
#  modulo e' rtos se nomina un simbolo del kernel. Applicato ai simboli veri
#  da' una partizione senza residui, e ogni gruppo la argomenta nel proprio
#  CMakeLists invece che qui.
#
#  L'ordine e' un ordine topologico del DAG e non e' negoziabile: in CMake un
#  target dev'essere definito prima di essere nominato.
# ---------------------------------------------------------------------------
add_subdirectory(hal)

# ---------------------------------------------------------------------------
#  Il catalogo delle misure: marks.conf -> un .vinc GENERATO.
#
#  La sorgente e' marks.conf, e l'interfaccia che i programmi includono nasce
#  da li'. Cosi' i nomi che lo strumento mostra e gli .equ che il programma usa
#  sono lo STESSO file, e non due che devono restare d'accordo -- il difetto che
#  il 12/09/2026 ha fatto sparire un task intero da traccia.html.
#
#  A tempo di CONFIGURE e non con un add_custom_command: il .vinc dev'essere sul
#  disco prima che qualunque vasm_object lo dichiari fra i propri INTERFACES, e
#  un artefatto generato durante la build arriverebbe troppo tardi per il
#  DEPENDS. CMAKE_CONFIGURE_DEPENDS fa rifare il configure quando il catalogo
#  cambia, quindi il generato non resta indietro.
# ---------------------------------------------------------------------------
set(MARKS_CONF ${CMAKE_SOURCE_DIR}/marks.conf)
set(MARKS_DIR  ${VASM_BINARY_DIR}/marks)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/tools/marks.py
          generate ${MARKS_CONF} -o ${MARKS_DIR}/marks/marks.vinc
  RESULT_VARIABLE _marks_rc OUTPUT_VARIABLE _marks_out ERROR_VARIABLE _marks_err)
if(NOT _marks_rc EQUAL 0)
  message(FATAL_ERROR "marks.conf: ${_marks_err}")
endif()
message(STATUS "marks: ${_marks_out}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MARKS_CONF})

vasm_interface(vinc_marks
  DIR        ${MARKS_DIR}
  HEADERS    marks/marks.vinc
  INTERFACES vinc_hal)          # il generato include hal/marker.vinc

add_subdirectory(generic)
add_subdirectory(rtos)

# ---------------------------------------------------------------------------
#  Cio' che non e' l'RTOS: gli esempi del simulatore.
#
#  linked/multi e' l'esempio di piu' moduli linkati insieme, e resta l'unico
#  abitante di linked/ da quando linked/scheduler e' diventato tre cartelle di
#  primo livello. Insieme a standalone/ starebbe bene sotto un examples/: e'
#  aperto e non blocca.
# ---------------------------------------------------------------------------
vasm_object(multi_main  SOURCE ${CMAKE_SOURCE_DIR}/linked/multi/main.vasm)
vasm_object(multi_saxpy SOURCE ${CMAKE_SOURCE_DIR}/linked/multi/saxpy.vasm)
vasm_program(multi      OBJECTS multi_main multi_saxpy)

# ---------------------------------------------------------------------------
#  Le invarianti di regressione — §4 di docs/stato-lavori.md.
#  Da qui in avanti "verificare che nulla si sia rotto" e' `ctest`.
# ---------------------------------------------------------------------------
vasm_check(saxpy      MODE STATS EXPECT "17 40 94"
           SOURCE ${CMAKE_SOURCE_DIR}/standalone/saxpy.vasm)
vasm_check(multi      MODE STATS EXPECT "18 40 95"  PROGRAM multi)

# scheduler_demo RITIRATO il 07/09/2026 insieme alla sua invariante (97/64, poi
# 98/65): verificava l'alternanza al tick, che con lo scheduler a priorita' non
# e' piu' il comportamento atteso. Il suo posto lo prende test_scheduler, il cui
# vasm_check sta in rtos/test/ -- accanto al programma che lo produce, come tutti
# quelli nati dopo §3.24.

vasm_check(proc       MODE DUMPS EXPECT "100 200 300"
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_proc.vasm)
# test_include gira in modalita' legacy a file singolo (non ha oggetti ne'
# link), quindi i suoi -I se li scrive a mano invece di riceverli da
# INTERFACES. La lista cresce di uno a ogni libreria che si sposta, ed e' il
# vincolo che funziona: il file nomina tcb.vinc e pool.vinc, e pool.vinc tira
# dentro queue.vinc. Le cartelle si prendono dai target e non a percorso, cosi'
# seguono le librerie se si spostano ancora.
#
# Resta legacy di proposito: e' il test dell'assemblatore, e passargli i -I a
# mano e' proprio cio' che verifica. Le tre righe sono la sua asserzione.
# "20 16" dal 07/09/2026 (era "16 12"): il TCB ha guadagnato il campo pcb (§7.3),
# quindi TCB.size passa da 16 a 20 e TCB.state da +12 a +16. Il test non e'
# cambiato -- stampa gli offset che l'assembler produce -- e i due numeri nuovi
# sono la prova che li ha presi dalla dichiarazione e non da una copia.
# "24" dall'11/09/2026: il campo TCB.crit, che toglie la rotazione fra pari a
# chi tiene un mutex (tcb.vinc). TCB.state resta a +16: il campo e' in fondo.
vasm_check(include    MODE DUMPS EXPECT "24 16 16 512 1"
           IFLAGS -I$<TARGET_PROPERTY:vinc_tcb,INTERFACE_INCLUDE_DIRECTORIES>
                  -I$<TARGET_PROPERTY:vinc_pool,INTERFACE_INCLUDE_DIRECTORIES>
                  -I$<TARGET_PROPERTY:vinc_queue,INTERFACE_INCLUDE_DIRECTORIES>
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_include.vasm)
vasm_check(epsw       MODE DUMPS EXPECT "1 0 0 7"
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_epsw.vasm)

# test_kbd — il primo device del progetto, letto in polling (11/09/2026).
#
# I cinque numeri sono derivabili a mano dalla traccia: 'a'+'b'+'c' = 294, e
# 'y' = 121 e' il secondo dei due che arrivano allo stesso ciclo. Che un test
# che dipende dal MONDO sia deterministico e' il punto dell'esercizio: lo stato
# del device e chi lo riempie sono separati, e qui l'alimentatore e' una traccia
# che vive nel tempo SIMULATO. Un thread che legge stdin sara' il secondo
# alimentatore, e questo test continuera' a girare identico.
#
# La distanza fra il terzo carattere (300) e la coppia (2000) e' quella che
# rende osservabile il flag abbassato dalla lettura: senza, il caso interessante
# potrebbe non capitare mai.
vasm_check(kbd        MODE DUMPS EXPECT "0 294 0 3 121"
           IFLAGS -I$<TARGET_PROPERTY:vinc_hal,INTERFACE_INCLUDE_DIRECTORIES>
           ARGS   --kbd "100:a,200:b,300:c,2000:x,2000:y"
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_kbd.vasm)

# I 13 programmi di standalone/ devono continuare a girare da soli: qui si
# verifica che nessuno vada in errore, non cosa stampano (solo saxpy ha
# un'invariante numerica dichiarata, ed e' il test qui sopra).
file(GLOB STANDALONE ${CMAKE_SOURCE_DIR}/standalone/*.vasm)
foreach(src ${STANDALONE})
  get_filename_component(nm ${src} NAME_WE)
  add_test(NAME standalone_${nm} COMMAND vcpu_sim ${src})
endforeach()
