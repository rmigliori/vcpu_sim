# wait.vinc: il tipo WAIT -- il contatore e il TCB titolare. File foglia.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_wait
  DIR        ${_iface_dir}
  HEADERS    wait/wait.vinc)
