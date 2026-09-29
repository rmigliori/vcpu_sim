# ---------------------------------------------------------------------------
#  dsp — l'elaborazione del segnale (29/09/2026).
#
#  Non sta in generic/, che per il suo criterio contiene cio' che non nomina
#  lo scheduler: una FFT lo soddisfa, ma generic/ e' fatta di strutture del
#  kernel -- code, pool, messaggi -- e una FFT e' un'altra specie di cosa.
#  Ne eredita pero' la regola: nessun modulo di dsp/, test compresi, nomina un
#  simbolo o un header di rtos/. Cosi' la cartella si solleva di peso.
# ---------------------------------------------------------------------------
add_subdirectory(fft)
add_subdirectory(test)
