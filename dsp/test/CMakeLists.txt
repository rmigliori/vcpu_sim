# ---------------------------------------------------------------------------
#  I programmi che verificano dsp/. Stanno dentro dsp/ per la stessa ragione
#  di generic/test/: la cartella deve potersi sollevare con le sue verifiche.
#
#  test_fft nomina hal/adc.vinc, e non e' un'eccezione alla regola di dsp/:
#  l'HAL e' il fondo del grafo, non rtos/. Gli serve il DEVICE per produrre il
#  blocco; la FFT, che lo riceve, non lo nomina.
# ---------------------------------------------------------------------------
vasm_object(t_fft SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_fft.vasm
                  INTERFACES vinc_fft vinc_hal)

vasm_program(test_fft OBJECTS t_fft LINK lib_fft)

# test_fft — LA FFT SUL BLOCCO DELL'ADC (29/09/2026).
#
# Tre numeri: il bin del picco, il picco in migliaia, il massimo degli altri
# bin in migliaia. fs = 1 MHz, un bin = 15625 Hz, e 78125 Hz = 5 bin esatti:
#   fft_pos   +78125 Hz, A = 1000   picco in 5
#   fft_neg   -78125 Hz, A = 1000   picco in 64 - 5 = 59
# 64 = A * N, e 0 e' la dispersione, che con k periodi interi non c'e'. Visti
# rossi coniugando i twiddle (lo spettro si specchia: 59 e 5) e togliendo il
# bit reversal. La fase non entra in nessuno dei tre: il perche' e' nel
# sorgente.
vasm_check(fft_pos MODE DUMPS PROGRAM test_fft EXPECT "5 64 0"
           ARGS --adc 78125,1000)
vasm_check(fft_neg MODE DUMPS PROGRAM test_fft EXPECT "59 64 0"
           ARGS --adc -78125,1000)

vasm_object(t_fronte SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_fronte.vasm
                     INTERFACES vinc_eco vinc_hal)

vasm_program(test_fronte OBJECTS t_fronte LINK lib_eco)

# test_fronte — FRONTE: LA RIVELAZIONE E IL FRONTE (14/10/2026).
#
# Due sweep sull'acqua ferma con rumore (fronte.scena): il primo trova l'eco
# entro mezzo gate da 30,5, contato a mano da tau; il secondo e' un TX perso
# e FRONTE deve dire che non c'e'. Visto rosso togliendo la rivelazione (la
# perdita esce trovata) e prendendo il rumore su tutta la finestra (l'eco vero
# esce non trovato). Il conto, e cio' che il test non vede, nel sorgente.
vasm_check(fronte MODE DUMPS PROGRAM test_fronte EXPECT "1 1 0"
           ARGS --eco-cmd "python3 ${CMAKE_SOURCE_DIR}/tools/mare.py ${CMAKE_CURRENT_SOURCE_DIR}/fronte.scena")

vasm_object(t_tracker SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_tracker.vasm
                      INTERFACES vinc_eco)

vasm_program(test_tracker OBJECTS t_tracker LINK lib_eco)

# test_tracker — TRACKER: L'ALFA/BETA CON L'INERZIA (20/10/2026).
#
# Otto sweep di misure scritte a mano, la finestra in retroazione: una perdita
# nell'inizializzazione, i due punti, il filtro, un clutter, una perdita in
# inerzia. Per ogni sweep esito, residuo e finestra, contati a mano nel
# sorgente. Visto rosso con quattro mutanti: la perdita che fa avanzare n (il
# difetto di m_attivita), la perdita senza inerzia, il clutter non scartato,
# beta a zero.
vasm_check(tracker MODE DUMPS PROGRAM test_tracker
           EXPECT "1 0 60010 0 0 60009 0 0 60010 0 0 60010 0 -1.5 60009 2 4.03516 60009 1 0 60009 0 0.558594 60009")

vasm_object(t_barker SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_barker.vasm
                     INTERFACES vinc_eco)

vasm_program(test_barker OBJECTS t_barker LINK lib_eco)

# test_barker — BARKER_ACC E BARKER_DEC: LA SOMMA ALLINEATA (21/10/2026).
#
# Cinque blocchi scritti dal test, un bersaglio fermo, il Barker + + + - + e
# finestre che saltano (d = 0, +3, -5, +1): la somma allineata ha il fronte a
# 30,375 e il plateau a 5 volte il blocco. Poi la somma che riparte con
# |d| >= 64 e quella che torna a zero, non trovata. Visto rosso con cinque
# mutanti: senza spostamento, lo spostamento al contrario, il segno ignorato,
# il bordo non azzerato, n = 0 che accumula. Il conto nel sorgente.
vasm_check(barker MODE DUMPS PROGRAM test_barker
           EXPECT "1 2 3 4 5 1 30.375 750 3000 600 1 600 2 0")

vasm_object(t_msg0       SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_msg0.vasm
                         INTERFACES vinc_eco)
vasm_object(t_msg1       SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_msg1.vasm
                         INTERFACES vinc_eco vinc_hal)
vasm_object(t_msg_shared SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_msg_shared.vasm
                         INTERFACES vinc_eco)
vasm_program(test_msg0 OBJECTS t_msg0 t_msg_shared LINK lib_eco)
vasm_program(test_msg1 OBJECTS t_msg1 t_msg_shared)
get_target_property(_msg1_vx test_msg1 VASM_OUTPUT)

# test_msg — ECO_APRI E MSG: IL SEQLOCK FRA LE DUE CPU (22/10/2026).
#
# La CPU 0 scrive tre sweep in due record alternati, e la CPU 1 legge il primo
# LENTA: a meta' aspetta che la CPU 0 abbia riaperto e riscritto lo stesso
# record con lo sweep 2. Il lettore deve buttarlo (0 2) e prendere interi gli
# altri due. Poi cinque msg a una FIFO di quattro: il quinto dice overrun.
# Visto rosso con tre mutanti: eco_apri vuota (il record misto esce buono, "0
# 0 0 2127"), un record solo nella CPU 0 (lo sweep 1 esce perso, "1 1"),
# l'overrun non riportato (l'ultimo 0). L'ordine e' quello del tempo simulato.
set(_msg "cpu0:0 cpu0:0 cpu1:0 cpu1:2 cpu0:0"
         "cpu1:1 cpu1:0 cpu1:1000 cpu1:1127 cpu1:60001 cpu1:1.5 cpu1:1"
         "cpu1:2 cpu1:0 cpu1:2000 cpu1:2127 cpu1:60002 cpu1:2.5 cpu1:2"
         "cpu0:0 cpu0:0 cpu0:0 cpu0:0 cpu0:1")
string(JOIN " " _msg ${_msg})
vasm_check(msg MODE DUMPS PROGRAM test_msg0 ARGS ${_msg1_vx} EXPECT "${_msg}")
