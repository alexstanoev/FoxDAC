/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jerzy Kasenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/audio.h"
#include "pico/audio_spdif.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include <math.h>

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTOTYPES
//--------------------------------------------------------------------+

#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE     48000
#endif

/* Blink pattern
 * - 25 ms   : streaming data
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum
{
  BLINK_STREAMING = 25,
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

enum
{
  VOLUME_CTRL_0_DB = 0,
  VOLUME_CTRL_10_DB = 2560,
  VOLUME_CTRL_20_DB = 5120,
  VOLUME_CTRL_30_DB = 7680,
  VOLUME_CTRL_40_DB = 10240,
  VOLUME_CTRL_50_DB = 12800,
  VOLUME_CTRL_60_DB = 15360,
  VOLUME_CTRL_70_DB = 17920,
  VOLUME_CTRL_80_DB = 20480,
  VOLUME_CTRL_90_DB = 23040,
  VOLUME_CTRL_100_DB = 25600,
  VOLUME_CTRL_SILENCE = 0x8000,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// Audio controls
// Current states
int8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];       // +1 for master channel 0
int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];    // +1 for master channel 0

// Buffer for microphone data
int16_t mic_buf[1000];
// Buffer for speaker data
int16_t spk_buf[1000];
// Speaker data size received in the last frame
int spk_data_size;


static struct audio_buffer_pool *producer_pool;

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

void led_blinking_task(void);
void audio_task(void);



#define SINE_WAVE_TABLE_LEN 2048
#define SAMPLES_PER_BUFFER 256

static int16_t sine_wave_table[SINE_WAVE_TABLE_LEN];
uint32_t step = 0x600000;
uint32_t pos = 0;
uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
uint vol = 128;



/*------------- MAIN -------------*/
int main_usb(void)
{

    for (int i = 0; i < SINE_WAVE_TABLE_LEN; i++) {
        sine_wave_table[i] = 32767 * cosf(i * 2 * (float) (M_PI / SINE_WAVE_TABLE_LEN));
    }



  //board_init();

  //tusb_init();

  producer_pool = audio_new_producer_pool(&producer_format, 16, SAMPLES_PER_BUFFER);

  const struct audio_format *output_format;
  output_format = audio_spdif_setup(&audio_format_48k, &config);
  if (!output_format) {
      panic("PicoAudio: Unable to open audio device.\n");
  }

  bool ok = audio_spdif_connect_extra(producer_pool, false, 4, NULL);
  assert(ok);

  // CORE 1?
  //audio_spdif_set_enabled(true);

  TU_LOG1("Headset running\r\n");

  //while (1)
  //{
  //  tud_task(); // TinyUSB device task
    //audio_task();
    //led_blinking_task();
  //}

  return 0;
}

void loop_sine() {
    //int n_bytes_received = 192;



    struct audio_buffer *audio_buffer = take_audio_buffer(producer_pool, true);

    gpio_put(22, 1);

    //int rxd_size = tud_audio_read(audio_buffer->buffer->bytes, n_bytes_received);

    //printf("%u %u\n", n_bytes_received, rxd_size);

    //audio_buffer->sample_count = n_bytes_received / 4;
    //assert(audio_buffer->sample_count);
    //assert(audio_buffer->max_sample_count >= audio_buffer->sample_count);


  int j = 0;
    int16_t *samples = (int16_t *) audio_buffer->buffer->bytes;
    for (uint i = 0; i < audio_buffer->max_sample_count; i++) {
        samples[j++] = (vol * sine_wave_table[pos >> 16u]) >> 8u;
        samples[j++] = (vol * sine_wave_table[pos >> 16u]) >> 8u;
        pos += step;
        if (pos >= pos_max) pos -= pos_max;
    }

    audio_buffer->sample_count = audio_buffer->max_sample_count;

    //spectrum_consume_samples(out, audio_buffer->sample_count);

    give_audio_buffer(producer_pool, audio_buffer);

    gpio_put(22, 0);
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void)remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

typedef struct TU_ATTR_PACKED
{
  union
  {
    struct TU_ATTR_PACKED
    {
      uint8_t recipient :  5; ///< Recipient type tusb_request_recipient_t.
      uint8_t type      :  2; ///< Request type tusb_request_type_t.
      uint8_t direction :  1; ///< Direction type. tusb_dir_t
    } bmRequestType_bit;

    uint8_t bmRequestType;
  };

  uint8_t bRequest;  ///< Request type audio_cs_req_t
  uint8_t bChannelNumber;
  uint8_t bControlSelector;
  union
  {
    uint8_t bInterface;
    uint8_t bEndpoint;
  };
  uint8_t bEntityID;
  uint16_t wLength;
} audio_control_request_t;

