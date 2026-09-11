# ---------------------------------------------------------------------------
#  vasm.cmake — i sorgenti .vasm come target CMake.
#
#  CMake non conosce questo linguaggio: non ha un compilatore da configurare,
#  non deduce le dipendenze e non sa cosa sia un .vinc. Tutto quello che c'e'
#  qui e' lo strato che glielo insegna, sopra `vcpu_sim asm|ar|ld|run`.
#
#  Le quattro forme, dal basso:
#    vasm_interface  un .vinc (o un gruppo): niente da compilare, solo una
#                    cartella da propagare come -I e dei file da cui dipendere
#    vasm_object     un .vasm -> un .vo
#    vasm_archive    piu' .vo -> un .va (il linker ne pesca solo cio' che serve)
#    vasm_program    .vo/.va -> un .vx eseguibile
#    vasm_check      un .vx (o un .vasm legacy) -> un test di ctest
#
#  DUE SPECIE DI DIPENDENZA, e non vanno confuse:
#
#    INTERFACES  le interfacce da cui un sorgente prende COSTANTI (.equ,
#                .struct). Diventano i -I di `asm`, e servono a risolvere le
#                .include.
#    LINK        le librerie di cui un sorgente usa i SIMBOLI. Diventano i .va
#                sul comando di `ld`, e servono a risolvere gli .extern.
#
#  Sono davvero due cose diverse e il progetto lo mostra: lib_kernel LINKa le
#  code ma non ne dichiara l'interfaccia, perche' scheduler.vasm chiama
#  enqueue_coda senza aver bisogno di una sola costante di coda.vinc. Tenerle
#  in due parole chiave e non in una lista sola e' quello che rende quel fatto
#  leggibile al punto di chiamata invece che deducibile da com'e' fatto un
#  target definito altrove.
#
#  Si propagano anche con regole diverse, ed e' la ragione per cui la chiusura
#  e' calcolata due volte da due funzioni distinte. I -I sono transitivi FRA
#  INTERFACCE (tcb.vinc contiene .include "coda.vinc": chi nomina la prima deve
#  ricevere la cartella della seconda) ma NON attraversano un arco fra
#  librerie: se lo facessero, chi dichiara LINK lib_messaggi si ritroverebbe
#  gratis i -I di code, TCB e HAL, e potrebbe includerne gli header senza
#  averli dichiarati. E' la distinzione fra PUBLIC e PRIVATE di
#  target_link_libraries, fatta qui con due parole chiave invece che con un
#  qualificatore.
#
#  La specie di ogni target sta nella proprieta' VASM_KIND ed e' CONTROLLATA:
#  mettere una libreria in INTERFACES, o un'interfaccia in LINK, e' un errore a
#  tempo di configure, non un -I che manca a tempo di assemblaggio.
# ---------------------------------------------------------------------------

set(VASM_BINARY_DIR ${CMAKE_BINARY_DIR}/vasm)
file(MAKE_DIRECTORY ${VASM_BINARY_DIR})

# --- la specie di un target, controllata ------------------------------------
#
# Ogni forma marchia cio' che produce con VASM_KIND, e le parole chiave che
# accettano una lista di target verificano di aver ricevuto la specie giusta.
# Prima la garanzia era la convenzione sui nomi (vinc_* contro lib_*), cioe'
# niente: scambiarli dava un -I mancante a tempo di assemblaggio, lontano dalla
# riga sbagliata. Qui l'errore arriva a tempo di configure e dice quale voce di
# quale parola chiave.
function(_vasm_require_kind where keyword kind)
  foreach(t ${ARGN})
    if(NOT TARGET ${t})
      message(FATAL_ERROR
        "${where}: ${keyword} nomina '${t}', che non e' un target. "
        "Le voci di ${keyword} sono target, non percorsi.")
    endif()
    get_target_property(k ${t} VASM_KIND)
    if(NOT "${k}" STREQUAL "${kind}")
      message(FATAL_ERROR
        "${where}: ${keyword} vuole target di specie '${kind}', ma '${t}' e' "
        "'${k}'. INTERFACES sono le interfacce da cui arrivano le COSTANTI "
        "(-I), LINK le librerie da cui arrivano i SIMBOLI (.va).")
    endif()
  endforeach()
