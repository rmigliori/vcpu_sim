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
