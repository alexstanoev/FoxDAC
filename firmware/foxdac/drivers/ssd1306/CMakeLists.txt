add_library(ssd1306_driver ssd1306.c ssd1306_fonts.c ssd1306_tests.c ssd1306.h ssd1306_tests.h)
target_link_libraries(ssd1306_driver pico_stdlib pico_time hardware_i2c)