/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/usb_device.h"
#include "pico/audio.h"
#include "pico/audio_spdif.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "lufa/AudioClassCommon.h"

#include "ui/ui.h"
#include "ui/spectrum.h"

#include "drivers/wm8805/wm8805.h"
#include "drivers/tpa6130/tpa6130.h"

#include "dsp/biquad_eq.h"

CU_REGISTER_DEBUG_PINS(audio_timing)

// ---- select at most one ---
//CU_SELECT_DEBUG_PINS(audio_timing)

static char *descriptor_strings[] =
{
        "astanoev.com",
        "FoxDAC",
        "0123456789AB"
};

// todo fix these
#define VENDOR_ID   0x2e8au
#define PRODUCT_ID  0xfeddu

#define AUDIO_OUT_ENDPOINT  0x01U
#define AUDIO_IN_ENDPOINT   0x82U

#undef AUDIO_SAMPLE_FREQ
#define AUDIO_SAMPLE_FREQ(frq) (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_MAX_PACKET_SIZE(freq) (uint8_t)(((freq + 999) / 1000) * 4)

#define FEATURE_MUTE_CONTROL 1u
#define FEATURE_VOLUME_CONTROL 2u

#define ENDPOINT_FREQ_CONTROL 1u

struct audio_device_config {
    struct usb_configuration_descriptor descriptor;
    struct usb_interface_descriptor ac_interface;
    struct __packed {
        USB_Audio_StdDescriptor_Interface_AC_t core;
        USB_Audio_StdDescriptor_InputTerminal_t input_terminal;
        USB_Audio_StdDescriptor_FeatureUnit_t feature_unit;
        USB_Audio_StdDescriptor_OutputTerminal_t output_terminal;
    } ac_audio;
    struct usb_interface_descriptor as_zero_interface;
    struct usb_interface_descriptor as_op_interface;
    struct __packed {
        USB_Audio_StdDescriptor_Interface_AS_t streaming;
        struct __packed {
            USB_Audio_StdDescriptor_Format_t core;
            USB_Audio_SampleFreq_t freqs[3];
        } format;
    } as_audio;
    struct __packed {
        struct usb_endpoint_descriptor_long core;
        USB_Audio_StdDescriptor_StreamEndpoint_Spc_t audio;
    } ep1;
    struct usb_endpoint_descriptor_long ep2;
};

