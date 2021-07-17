if (TARGET usb_device)
    PROJECT(foxdac)

    add_executable(foxdac
            usb_spdif.c
            )
            
    add_subdirectory(drivers)
    add_subdirectory(ui)

    target_compile_definitions(foxdac PRIVATE
            AUDIO_FREQ_MAX=48000

            # ours are zero based, so say so
            PICO_USBDEV_USE_ZERO_BASED_INTERFACES=1

            # need large descriptor
            PICO_USBDEV_MAX_DESCRIPTOR_SIZE=256

            PICO_USBDEV_ISOCHRONOUS_BUFFER_STRIDE_TYPE=1
            PICO_USBDEV_ENABLE_DEBUG_TRACE

            PICO_AUDIO_SPDIF_PIN=9
    )

    target_link_libraries(foxdac dac_ui ssd1306_driver pico_stdlib usb_device pico_audio_spdif pico_multicore hardware_i2c)
    pico_add_extra_outputs(foxdac)
endif()
