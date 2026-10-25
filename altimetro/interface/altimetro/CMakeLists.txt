# altimetro.vinc: la configurazione, i modi, le ancore e i simboli pubblicati.
# Foglia, niente INTERFACES.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_altimetro
  DIR     ${_iface_dir}
  HEADERS altimetro/altimetro.vinc)
