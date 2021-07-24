add_subdirectory(lvgl)
add_subdirectory(kissfft)

add_library(dac_ui ui.c lv_port_disp.c lv_port_indev.c badapple.c spectrum.c dac_lvgl_ui.c img_fox_logo_png.c img_speaker_png.c img_usb_png.c img_toslink_1_png.c img_toslink_2_png.c img_toslink_3_png.c)
target_link_libraries(dac_ui ssd1306_driver encoder-pio pico_stdlib pico_time hardware_i2c lvgl kiss_fft)