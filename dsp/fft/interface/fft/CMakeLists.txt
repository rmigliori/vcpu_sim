# fft.vinc: FFT_N e il contratto di fft64. Foglia, niente INTERFACES.
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_fft
  DIR     ${_iface_dir}
  HEADERS fft/fft.vinc)