static const struct audio_device_config audio_device_config = {
        .descriptor = {
                .bLength             = sizeof(audio_device_config.descriptor),
                .bDescriptorType     = DTYPE_Configuration,
                .wTotalLength        = sizeof(audio_device_config),
                .bNumInterfaces      = 2,
                .bConfigurationValue = 0x01,
                .iConfiguration      = 0x00,
                .bmAttributes        = 0x80,
                .bMaxPower           = 0x32,
        },
        .ac_interface = {
                .bLength            = sizeof(audio_device_config.ac_interface),
                .bDescriptorType    = DTYPE_Interface,
                .bInterfaceNumber   = 0x00,
                .bAlternateSetting  = 0x00,
                .bNumEndpoints      = 0x00,
                .bInterfaceClass    = AUDIO_CSCP_AudioClass,
                .bInterfaceSubClass = AUDIO_CSCP_ControlSubclass,
                .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
                .iInterface         = 0x00,
        },
        .ac_audio = {
                .core = {
                        .bLength = sizeof(audio_device_config.ac_audio.core),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Header,
                        .bcdADC = VERSION_BCD(1, 0, 0),
                        .wTotalLength = sizeof(audio_device_config.ac_audio),
                        .bInCollection = 1,
                        .bInterfaceNumbers = 1,
                },
                .input_terminal = {
                        .bLength = sizeof(audio_device_config.ac_audio.input_terminal),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_InputTerminal,
                        .bTerminalID = 1,
                        .wTerminalType = AUDIO_TERMINAL_STREAMING,
                        .bAssocTerminal = 0,
                        .bNrChannels = 2,
                        .wChannelConfig = AUDIO_CHANNEL_LEFT_FRONT | AUDIO_CHANNEL_RIGHT_FRONT,
                        .iChannelNames = 0,
                        .iTerminal = 0,
                },
                .feature_unit = {
                        .bLength = sizeof(audio_device_config.ac_audio.feature_unit),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Feature,
                        .bUnitID = 2,
                        .bSourceID = 1,
                        .bControlSize = 1,
                        .bmaControls = {AUDIO_FEATURE_MUTE | AUDIO_FEATURE_VOLUME, 0, 0},
                        .iFeature = 0,
                },
                .output_terminal = {
                        .bLength = sizeof(audio_device_config.ac_audio.output_terminal),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_OutputTerminal,
                        .bTerminalID = 3,
                        .wTerminalType = AUDIO_TERMINAL_OUT_SPEAKER,
                        .bAssocTerminal = 0,
                        .bSourceID = 2,
                        .iTerminal = 0,
                },
        },
        .as_zero_interface = {
                .bLength            = sizeof(audio_device_config.as_zero_interface),
                .bDescriptorType    = DTYPE_Interface,
                .bInterfaceNumber   = 0x01,
                .bAlternateSetting  = 0x00,
                .bNumEndpoints      = 0x00,
                .bInterfaceClass    = AUDIO_CSCP_AudioClass,
                .bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
                .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
                .iInterface         = 0x00,
        },
        .as_op_interface = {
                .bLength            = sizeof(audio_device_config.as_op_interface),
                .bDescriptorType    = DTYPE_Interface,
                .bInterfaceNumber   = 0x01,
                .bAlternateSetting  = 0x01,
                .bNumEndpoints      = 0x02,
                .bInterfaceClass    = AUDIO_CSCP_AudioClass,
                .bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
                .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
                .iInterface         = 0x00,
        },
        .as_audio = {
                .streaming = {
                        .bLength = sizeof(audio_device_config.as_audio.streaming),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_General,
                        .bTerminalLink = 1,
                        .bDelay = 1,
                        .wFormatTag = 1, // PCM
                },
                .format = {
                        .core = {
                                .bLength = sizeof(audio_device_config.as_audio.format),
                                .bDescriptorType = AUDIO_DTYPE_CSInterface,
                                .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_FormatType,
                                .bFormatType = 1,
                                .bNrChannels = 2,
                                .bSubFrameSize = 2,
                                .bBitResolution = 16,
                                .bSampleFrequencyType = count_of(audio_device_config.as_audio.format.freqs),
                        },
                        .freqs = {
                                AUDIO_SAMPLE_FREQ(44100),
                                AUDIO_SAMPLE_FREQ(48000),
                                AUDIO_SAMPLE_FREQ(96000)
                        },
                },
        },
        .ep1 = {
                .core = {
                        .bLength          = sizeof(audio_device_config.ep1.core),
                        .bDescriptorType  = DTYPE_Endpoint,
                        .bEndpointAddress = AUDIO_OUT_ENDPOINT,
                        .bmAttributes     = 5,
                        .wMaxPacketSize   = 384,
                        .bInterval        = 1,
                        .bRefresh         = 0,
                        .bSyncAddr        = AUDIO_IN_ENDPOINT,
                },
                .audio = {
                        .bLength = sizeof(audio_device_config.ep1.audio),
                        .bDescriptorType = AUDIO_DTYPE_CSEndpoint,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSEndpoint_General,
                        .bmAttributes = 1,
                        .bLockDelayUnits = 0,
                        .wLockDelay = 0,
                }
        },
        .ep2 = {
                .bLength          = sizeof(audio_device_config.ep2),
                .bDescriptorType  = 0x05,
                .bEndpointAddress = AUDIO_IN_ENDPOINT,
                .bmAttributes     = 0x11,
                .wMaxPacketSize   = 3,
                .bInterval        = 0x01,
                .bRefresh         = 2,
                .bSyncAddr        = 0,
        },
};

static struct usb_interface ac_interface;
static struct usb_interface as_op_interface;
static struct usb_endpoint ep_op_out, ep_op_sync;

