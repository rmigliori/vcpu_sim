# Le tabelle sono un .vinc PRIVATO, generato da tools/fft_tables.py: sta
# accanto a fft.vasm e .include lo trova li'. Lo si dichiara comunque come
# interfaccia, perche' e' l'unico modo in cui il build sa che fft.vasm ne
# dipende -- senza, rigenerare le tabelle non riassemblerebbe niente. Il target
# e' definito qui e nominato solo qui: fuori da impl/ nessuno lo vede.
vasm_interface(vinc_fft_tables
  DIR     ${CMAKE_CURRENT_SOURCE_DIR}
  HEADERS fft_tables.vinc)

vasm_library(lib_fft
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/fft.vasm
  INTERFACES vinc_fft vinc_fft_tables)
