# lib_altimetro: il super task, le tabelle, gli adattatori, DECIDI e l'ISR.
# La RAM condivisa sta in un oggetto a parte, alt_shared, perche' va linkata
# anche nell'immagine dell'altra CPU, che il codice non lo vuole.
vasm_library(lib_altimetro
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/altimetro.vasm
  INTERFACES vinc_altimetro vinc_eco vinc_fft vinc_hal vinc_tcb vinc_wait
  LINK       lib_eco lib_fft lib_wait lib_kernel)

vasm_object(alt_shared SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/altimetro_shared.vasm
                       INTERFACES vinc_eco)
