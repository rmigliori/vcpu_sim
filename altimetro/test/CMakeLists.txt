# ---------------------------------------------------------------------------
#  I programmi che verificano l'altimetro.
# ---------------------------------------------------------------------------
vasm_object(t_alt0       SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro0.vasm
                         INTERFACES vinc_altimetro vinc_tcb vinc_queue)
vasm_object(t_alt1       SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro1.vasm
                         INTERFACES vinc_eco vinc_hal)
vasm_object(t_alt_shared SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro_shared.vasm)
vasm_program(test_altimetro0 OBJECTS t_alt0 alt_shared t_alt_shared
                             LINK lib_altimetro CONFIG banco)
vasm_program(test_altimetro1 OBJECTS t_alt1 alt_shared t_alt_shared)
get_target_property(_alt1_vx test_altimetro1 VASM_OUTPUT)

# test_altimetro — IL PERCORSO NOMINALE DELLA MACCHINA A STATI (25/10/2026).
#
# La CPU 0 e' l'altimetro, la CPU 1 riceve gli sweep di TRACK; il mondo e' la
# scena di §3.82 senza clutter ne' perdite. 15 frame: la sequenza dei modi
# 111112222233333, nessuna voce in ritardo, nessun blocco non finito, nessun
# armamento in ritardo, nessuna parola persa; la CPU 1 riceve buoni gli sweep
# 10..14, e il fronte dell'ultimo sta entro mezzo gate dal tau contato a mano.
# I conti e i rossi nei due sorgenti.
set(_alt "cpu0:111112222233333 cpu0:0 cpu0:0 cpu0:0 cpu0:0"
         "cpu1:5 cpu1:0 cpu1:14 cpu1:1")
string(JOIN " " _alt ${_alt})
vasm_check(altimetro MODE DUMPS PROGRAM test_altimetro0
           ARGS ${_alt1_vx} --eco-cmd "python3 ${CMAKE_SOURCE_DIR}/tools/mare.py ${CMAKE_CURRENT_SOURCE_DIR}/nominale.scena"
           EXPECT "${_alt}")