endfunction()

# --- una libreria di interfaccia: i .vinc -----------------------------------
#
# I .vinc sono header-only *per costruzione*: contengono solo costanti di
# compile-time (.equ/.struct) e non emettono un byte, quindi la INTERFACE
# library li modella esattamente — non c'e' nessun artefatto da produrre.
#
# Porta due cose ai consumatori: la cartella, che diventera' un -I, e l'elenco
# dei file, che diventera' un DEPENDS. La seconda e' in una proprieta' nostra
# (VASM_HEADERS) perche' CMake propaga da solo le sole proprieta' INTERFACE_*
# che conosce; la chiusura transitiva la calcola _vasm_closure qui sotto.
#
# INTERFACES sono le interfacce che questa include a sua volta: tcb.vinc
# contiene .include "coda.vinc", quindi vinc_tcb dichiara INTERFACES vinc_coda
# e chi nomina il primo riceve la cartella del secondo. Non e' un LINK: qui non
# ci sono simboli da risolvere, un .vinc non emette un byte.
function(vasm_interface name)
  cmake_parse_arguments(A "" "DIR" "HEADERS;INTERFACES" ${ARGN})
  _vasm_require_kind("vasm_interface(${name})" INTERFACES interface ${A_INTERFACES})
  add_library(${name} INTERFACE)
  set_property(TARGET ${name} PROPERTY VASM_KIND interface)
  target_include_directories(${name} INTERFACE ${A_DIR})
  set(files "")
  foreach(h ${A_HEADERS})
    list(APPEND files ${A_DIR}/${h})
  endforeach()
  set_property(TARGET ${name} PROPERTY VASM_HEADERS ${files})
  if(A_INTERFACES)
    target_link_libraries(${name} INTERFACE ${A_INTERFACES})
  endif()
endfunction()

# Chiusura transitiva delle interfacce: cartelle -I e file da cui dipendere.
#
# La transitivita' va percorsa a mano. CMake propaga INTERFACE_INCLUDE_DIRECTORIES
# ai target C che linkano, ma qui i consumatori sono add_custom_command, che non
# linkano niente e non ereditano nulla — e' la stessa ragione per cui piu' sotto
# la cartella diventa un -I esplicito invece di arrivare da sola.
function(_vasm_closure out_dirs out_files)
  set(dirs "")
  set(files "")
  set(seen "")
  set(pending ${ARGN})
  while(pending)
    list(POP_FRONT pending lib)
    if(lib IN_LIST seen)
      continue()
    endif()
    list(APPEND seen ${lib})

    get_target_property(d ${lib} INTERFACE_INCLUDE_DIRECTORIES)
    if(d)
      list(APPEND dirs ${d})
    endif()
    get_target_property(f ${lib} VASM_HEADERS)
    if(f)
      list(APPEND files ${f})
    endif()
    get_target_property(l ${lib} INTERFACE_LINK_LIBRARIES)
    if(l)
      list(APPEND pending ${l})
    endif()
  endwhile()
  if(dirs)
    list(REMOVE_DUPLICATES dirs)
  endif()
  if(files)
    list(REMOVE_DUPLICATES files)
  endif()
  set(${out_dirs}  "${dirs}"  PARENT_SCOPE)
  set(${out_files} "${files}" PARENT_SCOPE)
endfunction()

# --- un modulo: .vasm -> .vo ------------------------------------------------
#
# DEPENDS elenca i .vinc della chiusura, ed e' quello che fa scattare il
# riassemblaggio quando cambia un file di interfaccia. E' una dipendenza
# DICHIARATA, non scoperta: se un .vasm include un .vinc senza che INTERFACES lo
# dica, CMake non lo sapra' mai. La versione scoperta e' --emit-deps (punto 3).
function(vasm_object name)
  cmake_parse_arguments(A "" "SOURCE" "INTERFACES" ${ARGN})
  _vasm_require_kind("vasm_object(${name})" INTERFACES interface ${A_INTERFACES})
  _vasm_closure(dirs headers ${A_INTERFACES})

  set(iflags "")
  foreach(d ${dirs})
    list(APPEND iflags -I${d})
  endforeach()

  set(out ${VASM_BINARY_DIR}/${name}.vo)
  add_custom_command(
    OUTPUT  ${out}
    COMMAND vcpu_sim asm ${iflags} ${A_SOURCE} -o ${out}
    DEPENDS vcpu_sim ${A_SOURCE} ${headers}
    COMMENT "asm  ${name}.vo"
    VERBATIM)
  add_custom_target(${name} DEPENDS ${out})
  set_property(TARGET ${name} PROPERTY VASM_KIND object)
  set_property(TARGET ${name} PROPERTY VASM_OUTPUT ${out})
