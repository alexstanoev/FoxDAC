if (TARGET usb_device)
    PROJECT(foxdac)

    set(CMAKE_CXX_FLAGS_RELEASE "-Os")

    add_executable(foxdac usb_spdif.c)

    pico_set_binary_type(foxdac copy_to_ram)

    add_subdirectory(CMSIS)
    add_subdirectory(dsp)
    add_subdirectory(drivers)
    add_subdirectory(ui)

    target_compile_definitions(foxdac PRIVATE
            AUDIO_FREQ_MAX=48000

            # ours are zero based, so say so
            PICO_USBDEV_USE_ZERO_BASED_INTERFACES=1

            # need large descriptor
            PICO_USBDEV_MAX_DESCRIPTOR_SIZE=256

            PICO_USBDEV_ISOCHRONOUS_BUFFER_STRIDE_TYPE=2

            PICO_AUDIO_SPDIF_PIN=9
            PICO_AUDIO_SPDIF_DMA_IRQ=1
            
            PICO_DEFAULT_UART_TX_PIN=16
    )

    target_link_libraries(foxdac dac_ui dac_dsp ssd1306_driver wm8805 tpa6130 pico_stdlib usb_device pico_audio_spdif pico_multicore hardware_i2c)
    pico_add_extra_outputs(foxdac)
endif()
