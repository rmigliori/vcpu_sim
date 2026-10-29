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
vasm_program(test_altimetro1 OBJECTS t_alt1 alt_shared t_alt_shared LINK lib_fft)
get_target_property(_alt1_vx test_altimetro1 VASM_OUTPUT)

# test_altimetro — IL PERCORSO NOMINALE DELLA MACCHINA A STATI (25/10/2026).
#
# La CPU 0 e' lo strumento, la CPU 1 il prodotto: riceve gli sweep di TRACK e
# fa la FFT sul blocco nel ping-pong dell'ADC. Il mondo e' la scena di §3.82
# senza clutter ne' perdite. 15 frame: la sequenza dei modi 11111 22222 33333,
# nessuna voce in ritardo, nessun blocco non finito, nessun armamento in
# ritardo, nessuna parola persa; e dal 26/10 (§3.106) il testimone e' il task
# delle statistiche: nessun frame sfuggito, nessun record perso, i record per
# procedura 15 15 5 1 5 15, nessuna riga con la regola dei budget negativa;
# la CPU 1 riceve buone le teste degli sweep 10..14, il fronte dell'ultimo
# sta entro mezzo gate dal tau contato a mano,
# quattro spettri hanno la DC a 45 gradi e lo sweep 12, letto tardi apposta,
# esce superato. I conti e i rossi nei due sorgenti.
set(_alt "cpu0:11111 cpu0:22222 cpu0:33333 cpu0:0 cpu0:0 cpu0:0 cpu0:0"
         "cpu0:0 cpu0:0 cpu0:15 cpu0:15 cpu0:5 cpu0:1 cpu0:5 cpu0:15 cpu0:0"
         "cpu1:5 cpu1:0 cpu1:14 cpu1:1 cpu1:4 cpu1:1")
string(JOIN " " _alt ${_alt})
vasm_check(altimetro MODE DUMPS PROGRAM test_altimetro0
           ARGS ${_alt1_vx} --eco-cmd "python3 ${CMAKE_SOURCE_DIR}/tools/mare.py ${CMAKE_CURRENT_SOURCE_DIR}/nominale.scena"
           EXPECT "${_alt}")

# I RAMI DI FALLIMENTO (29/10/2026, §3.108): lo stesso programma per 30 frame
# (-D FRAMI_30), e la CPU 1 che conta solo le teste (-D SOLO_TESTE: il tau e lo
# sweep letto tardi sono tarati sul nominale). Una scena per ramo, piu' quella
# di §3.82 intera. Le sequenze e i conti, a mano, nelle scene e in §3.108.
vasm_object(t_alt0_30    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro0.vasm
                         INTERFACES vinc_altimetro vinc_tcb vinc_queue
                         DEFINES FRAMI_30)
vasm_program(test_altimetro0_30 OBJECTS t_alt0_30 alt_shared t_alt_shared
                                LINK lib_altimetro CONFIG banco)
vasm_object(t_alt1_teste SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro1.vasm
                         INTERFACES vinc_eco vinc_hal
                         DEFINES SOLO_TESTE)
vasm_program(test_altimetro1_teste OBJECTS t_alt1_teste alt_shared t_alt_shared LINK lib_fft)
get_target_property(_alt1t_vx test_altimetro1_teste VASM_OUTPUT)

