
add_library(kiss_fft kiss_fft.c kiss_fftr.c)
target_link_libraries(kiss_fft pico_stdlib)