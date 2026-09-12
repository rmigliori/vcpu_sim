# timeout_manager.vinc: le tre entry del task, e nessun tipo.
#
# Non pubblica strutture perche' non ne possiede: il descrittore e' di
# generic/timeout, il formato del messaggio di generic/message, e la mailbox
# del gestore e' privata. E' un _api puro, il secondo del progetto dopo
# queue_api.vinc, e come quello non include l'_api di nessun altro.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_timeout_manager
  DIR     ${_iface_dir}
  HEADERS timeout_manager/timeout_manager.vinc)