static const struct usb_device_descriptor boot_device_descriptor = {
        .bLength            = 18,
        .bDescriptorType    = 0x01,
        .bcdUSB             = 0x0110,
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = 0x40,
        .idVendor           = VENDOR_ID,
        .idProduct          = PRODUCT_ID,
        .bcdDevice          = 0x0200,
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x03,
        .bNumConfigurations = 0x01,
};

const char *_get_descriptor_string(uint index) {
    if (index <= count_of(descriptor_strings)) {
        return descriptor_strings[index - 1];
    } else {
        return "";
    }
}

static struct {
    uint32_t freq;
    int16_t volume;
    int16_t vol_mul;
    bool mute;
} audio_state = {
        .freq = 44100,
};

static volatile uint8_t clock_176mhz = 0;

#define AUDIO_BUFFER_COUNT 8

static struct audio_buffer_pool *producer_pool;

volatile uint32_t pio_samples_dma = 0, pio_prev_samples_dma = 0;
int sof_runs = 0, sof_avg = 0;
int overruns = 0;

#define SOF_AVG_BUF_SIZE 50
#define SOF_AVG_BUF_SIZE_F 50.0f
static volatile uint8_t sof_dma_buf[SOF_AVG_BUF_SIZE];
static volatile uint8_t sof_dma_buf_pos = 0, sof_dma_buf_filled = 0;

static volatile uint32_t rate = 48000;

void __not_in_flash_func(usb_sof_irq)(void) {
    return;
    // handle feedback sample rate calculations @ SOF to sync up with USB clock
    uint32_t new_samples = pio_samples_dma - pio_prev_samples_dma;
    pio_prev_samples_dma = pio_samples_dma;

    // we're racing audio_spdif_dma_irq_handler on the other core here
    sof_dma_buf[sof_dma_buf_pos] = new_samples;

    sof_dma_buf_pos++;
    if(sof_dma_buf_pos == SOF_AVG_BUF_SIZE) {
        sof_dma_buf_pos = 0;
        sof_dma_buf_filled = 1;
    }

    if(sof_dma_buf_filled) {
        uint32_t sof_avg = 0;
        for(uint8_t i = 0; i < SOF_AVG_BUF_SIZE; i++) {
            sof_avg += sof_dma_buf[i];
        }

        rate = (uint32_t) ((((float)sof_avg) / SOF_AVG_BUF_SIZE_F) * 192.0f * 1000.0f);
    }

    //        // 0.230 for 44.1k, 0.250 for 48k, 0.500 for 96k
    //        // multiply by 192 (spdif frame size) to get rate


    //gpio_put(18, !gpio_get(18));
}

static void __not_in_flash_func(_as_audio_packet)(struct usb_endpoint *ep) {
    assert(ep->current_transfer);
    struct usb_buffer *usb_buffer = usb_current_out_packet_buffer(ep);
    DEBUG_PINS_SET(audio_timing, 1);
    // todo deal with blocking correctly

    gpio_put(25, 1);
    struct audio_buffer *audio_buffer = take_audio_buffer(producer_pool, true);
    gpio_put(25, 0);

    DEBUG_PINS_CLR(audio_timing, 1);
    //assert(!(usb_buffer->data_len & 3u));
    audio_buffer->sample_count = usb_buffer->data_len / 4;
    //assert(audio_buffer->sample_count);
    //assert(audio_buffer->max_sample_count >= audio_buffer->sample_count);
    uint16_t vol_mul = audio_state.vol_mul;
    int16_t *out = (int16_t *) audio_buffer->buffer->bytes;
    int16_t *in = (int16_t *) usb_buffer->data;

    memcpy(out, in, usb_buffer->data_len);

    //for (int i = 0; i < audio_buffer->sample_count * 2; i++) {
    //    out[i] = (int16_t) ((in[i] * vol_mul) >> 15u);
    //}

    spectrum_consume_samples(out, audio_buffer->sample_count, audio_state.freq);

    biquad_eq_process_inplace(out, audio_buffer->sample_count);

    give_audio_buffer(producer_pool, audio_buffer);

    //gpio_put(25, 0);

    // keep on truckin'
    usb_grow_transfer(ep->current_transfer, 1);
    usb_packet_done(ep);
}

