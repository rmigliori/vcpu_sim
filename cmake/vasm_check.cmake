# ---------------------------------------------------------------------------
#  vasm_check.cmake — esegue un programma e confronta i valori significativi
#  del suo output con quelli attesi. Invocato da vasm_check() in vasm.cmake:
#
#      cmake -P vasm_check.cmake -- <MODE> <ATTESO> <comando...>
#
#  Confronta solo cio' che §4 dell'handoff dichiara invariante, non tutto
#  l'output: per i test mirati la sequenza dei `dumps`, per i tre programmi di
#  riferimento le tre statistiche. Includere il resto renderebbe il test piu'
#  fragile del contratto che deve verificare.
# ---------------------------------------------------------------------------

# Argomenti dopo il "--": MODE, ATTESO, poi il comando.
set(argv "")
set(after_sep FALSE)
math(EXPR last "${CMAKE_ARGC} - 1")
foreach(i RANGE 0 ${last})
  if(after_sep)
    list(APPEND argv "${CMAKE_ARGV${i}}")
  elseif("${CMAKE_ARGV${i}}" STREQUAL "--")
    set(after_sep TRUE)
  endif()
endforeach()

list(POP_FRONT argv mode expected)
if(NOT argv)
  message(FATAL_ERROR "vasm_check: manca il comando da eseguire")
endif()

# IL TETTO DI BYTE (26/10/2026, §3.105). Fino a oggi l'uscita finiva in due
# variabili, cioe' in RAM e senza limite: un programma che stampa per sempre
# riempie ~94 MB/s (misurato, §3.104), e il 25/10 ha mandato il VPS in OOM.
# Adesso stdout e stderr vanno in due FILE nella cartella di build (disco, non
# /tmp che e' RAM), sotto `ulimit -f`: chi supera VASM_TETTO byte viene ucciso
# dal sistema (SIGXFSZ) e il test fallisce dicendolo. I file sono separati
# perche' uno stdout bufferizzato, mescolato allo stderr, si spezzerebbe a
# meta' di una riga di dumps. Il TIMEOUT di ctest (5 s) resta per chi gira per
# sempre senza stampare.
#
# bash e non sh: `ulimit -f` conta blocchi da 1024 byte in bash e da 512 in
# dash, che qui e' /bin/sh (provato il 26/10). Il 26/10 l'uscita piu' grande
# dei test era di 709 byte (msg).
set(VASM_TETTO 16777216)                       # 16 MiB per file
math(EXPR blocchi "${VASM_TETTO} / 1024")
find_program(BASH bash REQUIRED)
string(RANDOM LENGTH 12 tag)
set(f_out "${CMAKE_CURRENT_BINARY_DIR}/vasm_check_${tag}.out")
set(f_err "${CMAKE_CURRENT_BINARY_DIR}/vasm_check_${tag}.err")
execute_process(COMMAND ${BASH} -c "ulimit -f ${blocchi} && exec \"$@\"" vasm_check ${argv}
                OUTPUT_FILE     ${f_out}
                ERROR_FILE      ${f_err}
                RESULT_VARIABLE rc)
file(SIZE ${f_out} n_out)
file(SIZE ${f_err} n_err)
if(n_out GREATER_EQUAL VASM_TETTO OR n_err GREATER_EQUAL VASM_TETTO)
  file(READ ${f_out} out LIMIT 400)
  file(READ ${f_err} err LIMIT 400)
  file(REMOVE ${f_out} ${f_err})
  message(FATAL_ERROR "l'uscita ha superato il tetto di ${VASM_TETTO} byte "
                      "(stdout ${n_out}, stderr ${n_err}, rc=${rc}): il programma "
                      "e' stato fermato. L'inizio:\n${out}\n--- stderr ---\n${err}")
endif()
file(READ ${f_out} out)
file(READ ${f_err} err)
file(REMOVE ${f_out} ${f_err})
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "il comando e' fallito (rc=${rc}):\n${out}${err}")
endif()

string(REPLACE "\n" ";" lines "${out}")
set(got "")

if(mode STREQUAL "DUMPS")
  # Le righe di `dumps`: "r5 = 98", "f1 = 2.5". Tiene solo il valore, in ordine.
  # Con piu' CPU ogni riga ha il prefisso della sua (§3.79), e il valore si
  # tiene col nome della CPU: "[cpu1] r5 = 10" -> "cpu1:10". L'atteso dice cosi'
  # anche CHI ha stampato cosa, nell'ordine del tempo simulato.
  foreach(line ${lines})
    if(line MATCHES "^[rfv][0-9]+ = (.+)$")
      list(APPEND got "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^\\[(cpu[0-9]+)\\] [rfv][0-9]+ = (.+)$")
      list(APPEND got "${CMAKE_MATCH_1}:${CMAKE_MATCH_2}")
    endif()
  endforeach()
elseif(mode STREQUAL "STATS")
  # Le tre statistiche finali, sempre in quest'ordine. I rami sono separati di
  # proposito: in un if(... OR ...) CMake valuta ogni regex e CMAKE_MATCH_1
  # resta quella dell'ultima, non quella che ha fatto scattare la condizione.
  foreach(line ${lines})
    if(line MATCHES "^instructions executed *: ([0-9]+)$")
      list(APPEND got "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^vector element ops *: ([0-9]+)$")
      list(APPEND got "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^cycles \\(timing model\\) *: ([0-9]+)$")
      list(APPEND got "${CMAKE_MATCH_1}")
    endif()
  endforeach()
else()
  message(FATAL_ERROR "vasm_check: MODE sconosciuto '${mode}'")
endif()

string(JOIN " " got_str ${got})
string(STRIP "${expected}" want_str)
string(REGEX REPLACE "[ \t]+" " " want_str "${want_str}")

if(NOT got_str STREQUAL want_str)
  message(FATAL_ERROR
          "invariante spostata:\n  atteso : ${want_str}\n  ottenuto: ${got_str}\n"
          "--- output completo ---\n${out}")
endif()

message(STATUS "${mode} ok: ${got_str}")
