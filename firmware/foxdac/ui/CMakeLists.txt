add_subdirectory(lvgl)

add_library(dac_ui ui.c lv_port_disp.c)
target_link_libraries(dac_ui ssd1306_driver encoder-pio pico_stdlib pico_time hardware_i2c lvgl)