static void __not_in_flash_func(_as_sync_packet)(struct usb_endpoint *ep) {
    assert(ep->current_transfer);
    DEBUG_PINS_SET(audio_timing, 2);
    DEBUG_PINS_CLR(audio_timing, 2);
    struct usb_buffer *buffer = usb_current_in_packet_buffer(ep);
    assert(buffer->data_max >= 3);
    buffer->data_len = 3;

    int32_t diff = audio_state.freq - rate;

    // todo lie thru our teeth for now
    uint feedback = (audio_state.freq << 14u) / 1000u;
    //uint feedback = (rate << 14u) / 1000u;
    //uint feedback = ((audio_state.freq + diff) << 14u) / 1000u;

    buffer->data[0] = feedback;
    buffer->data[1] = feedback >> 8u;
    buffer->data[2] = feedback >> 16u;

    // keep on truckin'
    usb_grow_transfer(ep->current_transfer, 1);
    usb_packet_done(ep);

    //    sof_runs++;
    //    if(sof_runs == 50) {
    //        sof_runs = 0;
    //        //sof_avg = 0;
    //
    //        //printf("%d\n", audio_state.freq + diff);
    //        printf("%d %d\n", rate, overruns);
    //    }
}

static const struct usb_transfer_type as_transfer_type = {
        .on_packet = _as_audio_packet,
        .initial_packet_count = 1,
};

static const struct usb_transfer_type as_sync_transfer_type = {
        .on_packet = _as_sync_packet,
        .initial_packet_count = 1,
};

static struct usb_transfer as_transfer;
static struct usb_transfer as_sync_transfer;

static bool do_get_current(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_CUR\n");

    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
        case FEATURE_MUTE_CONTROL: {
            usb_start_tiny_control_in_transfer(audio_state.mute, 1);
            return true;
        }
        case FEATURE_VOLUME_CONTROL: {
            /* Current volume. See UAC Spec 1.0 p.77 */
            usb_start_tiny_control_in_transfer(audio_state.volume, 2);
            return true;
        }
        }
    } else if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_ENDPOINT) {
        if ((setup->wValue >> 8u) == ENDPOINT_FREQ_CONTROL) {
            /* Current frequency */
            usb_start_tiny_control_in_transfer(audio_state.freq, 3);
            return true;
        }
    }
    return false;
}

// todo this seemed like aood guess, but is not correct
uint16_t db_to_vol[91] = {
        0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0002, 0x0002,
        0x0002, 0x0002, 0x0003, 0x0003, 0x0004, 0x0004, 0x0005, 0x0005,
        0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x000d, 0x000e,
        0x0010, 0x0012, 0x0014, 0x0017, 0x001a, 0x001d, 0x0020, 0x0024,
        0x0029, 0x002e, 0x0033, 0x003a, 0x0041, 0x0049, 0x0052, 0x005c,
        0x0067, 0x0074, 0x0082, 0x0092, 0x00a4, 0x00b8, 0x00ce, 0x00e7,
        0x0104, 0x0124, 0x0147, 0x016f, 0x019c, 0x01ce, 0x0207, 0x0246,
        0x028d, 0x02dd, 0x0337, 0x039b, 0x040c, 0x048a, 0x0518, 0x05b7,
        0x066a, 0x0732, 0x0813, 0x090f, 0x0a2a, 0x0b68, 0x0ccc, 0x0e5c,
        0x101d, 0x1214, 0x1449, 0x16c3, 0x198a, 0x1ca7, 0x2026, 0x2413,
        0x287a, 0x2d6a, 0x32f5, 0x392c, 0x4026, 0x47fa, 0x50c3, 0x5a9d,
        0x65ac, 0x7214, 0x7fff
};

// actually windows doesn't seem to like this in the middle, so set top range to 0db
#define CENTER_VOLUME_INDEX 91