endfunction()

# Raccoglie i file prodotti da una lista di target vasm_object/vasm_archive,
# nell'ordine dato: per il linker l'ordine e' significativo.
function(_vasm_outputs out)
  set(files "")
  foreach(t ${ARGN})
    get_target_property(f ${t} VASM_OUTPUT)
    list(APPEND files ${f})
  endforeach()
  set(${out} "${files}" PARENT_SCOPE)
endfunction()

# Ordina un consumatore dopo i target che producono cio' che consuma.
#
# Non e' ridondante rispetto al DEPENDS sui file. Col generatore Make, un
# add_custom_command consumato da piu' target viene copiato in ognuno: senza
# questa dipendenza fra TARGET, `make -j` assembla lo stesso .vo piu' volte in
# parallelo — due processi che scrivono lo stesso file. Con Ninja non
# accadrebbe, ma il build non deve dipendere dal generatore scelto.
function(_vasm_order_after name)
  add_dependencies(${name} ${ARGN})
endfunction()

# --- una libreria vera: sorgenti + dipendenze verso altre librerie ----------
#
#  vasm_library(lib_pool SOURCES pool.vasm INTERFACES vinc_pool LINK lib_coda)
#
#  Le due specie di dipendenza dell'intestazione di questo file:
#    INTERFACES  le interfacce (.vinc) da cui prende costanti -> diventano -I
#    LINK        le altre librerie i cui simboli usa          -> diventano .va
#                sul comando di ld, chiuse transitivamente
#
#  La chiusura e' il punto: un programma che linka lib_messaggi non deve sapere
#  che sotto ci sono lib_coda e lib_hal. Prima quella conoscenza stava scritta a
#  mano nell'intestazione di ogni test, come lista ordinata di .vo.
function(vasm_library name)
  cmake_parse_arguments(A "" "" "SOURCES;INTERFACES;LINK" ${ARGN})
  _vasm_require_kind("vasm_library(${name})" INTERFACES interface ${A_INTERFACES})
  _vasm_require_kind("vasm_library(${name})" LINK archive ${A_LINK})
  set(objs "")
  set(i 0)
  foreach(src ${A_SOURCES})
    get_filename_component(base ${src} NAME_WE)
    vasm_object(${name}_${base} SOURCE ${src} INTERFACES ${A_INTERFACES})
    list(APPEND objs ${name}_${base})
    math(EXPR i "${i} + 1")
  endforeach()
  vasm_archive(${name} OBJECTS ${objs})
  set_property(TARGET ${name} PROPERTY VASM_LINK ${A_LINK})
  if(A_LINK)
    _vasm_order_after(${name} ${A_LINK})
  endif()
endfunction()

# Chiusura transitiva delle librerie, in ordine di scoperta: le dirette per
# prime, poi cio' che si tirano dietro. L'ordine conta per il layout
# dell'immagine, non per la correttezza -- il linker fa inclusione selettiva a
# fixpoint, quindi un ciclo o un ordine sfortunato non gli impediscono di
# chiudere i riferimenti.
function(_vasm_link_closure out)
  set(libs "")
  set(pending ${ARGN})
  while(pending)
    list(POP_FRONT pending lib)
    if(lib IN_LIST libs)
      continue()
    endif()
    list(APPEND libs ${lib})
    get_target_property(deps ${lib} VASM_LINK)
    if(deps)
      list(APPEND pending ${deps})
    endif()
  endwhile()
  set(${out} "${libs}" PARENT_SCOPE)
