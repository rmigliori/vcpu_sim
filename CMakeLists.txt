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
#
#  E DAL 14/09/2026 L'HAL ENTRA IN DUE TEMPI (§3.63), perche' il DAG lo impone:
#
#      hal/interface   il porto del marcatore (marker.vinc)
#          |                  il catalogo generato lo include
#      vinc_marks  ->  vasm_config(marks)
#          |                  ...e da li' prende il -D MARKS
#      hal/impl        ctx_restore, che ora porta un tag sotto .ifdef
#
#  Non e' un ciclo: l'INTERFACCIA dell'HAL sta sotto al catalogo,
#  l'IMPLEMENTAZIONE sopra. Finche' l'HAL non aveva tag le due meta' potevano
#  entrare insieme, e infatti `add_subdirectory(hal)` le aggiungeva tutte e
#  due. Adesso no, e scriverlo qui e' l'unico modo di far vedere che sono due
#  posizioni diverse dello stesso ordine topologico -- la regola che
#  hal/CMakeLists.txt gia' dichiarava per conto suo: «l'interfaccia viene
#  prima, sempre».
# ---------------------------------------------------------------------------
add_subdirectory(hal/interface)

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

# ---------------------------------------------------------------------------
#  La configurazione STRUMENTATA (13/09/2026, §3.47).
#
#  Un nome solo per cio' che significa "con il marcatore dentro", dichiarato
#  qui e nominato da ogni libreria che ha dei tag. Le due cose viaggiano in
#  coppia e non e' un caso: il sorgente strumentato include il catalogo
#  generato, che include hal/marker.vinc. Ripeterle libreria per libreria
#  sarebbe la ripetizione che marks.conf esiste per togliere, un piano piu' su.
#
#  Perche' a COMPILE-TIME e non con l'interruttore a runtime (--marks): con
#  quello le istruzioni dei tag si eseguono comunque -- e' il registratore a
#  tacere, non il programma -- quindi i valori attesi che dipendono dai cicli
#  non tornerebbero mai piu'. Misurato: bastano DICIOTTO istruzioni in tutto un
#  programma per spostarne uno.
# ---------------------------------------------------------------------------
vasm_config(marks DEFINES MARKS INTERFACES vinc_marks)

# L'altra meta' dell'HAL: qui e non prima, perche' lib_hal dichiara
# `CONFIGS marks` e quel target nasce nella riga sopra. Vedi il riquadro in
# testa a `add_subdirectory(hal/interface)`.
add_subdirectory(hal/impl)

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

# .macro/.endmacro (14/09/2026, §3.65). Cinque numeri tutti DERIVABILI a mano, e
# non e' pignoleria: un test del linguaggio che dipendesse dai cicli
# misurerebbe la macchina invece dell'assembler. Il quinto (42) e' quello che
# conta di piu' -- dice che una macro dal corpo VUOTO si espande in zero
# istruzioni, che e' la ragione per cui il marcatore usa macro e non procedure.
vasm_check(macro      MODE DUMPS EXPECT "15 25 75 35 42"
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_macro.vasm)
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

# test_due_irq — LA SECONDA SORGENTE DI INTERRUZIONE (14/09/2026, §3.67).
#
# Fino a oggi la macchina ne aveva UNA, il timer, e la tastiera si interrogava.
# Adesso anche il tasto interrompe, e il vettore scopre chi e' stato leggendo la
# CAUSA -- come mcause su RISC-V, non un vettore per sorgente.
#
# I tre numeri sono derivabili a mano e dicono quattro cose:
#   65  'A' preso col POLLING, perche' arriva a 200 cicli quando la tastiera
#       non e' ancora armata: NASCE SPENTA, ed e' cio' che impedisce a
#       test_events -- che la interroga senza installare un vettore -- di
#       finire su un handler che non c'e';
#   66  'B' preso dall'ISR: l'interrupt scatta davvero, e la causa lo distingue
#       dal timer che nel frattempo batte;
#    1  una sola trap del tasto, cioe' l'ISR ha LETTO KBD_DATA. Il flag lo
#       abbassa la lettura e non la trap: un'ISR che non legge se la ritrova
#       subito, e il contatore sarebbe esploso.
#
# Ed e' il primo programma del progetto la cui ISR torna con una `reti` SECCA,
# senza commutare niente: una delle tre uscite che §12.4 prevede, e che fino a
# ieri esisteva solo nella documentazione.
#
# A file singolo e senza HAL ne' kernel, di proposito: qui si prova la MACCHINA,
# e metterci in mezzo il kernel vorrebbe dire provare due cose insieme senza
# sapere quale ha ceduto.
vasm_check(due_irq    MODE DUMPS EXPECT "65 66 1"
           IFLAGS -I$<TARGET_PROPERTY:vinc_hal,INTERFACE_INCLUDE_DIRECTORIES>
           ARGS --kbd "200:A,600:B"
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_due_irq.vasm)

