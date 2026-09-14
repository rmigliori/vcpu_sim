# L'HAL: zero dipendenze, e per un HAL e' la proprieta' che conta.
#
# Nessun LINK, e non e' perche' non serve ancora: e' la definizione dello strato
# piu' basso. Il giorno che comparisse qui un LINK verso qualcosa di questo
# progetto, l'HAL avrebbe smesso di essere un HAL.
#
# CONFIGS marks (14/09/2026, §3.63): lo stesso sorgente produce anche
# lib_hal_marks, in cui i `.ifdef MARKS` di ctx_restore entrano. Serve perche'
# il ripristino dei registri e' l'unico pezzo del cammino "pronto -> gira" che
# il kernel NON PUO' misurare: ctx_restore non ritorna. L'HAL si misura da se'.
#
# Questo NON e' un LINK: e' la configurazione che porta con se' vinc_marks, e
# solo in quella. Nella build pulita l'HAL resta a zero dipendenze, e
# tools/fingerprint.sh lo dimostra byte per byte. Chi linka non cambia una riga:
# vasm_program(... CONFIG marks) rimappa da solo ogni libreria alla sua
# variante, quando esiste.
vasm_library(lib_hal
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/machine.vasm
  INTERFACES vinc_hal
  CONFIGS    marks)
