add_library(tpa6130 INTERFACE)

target_sources(tpa6130 INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/tpa6130.c
)

target_include_directories(tpa6130 INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(tpa6130 INTERFACE pico_stdlib hardware_i2c)