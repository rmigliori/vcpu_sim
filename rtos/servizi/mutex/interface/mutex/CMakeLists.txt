# mutex.vinc: il tipo MUTEX — una TESTA annidata piu' owner, prio_prec e
# ceiling. Ci sta anche il riquadro su DOVE si dichiara il ceiling e perche'
# non ha una direzione conservativa.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_mutex
  DIR        ${_iface_dir}
  HEADERS    mutex/mutex.vinc
  INTERFACES vinc_coda)
