cmake_minimum_required (VERSION 3.14)

# Define the path to CMSIS-DSP (ROOT is defined on command line when using cmake)
set(ROOT ${PROJECT_SOURCE_DIR})

set(ARM_CPU cortex-m0plus)
include(${ROOT}/CMSIS/DSP/Toolchain/GCC.cmake)

# Define the path to CMSIS-DSP (ROOT is defined on command line when using cmake)
set(DSP ${ROOT}/CMSIS/DSP)

# Add DSP folder to module path
list(APPEND CMAKE_MODULE_PATH ${DSP})

include_directories(${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include/)

########### 
#
# CMSIS DSP
#

# Load CMSIS-DSP definitions. Libraries will be built in bin_dsp
add_subdirectory(${DSP}/Source bin_dsp)