#define ENCODE_DB(x) ((uint16_t)(int16_t)((x)*256))

#define MIN_VOLUME           ENCODE_DB(-CENTER_VOLUME_INDEX)
#define DEFAULT_VOLUME       ENCODE_DB(0)
#define MAX_VOLUME           ENCODE_DB(count_of(db_to_vol)-CENTER_VOLUME_INDEX)
#define VOLUME_RESOLUTION    ENCODE_DB(1)

static bool do_get_minimum(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_MIN\n");
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
        case FEATURE_VOLUME_CONTROL: {
            usb_start_tiny_control_in_transfer(MIN_VOLUME, 2);
            return true;
        }
        }
    }
    return false;
}

static bool do_get_maximum(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_MAX\n");
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
        case FEATURE_VOLUME_CONTROL: {
            usb_start_tiny_control_in_transfer(MAX_VOLUME, 2);
            return true;
        }
        }
    }
    return false;
}

static bool do_get_resolution(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_RES\n");
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
        case FEATURE_VOLUME_CONTROL: {
            usb_start_tiny_control_in_transfer(VOLUME_RESOLUTION, 2);
            return true;
        }
        }
    }
    return false;
}

static struct audio_control_cmd {
    uint8_t cmd;
    uint8_t type;
    uint8_t cs;
    uint8_t cn;
    uint8_t unit;
    uint8_t len;
} audio_control_cmd_t;

static void _audio_reconfigure() {
    switch (audio_state.freq) {
    case 44100:
    case 48000:
    case 96000:
        break;
    default:
        audio_state.freq = 44100;
    }

    // if we're going to 48/96 from 44.1, clock up the system PLL
    if((audio_state.freq == 48000 || audio_state.freq == 96000) && clock_176mhz) {
        // 192.0MHz
        set_sys_clock_pll(1536000000, 4, 2);
        clock_176mhz = 0;
    } else if(audio_state.freq == 44100 && !clock_176mhz) {
        // 176.57142857142858 MHz, closest PLL frequency near 176.4 MHz (multiple of 44.1)
        // results in a PIO frequency of 44098.7582418 when rounding up in update_pio_frequency
        set_sys_clock_pll(1236000000, 7, 1);
        clock_176mhz = 1;
    }

    // todo hack overwriting const
    ((struct audio_format *) producer_pool->format)->sample_freq = audio_state.freq;

    biquad_eq_set_fs(audio_state.freq);

    rate = audio_state.freq;
    sof_dma_buf_filled = 0;
    sof_dma_buf_pos = 0;
}

static void audio_set_volume(int16_t volume) {
    audio_state.volume = volume;
    // todo interpolate
    volume += CENTER_VOLUME_INDEX * 256;
    if (volume < 0) volume = 0;
    if (volume >= count_of(db_to_vol) * 256) volume = count_of(db_to_vol) * 256 - 1;

    //UI_SetVolume(((uint16_t)volume) >> 8u); // 0 to 23296 -> 0 to 91

    audio_state.vol_mul = db_to_vol[((uint16_t)volume) >> 8u];
    //    printf("VOL MUL %04x\n", audio_state.vol_mul);
}

static void audio_cmd_packet(struct usb_endpoint *ep) {
    assert(audio_control_cmd_t.cmd == AUDIO_REQ_SetCurrent);
    struct usb_buffer *buffer = usb_current_out_packet_buffer(ep);
    audio_control_cmd_t.cmd = 0;
    if (buffer->data_len >= audio_control_cmd_t.len) {
        if (audio_control_cmd_t.type == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
            switch (audio_control_cmd_t.cs) {
            case FEATURE_MUTE_CONTROL: {
                audio_state.mute = buffer->data[0];
                usb_warn("Set Mute %d\n", buffer->data[0]);
                break;
            }
            case FEATURE_VOLUME_CONTROL: {
                audio_set_volume(*(int16_t *) buffer->data);
                break;
            }
            }

        } else if (audio_control_cmd_t.type == USB_REQ_TYPE_RECIPIENT_ENDPOINT) {
            if (audio_control_cmd_t.cs == ENDPOINT_FREQ_CONTROL) {
                uint32_t new_freq = (*(uint32_t *) buffer->data) & 0x00ffffffu;
                usb_warn("Set freq %d\n", new_freq == 0xffffffu ? -1 : (int) new_freq);

                if (audio_state.freq != new_freq) {
                    audio_state.freq = new_freq;
                    _audio_reconfigure();
                }
            }
        }
    }
    usb_start_empty_control_in_transfer_null_completion();
    // todo is there error handling?
}