# La CPU 0: la sequenza a gruppi di 5, i sei contatori a zero (ritardi, blocchi
# non finiti, armamenti in ritardo, parole perse, frame sfuggiti, record persi),
# i record per procedura -- FRONTE, TRACKER e DECIDI uno per frame, BARKER_ACC
# uno per frame di ACQ, BARKER_DEC uno per ACQ5, MSG uno per frame di TRACK --
# e le regole negative. La CPU 1: le teste buone, le altre, l'ultimo n buono.
# Rossi con quattro mutanti di DECIDI (§3.108): CAL che passa sempre, ACQ che
# passa sempre, TRACK che riparte al primo sweep non nominale, e mai.
set(_zeri "0 0 0 0 0 0")
set(_alt_cal   "11111 11111 22222 33333 33333 33333 ${_zeri} 30 30 5 1 15 30 0 | 15 0 29")
set(_alt_acq   "11111 22222 11111 22222 33333 33333 ${_zeri} 30 30 10 2 10 30 0 | 10 0 29")
set(_alt_track "11111 22222 33333 11111 22222 33333 ${_zeri} 30 30 10 2 10 30 0 | 7 3 29")
set(_alt_vera  "11111 11111 22222 33333 33333 33333 ${_zeri} 30 30 5 1 15 30 0 | 14 1 29")
foreach(scena cal acq track vera)
  # "a b | c" -> "cpu0:a cpu0:b cpu1:c": chi stampa cosa, come vuole vasm_check
  string(REPLACE " | " ";" _parti "${_alt_${scena}}")
  list(GET _parti 0 _c0)
  list(GET _parti 1 _c1)
  string(REGEX REPLACE "([^ ]+)" "cpu0:\\1" _c0 "${_c0}")
  string(REGEX REPLACE "([^ ]+)" "cpu1:\\1" _c1 "${_c1}")
  vasm_check(altimetro_${scena} MODE DUMPS PROGRAM test_altimetro0_30
             ARGS ${_alt1t_vx} --eco-cmd "python3 ${CMAKE_SOURCE_DIR}/tools/mare.py ${CMAKE_CURRENT_SOURCE_DIR}/${scena}.scena"
             EXPECT "${_c0} ${_c1}")
endforeach()

# LA TABELLA DEI CICLI delle statistiche (26/10/2026, §3.106): lo stesso
# programma con -D STAT_TABELLA, che stampa anche ritardo, lavoro, margine e
# regola per procedura. NON e' in ctest, di proposito: sono misure, e ogni
# modifica del kernel le muove legittimamente. Si costruisce col resto e si
# lancia a mano, come scritto in §3.106.
vasm_object(t_alt0_tabella SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro0.vasm
                           INTERFACES vinc_altimetro vinc_tcb vinc_queue
                           DEFINES STAT_TABELLA)
vasm_program(test_altimetro0_tabella OBJECTS t_alt0_tabella alt_shared t_alt_shared
                                     LINK lib_altimetro CONFIG banco)

# IL CPUOMETRO DELL'ALTIMETRO (27/10/2026, §3.107): lo stesso programma con
# -D CPUOMETRO, in cui l'idle genera l'onda quadra di §3.88 (N = 128, H = 259).
# Il kernel e la libreria sono quelli puliti; cambia solo l'idle.
vasm_object(t_alt0_cpuometro SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_altimetro0.vasm
                             INTERFACES vinc_altimetro vinc_tcb vinc_queue vinc_hal vinc_marks
                             DEFINES CPUOMETRO)
vasm_program(test_altimetro0_cpuometro OBJECTS t_alt0_cpuometro alt_shared t_alt_shared
                                       LINK lib_altimetro CONFIG banco)

# Il test: tools/cpuometro.py per frame (100000 cicli da ALT_T0 = 20000,
# altimetro.vinc), col possesso del canale 0 come secondo strumento al posto di
# trace.py, che qui vorrebbe centinaia di MB di traccia. Controlla l'onda
# (alterna, H = 259 come dichiarato, nessuna marca persa), che nessun frame
# resti senza CPU libera, e che la CPU libera non superi il possesso dell'idle.
# I numeri -- la libera per frame, il minimo, la quota di S -- non sono qui:
# sono misure, e stanno in §3.107. Visto rosso: l'idle asimmetrico (H 258), un
# H dichiarato sbagliato, un lettore che conta 3 cicli in piu' a semiperiodo.
get_target_property(_alt0c_vx test_altimetro0_cpuometro VASM_OUTPUT)
add_test(NAME cpuometro_altimetro
         COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/cpuometro.py
                 $<TARGET_FILE:vcpu_sim> ${_alt0c_vx} --h 259
                 --frame 100000 20000 --idle tcbI
                 -- ${_alt1_vx} --eco-cmd "python3 ${CMAKE_SOURCE_DIR}/tools/mare.py ${CMAKE_CURRENT_SOURCE_DIR}/nominale.scena")
set_tests_properties(cpuometro_altimetro PROPERTIES
                     PASS_REGULAR_EXPRESSION "D'ACCORDO"
                     FAIL_REGULAR_EXPRESSION "NON D'ACCORDO"
                     TIMEOUT ${VASM_TEST_TIMEOUT})
