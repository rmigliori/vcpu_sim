# tcb.vinc: il descrittore di task e i suoi stati.
#
# INTERFACES vinc_coda perche' un TCB E' un nodo di lista: ridichiara fwd e bwd
# in testa, stesso idioma di TESTA, MESSAGGIO e BLOCCO.
#
# DA RIVEDERE quando §12.2 sara' completa: oggi messageHandling.vasm scrive
# TCB.state (riga 177), ed e' l'ultima violazione del confine "solo il kernel
# gestisce i task". Quando sara' sanata, questa interfaccia smettera' di essere
# dichiarata da chi non e' kernel -- e il grep a zero sara' la prova.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_tcb
  DIR        ${_iface_dir}
  HEADERS    tcb/tcb.vinc
  INTERFACES vinc_coda)
