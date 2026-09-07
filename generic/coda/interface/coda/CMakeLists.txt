# Due header, un target: stanno nella stessa cartella, quindi viaggiano con lo
# stesso -I e separarli in due target aggiungerebbe contabilita' senza dare
# nessun vincolo in piu'. A dividerli e' l'uso, e la regola sta scritta in cima
# a coda_api.vinc:
#
#   coda.vinc      i TIPI (TESTA, CODA_*) — lo include il fornitore e chi
#                  descrive nodi di lista
#   coda_api.vinc  i tipi PIU' le entry   — lo include chi CHIAMA le code
#
# Foglia: non include nessun altro .vinc, quindi niente INTERFACES. E' il fondo
# del grafo delle interfacce, come lib_hal lo e' di quello delle librerie.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_coda
  DIR     ${_iface_dir}
  HEADERS coda/coda.vinc coda/coda_api.vinc)
