# semaforo.vinc: il tipo SEMAFORO, cioe' una TESTA il cui terzo campo conta
# RISORSE e non nodi — il contatore CONTABILE di §13.1, contro quello
# DESCRITTIVO della mailbox e quello di queue.vasm.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_semaforo
  DIR        ${_iface_dir}
  HEADERS    semaforo/semaforo.vinc
  INTERFACES vinc_queue)
