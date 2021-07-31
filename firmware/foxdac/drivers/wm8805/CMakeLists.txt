add_library(wm8805 INTERFACE)

target_sources(wm8805 INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/wm8805.c
)

target_include_directories(wm8805 INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(wm8805 INTERFACE pico_stdlib hardware_spi)