static const struct usb_transfer_type _audio_cmd_transfer_type = {
        .on_packet = audio_cmd_packet,
        .initial_packet_count = 1,
};

static bool as_set_alternate(struct usb_interface *interface, uint alt) {
    assert(interface == &as_op_interface);
    usb_warn("SET ALTERNATE %d\n", alt);
    return alt < 2;
}

static bool do_set_current(struct usb_setup_packet *setup) {
#ifndef NDEBUG
    usb_warn("AUDIO_REQ_SET_CUR\n");
#endif

    if (setup->wLength && setup->wLength < 64) {
        audio_control_cmd_t.cmd = AUDIO_REQ_SetCurrent;
        audio_control_cmd_t.type = setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;
        audio_control_cmd_t.len = (uint8_t) setup->wLength;
        audio_control_cmd_t.unit = setup->wIndex >> 8u;
        audio_control_cmd_t.cs = setup->wValue >> 8u;
        audio_control_cmd_t.cn = (uint8_t) setup->wValue;
        usb_start_control_out_transfer(&_audio_cmd_transfer_type);
        return true;
    }
    return false;
}

static bool ac_setup_request_handler(__unused struct usb_interface *interface, struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        switch (setup->bRequest) {
        case AUDIO_REQ_SetCurrent:
            return do_set_current(setup);

        case AUDIO_REQ_GetCurrent:
            return do_get_current(setup);

        case AUDIO_REQ_GetMinimum:
            return do_get_minimum(setup);

        case AUDIO_REQ_GetMaximum:
            return do_get_maximum(setup);

        case AUDIO_REQ_GetResolution:
            return do_get_resolution(setup);

        default:
            break;
        }
    }
    return false;
}

bool _as_setup_request_handler(__unused struct usb_endpoint *ep, struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        switch (setup->bRequest) {
        case AUDIO_REQ_SetCurrent:
            return do_set_current(setup);

        case AUDIO_REQ_GetCurrent:
            return do_get_current(setup);

        case AUDIO_REQ_GetMinimum:
            return do_get_minimum(setup);

        case AUDIO_REQ_GetMaximum:
            return do_get_maximum(setup);

        case AUDIO_REQ_GetResolution:
            return do_get_resolution(setup);

        default:
            break;
        }
    }
    return false;
}

void usb_sound_card_init() {
    //msd_interface.setup_request_handler = msd_setup_request_handler;
    usb_interface_init(&ac_interface, &audio_device_config.ac_interface, NULL, 0, true);
    ac_interface.setup_request_handler = ac_setup_request_handler;

    static struct usb_endpoint *const op_endpoints[] = {
            &ep_op_out, &ep_op_sync
    };
    usb_interface_init(&as_op_interface, &audio_device_config.as_op_interface, op_endpoints, count_of(op_endpoints), true);
    as_op_interface.set_alternate_handler = as_set_alternate;
    ep_op_out.setup_request_handler = _as_setup_request_handler;
    as_transfer.type = &as_transfer_type;
    usb_set_default_transfer(&ep_op_out, &as_transfer);
    as_sync_transfer.type = &as_sync_transfer_type;
    usb_set_default_transfer(&ep_op_sync, &as_sync_transfer);

    static struct usb_interface *const boot_device_interfaces[] = {
            &ac_interface,
            &as_op_interface,
    };
    __unused struct usb_device *device = usb_device_init(&boot_device_descriptor, &audio_device_config.descriptor,
            boot_device_interfaces, count_of(boot_device_interfaces),
            _get_descriptor_string);
    assert(device);
    audio_set_volume(DEFAULT_VOLUME);
    _audio_reconfigure();
    //    device->on_configure = _on_configure;
    usb_device_start();
}

