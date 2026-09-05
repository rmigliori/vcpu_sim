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

execute_process(COMMAND ${argv}
                OUTPUT_VARIABLE out
                ERROR_VARIABLE  err
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "il comando e' fallito (rc=${rc}):\n${out}${err}")
endif()

string(REPLACE "\n" ";" lines "${out}")
set(got "")

if(mode STREQUAL "DUMPS")
  # Le righe di `dumps`: "r5 = 98", "f1 = 2.5". Tiene solo il valore, in ordine.
  foreach(line ${lines})
    if(line MATCHES "^[rfv][0-9]+ = (.+)$")
      list(APPEND got "${CMAKE_MATCH_1}")
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
