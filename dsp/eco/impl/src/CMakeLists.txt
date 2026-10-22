vasm_library(lib_eco
  SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/fronte.vasm
             ${CMAKE_CURRENT_SOURCE_DIR}/tracker.vasm
             ${CMAKE_CURRENT_SOURCE_DIR}/barker.vasm
             ${CMAKE_CURRENT_SOURCE_DIR}/msg.vasm
  INTERFACES vinc_eco vinc_hal)