endfunction()

# --- una libreria: piu' .vo -> .va ------------------------------------------
function(vasm_archive name)
  cmake_parse_arguments(A "" "" "OBJECTS" ${ARGN})
  _vasm_outputs(objs ${A_OBJECTS})
  set(out ${VASM_BINARY_DIR}/${name}.va)
  add_custom_command(
    OUTPUT  ${out}
    COMMAND vcpu_sim ar ${out} ${objs}
    DEPENDS vcpu_sim ${objs}
    COMMENT "ar   ${name}.va"
    VERBATIM)
  add_custom_target(${name} DEPENDS ${out})
  _vasm_order_after(${name} ${A_OBJECTS})
  set_property(TARGET ${name} PROPERTY VASM_KIND archive)
  set_property(TARGET ${name} PROPERTY VASM_OUTPUT ${out})
endfunction()

# --- un eseguibile: .vo/.va -> .vx ------------------------------------------
function(vasm_program name)
  cmake_parse_arguments(A "" "ENTRY" "OBJECTS;LINK" ${ARGN})
  _vasm_require_kind("vasm_program(${name})" LINK archive ${A_LINK})
  # Gli oggetti espliciti (sempre linkati) per primi, poi le librerie della
  # chiusura, da cui il linker pesca solo cio' che serve.
  set(all ${A_OBJECTS})
  if(A_LINK)
    _vasm_link_closure(libs ${A_LINK})
    list(APPEND all ${libs})
  endif()
  _vasm_outputs(objs ${all})
  set(A_OBJECTS ${all})
  set(entry_flag "")
  if(A_ENTRY)
    set(entry_flag -e ${A_ENTRY})
  endif()
  set(out ${VASM_BINARY_DIR}/${name}.vx)
  add_custom_command(
    OUTPUT  ${out}
    COMMAND vcpu_sim ld ${objs} ${entry_flag} -o ${out}
    DEPENDS vcpu_sim ${objs}
    COMMENT "ld   ${name}.vx"
    VERBATIM)
  add_custom_target(${name} ALL DEPENDS ${out})
  _vasm_order_after(${name} ${A_OBJECTS})
  set_property(TARGET ${name} PROPERTY VASM_KIND program)
  set_property(TARGET ${name} PROPERTY VASM_OUTPUT ${out})
endfunction()

# --- un'invariante di regressione -> un test di ctest -----------------------
#
# Questo e' il premio della migrazione. Le invarianti di §4 dell'handoff erano
# un commento e la disciplina di chi lo esegue: quattro pipeline scritte a mano
# nelle intestazioni dei test e il confronto con le sequenze attese fatto a
# occhio. Qui il confronto lo fa la macchina, e i numeri attesi stanno in un
# posto solo — questo file di build.
#
#   MODE DUMPS   confronta la sequenza dei valori stampati da `dumps`
#   MODE STATS   confronta istruzioni / vec-elem-ops / cicli
# ARGS: opzioni passate al simulatore all'esecuzione (non all'assemblaggio) --
# oggi serve per --kbd, che alimenta un device con una traccia a cicli. Sta qui
# e non in IFLAGS perche' le due cose vanno in due momenti diversi: gli -I li
# vuole l'assembler, questo lo vuole la macchina.
function(vasm_check name)
  cmake_parse_arguments(A "" "MODE;EXPECT;PROGRAM;SOURCE" "IFLAGS;ARGS" ${ARGN})
  if(A_PROGRAM)
    get_target_property(vx ${A_PROGRAM} VASM_OUTPUT)
    set(cmd $<TARGET_FILE:vcpu_sim> run ${vx} ${A_ARGS})
  else()
    # percorso legacy a file singolo: assembla ed esegue in memoria
    set(cmd $<TARGET_FILE:vcpu_sim> ${A_IFLAGS} ${A_ARGS} ${A_SOURCE})
  endif()
  add_test(NAME ${name}
           COMMAND ${CMAKE_COMMAND}
                   -P ${CMAKE_SOURCE_DIR}/cmake/vasm_check.cmake
                   -- ${A_MODE} ${A_EXPECT} ${cmd})
endfunction()