# test_ifdef — l'assemblaggio condizionale (13/09/2026, §3.44).
#
# DUE RIGHE, UN SORGENTE SOLO, ed e' il punto: un test solo proverebbe che il
# filtro fa qualcosa, non che SCEGLIE. La differenza fra le due sequenze attese
# e' tutta prodotta dal `-D MARKS` della seconda.
#
# Il -D sta in IFLAGS e non in ARGS perche' le due cose vanno in due momenti
# diversi, come gli -I: questo lo vuole l'ASSEMBLER, ARGS lo vuole la macchina.
# Qui la distinzione non e' una sottigliezza — e' l'intera cosa in prova.
#
# Sta in modalita' legacy a file singolo di proposito, come test_include: il
# soggetto e' l'assembler, e il percorso `asm`/`ld` passa per lo stesso filtro
# (inc_next_line e' unico per costruzione), quindi linkarlo non proverebbe
# niente di piu'.
vasm_check(ifdef_off  MODE DUMPS EXPECT "2 21 1 222 8"
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_ifdef.vasm)
vasm_check(ifdef_on   MODE DUMPS EXPECT "1 11 0 7 111 8"
           IFLAGS -D MARKS
           SOURCE ${CMAKE_SOURCE_DIR}/tests/test_ifdef.vasm)

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

# ---------------------------------------------------------------------------
#  La PAGINA di tools/trace.py, eseguita (13/09/2026, §3.49).
#
#  Era l'unico pezzo del progetto che si spediva LETTO invece che provato, e
#  gli e' costato quattro difetti -- fra cui uno che faceva sparire un task
#  intero dal disegno. Dal 12/09 c'e' il modo di eseguirla (gjs piu' il DOM
#  finto di tools/trace_dom.js), ma era una RICETTA in testa a un file, cioe'
#  una verifica che si esegue a mano -- e una verifica che si esegue a mano e'
#  una che qualcuno prima o poi non esegue. §4 dice esattamente questo delle
#  invarianti prima che diventassero ctest.
#
#  SU PIU' PROGRAMMI, e non e' zelo: i difetti di quella pagina sono tutti
#  della forma "funziona su quello per cui e' stata scritta". I cinque qui
#  sotto sono scelti per FORMA, non per copertura:
#     tmgr_marks  strumentato: marcatore, finestre e preemption
#     coop        nessun interrupt, IE = 0 per tutta la vita: niente tick
#     vectors     il piu' lungo, e con due task vettoriali
#     multi       nessun kernel: niente task, niente ISR, solo il boot
#     events      l'unico con gli EVENTI PUNTUALI della macchina (14/09/2026)
#
#  L'ULTIMO E' ARRIVATO TARDI, e vale la pena dire perche'. Il 14/09 le corsie
#  del marcatore sono entrate nel diagramma principale (§3.59), e fra queste
#  quella della macchina -- che nessuno dei quattro programmi di sopra produce.
#  Cioe': una corsia nuova, disegnata da codice nuovo, e nessun test che la
#  vedesse. Esattamente la forma di difetto che questa serie esiste per
#  prendere, ricreata mentre la si correggeva.
#
#  Vuole il suo ALIMENTATORE: test_events e' guidato dalla tastiera e senza i
#  tasti non termina (§3.39). Le opzioni della macchina vanno dopo un `--`.
#
#  OPZIONALE: senza gjs questi test non esistono, e ctest resta verde. Meglio
#  un test che manca di un test che fallisce per l'ambiente.
# ---------------------------------------------------------------------------
find_program(GJS_EXE gjs)
if(GJS_EXE)
  set(_tc ${CMAKE_SOURCE_DIR}/tools/trace_check.sh)
  foreach(p tmgr_marks coop vectors)
    add_test(NAME trace_${p}
             COMMAND ${_tc} ${CMAKE_BINARY_DIR} ${VASM_BINARY_DIR}/test_${p}.vx)
  endforeach()
  add_test(NAME trace_multi
           COMMAND ${_tc} ${CMAKE_BINARY_DIR} ${VASM_BINARY_DIR}/multi.vx)
  add_test(NAME trace_events
           COMMAND ${_tc} ${CMAKE_BINARY_DIR} ${VASM_BINARY_DIR}/test_events.vx
                   -- --kbd "2000:a,6000:b,10000:c,14000:d,18000:e")
else()
  message(STATUS "gjs non trovato: i test della pagina di traccia non ci sono")
endif()

# Il manifesto delle configurazioni, per chi sta fuori dal build: quale
# programma e' strumentato e quale no. Lo legge tools/scheduler_facts.py, che
# per regola raccoglie i numeri SOLO dai programmi puliti -- e lo chiede qui
# invece di dedurlo dal suffisso di un nome.
#
# In FONDO, dopo ogni vasm_program: la proprieta' globale che raccoglie le
# righe si legge quando e' piena.
vasm_write_manifest(${VASM_BINARY_DIR}/configs.txt)
