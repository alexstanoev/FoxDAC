add_library(encoder-pio INTERFACE)

target_sources(encoder-pio INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/encoder.c
)

pico_generate_pio_header(encoder-pio ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)

target_include_directories(encoder-pio INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(encoder-pio INTERFACE pico_stdlib hardware_pio)