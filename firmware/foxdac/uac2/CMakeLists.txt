add_library(foxdac-audio INTERFACE)

target_sources(foxdac-audio INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/uac2.c
  ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

target_include_directories(foxdac-audio INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(foxdac-audio INTERFACE pico_stdlib tinyusb_device tinyusb_board)