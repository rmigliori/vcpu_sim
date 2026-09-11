# hal.vinc: la taglia del frame di contesto e i bit della parola di stato.
#
# La cartella propagata e' interface/, non interface/hal/: e' il padre di questa,
# cosi' il nome da includere e' "hal/hal.vinc" e chi non ha dichiarato questa
# interfaccia non lo risolve.
#
# Foglia: non include nessun altro .vinc, quindi niente INTERFACES.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_hal
  DIR     ${_iface_dir}
  HEADERS hal/hal.vinc hal/kbd.vinc)
