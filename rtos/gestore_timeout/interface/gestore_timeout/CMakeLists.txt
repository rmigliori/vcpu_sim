# gestore_timeout.vinc: le tre entry del task, e nessun tipo.
#
# Non pubblica strutture perche' non ne possiede: il descrittore e' di
# generic/timeout, il formato del messaggio di generic/messaggio, e la mailbox
# del gestore e' privata. E' un _api puro, il secondo del progetto dopo
# coda_api.vinc, e come quello non include l'_api di nessun altro.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_gestore_timeout
  DIR     ${_iface_dir}
  HEADERS gestore_timeout/gestore_timeout.vinc)
