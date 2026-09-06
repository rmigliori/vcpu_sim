# coda.vinc: il tipo lista (TESTA) e gli esiti CODA_*.
#
# Foglia: non include nessun altro .vinc, quindi niente INTERFACES. E' il fondo
# del grafo delle interfacce, come lib_hal lo e' di quello delle librerie.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_coda
  DIR     ${_iface_dir}
  HEADERS coda/coda.vinc)
