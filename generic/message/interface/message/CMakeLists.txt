# message.vinc: MESSAGGIO, la testa comune PAYLOAD e i codici MSG_*.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_message
  DIR        ${_iface_dir}
  HEADERS    message/message.vinc
  INTERFACES vinc_queue)
