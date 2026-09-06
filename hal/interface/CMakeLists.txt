# Il ramo pubblico: cio' che gli altri possono nominare.
#
# Questa cartella e' quella che viaggia come -I. Il livello sotto ripete il nome
# della libreria, e non e' decorazione: e' quel livello a far scrivere
# .include "hal/hal.vinc" invece di "hal.vinc", e quindi a rendere un include
# non dichiarato un errore di assemblaggio invece che una fortuna.
add_subdirectory(hal)
