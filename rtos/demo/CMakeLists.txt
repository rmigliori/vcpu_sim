# La demo HAL+kernel: due task che si alternano su otto tick.
#
# Non e' una libreria ed e' in cima al DAG, come i test: dichiara cio' che usa
# e non la lista dei moduli da linkare. E' la sorgente dell'invariante 97/64.
vasm_object(demo_main
  SOURCE     ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_demo.vasm
  INTERFACES vinc_tcb)

vasm_program(scheduler_demo OBJECTS demo_main LINK lib_kernel)
