add_compile_options(-Ofast)

include_directories(${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include/)

add_library(dac_dsp biquad_eq.c)
target_link_libraries(dac_dsp pico_stdlib pico_multicore CMSISDSPCommon CMSISDSPBasicMath CMSISDSPTransform CMSISDSPFiltering)