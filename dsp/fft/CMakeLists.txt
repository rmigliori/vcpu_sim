# ---------------------------------------------------------------------------
#  lib_fft — la FFT complessa a 64 punti sul blocco dell'ADC.
#
#  Nessuna dipendenza da altre librerie: riceve un indirizzo e due buffer, e
#  non tocca ne' l'ADC ne' l'HAL. Il formato del blocco e' un CONTRATTO
#  (fft/fft.vinc), non un include.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
