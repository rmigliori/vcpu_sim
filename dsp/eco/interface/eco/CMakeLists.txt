# eco.vinc: le costanti e i contratti delle attivita'. Foglia, niente INTERFACES.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_eco
  DIR     ${_iface_dir}
  HEADERS eco/eco.vinc)
