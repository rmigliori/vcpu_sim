# pool.vinc: la geometria delle sei classi, BLOCCO e gli esiti POOL_*.
#
# INTERFACES vinc_coda, ed e' una dipendenza vera e non una comodita': BLOCCO
# *e'* un nodo di lista e una free-list *e'* una TESTA. Chi dichiara questa
# interfaccia riceve anche la cartella delle code, perche' senza non
# assemblerebbe.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_pool
  DIR        ${_iface_dir}
  HEADERS    pool/pool.vinc
  INTERFACES vinc_coda)
