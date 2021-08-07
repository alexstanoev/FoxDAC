if (TARGET usb_device)
    PROJECT(foxdac)

    add_executable(foxdac usb_spdif.c)
            
    add_subdirectory(drivers)
    add_subdirectory(ui)
    add_subdirectory(uac2)

    target_compile_definitions(foxdac PRIVATE
            AUDIO_FREQ_MAX=48000

            # ours are zero based, so say so
            #PICO_USBDEV_USE_ZERO_BASED_INTERFACES=1

            # need large descriptor
            #PICO_USBDEV_MAX_DESCRIPTOR_SIZE=256

            #PICO_USBDEV_ISOCHRONOUS_BUFFER_STRIDE_TYPE=1
            #PICO_USBDEV_ENABLE_DEBUG_TRACE

            PICO_AUDIO_SPDIF_PIN=9
            PICO_AUDIO_SPDIF_DMA_IRQ=1
            
            PICO_DEFAULT_UART_TX_PIN=16
    )
#usb_device
    target_link_libraries(foxdac foxdac-audio dac_ui ssd1306_driver wm8805 tpa6130 pico_stdlib pico_audio_spdif pico_multicore hardware_i2c)
    pico_add_extra_outputs(foxdac)
endif()
