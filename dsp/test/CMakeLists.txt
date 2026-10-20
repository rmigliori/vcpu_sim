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