// initialize for 48k we allow changing later
struct audio_format audio_format_48k = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .sample_freq = 48000,
        .channel_count = 2,
};

struct audio_spdif_config config = {
        .pin = PICO_AUDIO_SPDIF_PIN,
        .dma_channel = 0,
        .pio_sm = 0,
};

struct audio_buffer_format producer_format = {
        .format = &audio_format_48k,
        .sample_stride = 4
};

// Core split:
// core 0 handles high-priority tasks: USB and SPDIF IRQs
// core 1 handles low-priority tasks: LVGL, OLED, WM8805 polling and TPA6130 volume

static void core1_worker() {
    // Init the oled twice, in case the first time glitched
    busy_wait_ms(50);
    oled_init();
    busy_wait_ms(50);
    oled_init();

    // Init LVGL and all screens
    ui_init();

    // Start up the SPDIF PIO (core 1)
    //irq_set_priority(DMA_IRQ_0 + PICO_AUDIO_SPDIF_DMA_IRQ, PICO_HIGHEST_IRQ_PRIORITY);
    //audio_spdif_set_enabled(true);

    while(1) {
        // suspend if we had inited usb once (otherwise can't distinguish between 5V only)
        // only do that if the USB input is selected?
//        if(usb_hw->sie_status & USB_SIE_STATUS_SUSPENDED_BITS && usb_host_seen && !suspended) {
//
//            ssd1306_SetDisplayOn(0);
//            // we're suspended, TODO turn off the OLED
//            //ui_set_sr_text("SUSPEND");
//
//            // turn off underrun led
//            gpio_put(18, 0);
//        } else if(usb_host_seen && suspended){
//            // waking up
//            ssd1306_SetDisplayOn(0);
//        }

        ui_loop();

        __wfe();
    }
}

void core0_init() {
    // Set regulator into PWM mode
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);

    // Init board LED
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);

    // Init red LED
    gpio_init(18);
    gpio_set_dir(18, GPIO_OUT);

    // Grant high bus priority to the DMA
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // Init EQ
    biquad_eq_init();

    producer_pool = audio_new_producer_pool(&producer_format, AUDIO_BUFFER_COUNT, 192);

    const struct audio_format *output_format;
    output_format = audio_spdif_setup(&audio_format_48k, &config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    bool __unused ok = audio_spdif_connect_extra(producer_pool, false, AUDIO_BUFFER_COUNT / 2, NULL);
    assert(ok);

    //irq_set_priority(USBCTRL_IRQ, 0x40);
    usb_sound_card_init();

    // Init the WM8805 SPDIF receiver
    wm8805_init();

    // Init the TPA6130 headphone amp
    tpa6130_init();

    // Start up the SPDIF PIO (core 0)
    irq_set_priority(DMA_IRQ_0 + PICO_AUDIO_SPDIF_DMA_IRQ, PICO_HIGHEST_IRQ_PRIORITY);
    audio_spdif_set_enabled(true);
}

int main(void) {
    // 17.2032 MHz is a common multiple for all three sample rates we do:
    // https://www.electronicdesign.com/technologies/embedded-revolution/article/21801786/achieving-bitperfect-usb-audio
    // 17.2032*10=172.032MHz is not exactly doable by the PLL but 172.0MHz is
    // if we just ignore 44.1 instead, 192000 is a nice integer multiple for 48 and 96
    // ideally the core would run at 96MHz to avoid running overclocked, but this seems to be fine
    //set_sys_clock_khz(192000, true);

    // 192.0MHz
    set_sys_clock_pll(1536000000, 4, 2);

    // Debug UART
    stdout_uart_init();

    // Init H/W and USB audio
    core0_init();

    // Run low-priority UI core
    multicore_launch_core1(core1_worker);

    while (1) {
        __wfe();
    }
}
