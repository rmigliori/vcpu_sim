# timeout.vinc: il DESCRIPTOR e la geometria del vettore delle scadenze.
#
# Foglia per una proprieta' del modello, non per un limite: il descrittore non
# entra mai in una lista (§9.3), quindi non ha niente da chiedere alle code --
# ed e' il solo .vinc di un nodo-non-nodo del progetto.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_timeout
  DIR     ${_iface_dir}
  HEADERS timeout/timeout.vinc)