// Helper for clock get requests
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);

  // Example supports only single frequency, same value will be used for current value and range
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
  {
    if (request->bRequest == AUDIO_CS_REQ_CUR)
    {
      TU_LOG2("Clock get current freq %u\r\n", AUDIO_SAMPLE_RATE);

      audio_control_cur_4_t curf = { tu_htole32(AUDIO_SAMPLE_RATE) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
    }
    else if (request->bRequest == AUDIO_CS_REQ_RANGE)
    {
      audio_control_range_4_n_t(1) rangef =
      {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { tu_htole32(AUDIO_SAMPLE_RATE), tu_htole32(AUDIO_SAMPLE_RATE), 0}
      };
      TU_LOG2("Clock get freq range (%d, %d, %d)\r\n", (int)rangef.subrange[0].bMin, (int)rangef.subrange[0].bMax, (int)rangef.subrange[0].bRes);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
    }
  }
  else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
           request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t cur_valid = { .bCur = 1 };
    TU_LOG2("Clock get is valid %u\r\n", cur_valid.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
  }
  TU_LOG1("Clock get request not supported, entity = %u, selector = %u, request = %u\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);
  return false;
}

// Helper for feature unit get requests
static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t mute1 = { .bCur = mute[request->bChannelNumber] };
    TU_LOG2("Get channel %u mute %d\r\n", request->bChannelNumber, mute1.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
  }
  else if (UAC2_ENTITY_SPK_FEATURE_UNIT && request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
  {
    if (request->bRequest == AUDIO_CS_REQ_RANGE)
    {
      audio_control_range_2_n_t(1) range_vol = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { .bMin = tu_htole16(-VOLUME_CTRL_50_DB), tu_htole16(VOLUME_CTRL_0_DB), tu_htole16(256) }
      };
      TU_LOG2("Get channel %u volume range (%d, %d, %u) dB\r\n", request->bChannelNumber,
              range_vol.subrange[0].bMin / 256, range_vol.subrange[0].bMax / 256, range_vol.subrange[0].bRes / 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
    }
    else if (request->bRequest == AUDIO_CS_REQ_CUR)
    {
      audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(volume[request->bChannelNumber]) };
      TU_LOG2("Get channel %u volume %u dB\r\n", request->bChannelNumber, cur_vol.bCur);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
    }
  }
  TU_LOG1("Feature unit get request not supported, entity = %u, selector = %u, request = %u\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

// Helper for feature unit set requests
static bool tud_audio_feature_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
  (void)rhport;

  TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));

    mute[request->bChannelNumber] = ((audio_control_cur_1_t *)buf)->bCur;

    TU_LOG2("Set channel %d Mute: %d\r\n", request->bChannelNumber, mute[request->bChannelNumber]);

    return true;
  }
  else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));

    volume[request->bChannelNumber] = ((audio_control_cur_2_t const *)buf)->bCur;

    TU_LOG2("Set channel %d volume: %d dB\r\n", request->bChannelNumber, volume[request->bChannelNumber] / 256);

    return true;
  }
  else
  {
    TU_LOG1("Feature unit set request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  audio_control_request_t *request = (audio_control_request_t *)p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return tud_audio_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
    return tud_audio_feature_unit_get_request(rhport, request);
  else
  {
    TU_LOG1("Get request not handled, entity = %d, selector = %d, request = %d\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
  }
  return false;
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
    return tud_audio_feature_unit_set_request(rhport, request, buf);

  TU_LOG1("Set request not handled, entity = %d, selector = %d, request = %d\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void)rhport;

  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  if (ITF_NUM_AUDIO_STREAMING_SPK == itf && alt == 0)
      blink_interval_ms = BLINK_MOUNTED;

  return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void)rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  TU_LOG2("Set interface %d alt %d\r\n", itf, alt);
  if (ITF_NUM_AUDIO_STREAMING_SPK == itf && alt != 0)
      blink_interval_ms = BLINK_STREAMING;

  return true;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
{
  (void)rhport;
  (void)func_id;
  (void)ep_out;
  (void)cur_alt_setting;

  gpio_put(22, 1);

  struct audio_buffer *audio_buffer = take_audio_buffer(producer_pool, true);

  int rxd_size = tud_audio_read(audio_buffer->buffer->bytes, n_bytes_received);

  //printf("%u %u\n", n_bytes_received, rxd_size);

  audio_buffer->sample_count = n_bytes_received / 4;
  assert(audio_buffer->sample_count);
  assert(audio_buffer->max_sample_count >= audio_buffer->sample_count);


int j = 0;
  int16_t *samples = (int16_t *) audio_buffer->buffer->bytes;
  for (uint i = 0; i < audio_buffer->sample_count; i++) {
      samples[j++] = (vol * sine_wave_table[pos >> 16u]) >> 8u;
      samples[j++] = (vol * sine_wave_table[pos >> 16u]) >> 8u;
      pos += step;
      if (pos >= pos_max) pos -= pos_max;
  }

  //spectrum_consume_samples(out, audio_buffer->sample_count);

  give_audio_buffer(producer_pool, audio_buffer);

  gpio_put(22, 0);

  //spk_data_size = tud_audio_read(spk_buf, n_bytes_received);
  return true;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void)rhport;
  (void)itf;
  (void)ep_in;
  (void)cur_alt_setting;

  // This callback could be used to fill microphone data separately
  return true;
}

//--------------------------------------------------------------------+
// AUDIO Task
//--------------------------------------------------------------------+

void audio_task(void)
{
  // When new data arrived, copy data from speaker buffer, to microphone buffer
  // and send it over
  if (spk_data_size)
  {
    int16_t *src = spk_buf;
    int16_t *limit = spk_buf + spk_data_size / 2;
    int16_t *dst = mic_buf;
    while (src < limit)
    {
      // Combine two channels into one
      int32_t left = *src++;
      int32_t right = *src++;
      *dst++ = (int16_t)((left + right) / 2);
    }
    tud_audio_write((uint8_t *)mic_buf, spk_data_size / 2);
    spk_data_size = 0;
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
//  static uint32_t start_ms = 0;
//  static bool led_state = false;
//
//  // Blink every interval ms
//  if (board_millis() - start_ms < blink_interval_ms) return;
//  start_ms += blink_interval_ms;
//
//  board_led_write(led_state);
//  led_state = 1 - led_state;
}
