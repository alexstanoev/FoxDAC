/*
 * spectrum.c
 *
 *  Created on: 23 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "stdint.h"

#include "lvgl/lvgl.h"

#include "dac_lvgl_ui.h"
#include "spectrum.h"

#include "kissfft/kiss_fftr.h"

#define FFT_SIZE 1024
#define FFT_SIZE_F (1024.0f)

#define NUM_BARS 41
#define NUM_BARS_F 41.0f

#define MIN_BAR_FREQ 50.0f
#define MAX_BAR_FREQ 20000.0f

#define FFT_BIN_SIZE (sample_rate / FFT_SIZE)

static uint8_t spectrum_running = 0;
static uint32_t sample_rate = 48000;

static int interp_step = 0;
static const int interp_times = 3;

static uint16_t sample_buf[FFT_SIZE];
static volatile int sample_buf_pos = 0;

static kiss_fftr_cfg fft_cfg;
static kiss_fft_scalar fft_in[FFT_SIZE];
static kiss_fft_cpx fft_out[FFT_SIZE];

#define FFT_MEM_LEN_BYTES 16384
static uint8_t fft_mem[FFT_MEM_LEN_BYTES];
static size_t fft_mem_len = FFT_MEM_LEN_BYTES;

static lv_obj_t * chart;
static lv_coord_t value_array[NUM_BARS];

static lv_coord_t target_value_array[NUM_BARS];
static lv_coord_t old_value_array[NUM_BARS];

static lv_timer_t * spectrum_timer;

lv_obj_t * Spectrum;

static float mapRange(float a1, float a2, float b1, float b2, float s) {
    return b1 + (s - a1) * (b2 - b1) / (a2 - a1);
}

static float hanningWindow(short in, size_t i, size_t s) {
    return in * 0.5f * (1.0f - cosf(2.0f * ((float)M_PI) * (float)(i) / (float)(s - 1.0f)));
}

// called from usb_spdif.c, runs in IRQ on core 0
void spectrum_consume_samples(int16_t* samples, uint32_t sample_count, uint32_t rate) {
    if(!spectrum_running || sample_buf_pos == FFT_SIZE || interp_step < interp_times) return;

    for (int i = 0; i < sample_count * 2; i += 2) {
        // if the sample rate is 96000, decimate by 2 to get 48000
        // this will alias frequencies > 24k but big deal, it's not meant to be a real spectrum analyser
        if(rate == 96000) {
            // skip samples
            if(i + 2 < sample_count * 2) {
                i += 2;
            }

            // decimated down to 48k
            sample_rate = 48000;
        } else {
            sample_rate = rate;
        }

        // average channels
        sample_buf[sample_buf_pos++] = (samples[i] + samples[i + 1]) / 2;

        if(sample_buf_pos == FFT_SIZE) {
            return;
        }
    }
}

void spectrum_loop(void) {
    if(!spectrum_running || sample_buf_pos != FFT_SIZE || interp_step < interp_times) return;

    // apply window
    for (int i = 0; i < sample_buf_pos; i ++) {
        fft_in[i] = hanningWindow((float) sample_buf[i], i, FFT_SIZE);
    }

    // compute FFT
    kiss_fftr(fft_cfg, fft_in, fft_out);

    for (int i = 0; i < NUM_BARS; i++) {
        // log scale, bins spaced at:
        // min * (max/min) ^ x
        float bar_freq = MIN_BAR_FREQ * powf((MAX_BAR_FREQ / MIN_BAR_FREQ), i / NUM_BARS_F);
        float bar_freq_next = ((i + 1) == NUM_BARS) ? MAX_BAR_FREQ : (MIN_BAR_FREQ * powf((MAX_BAR_FREQ / MIN_BAR_FREQ), (i + 1) / NUM_BARS_F));

        int startbin = floorf(bar_freq / FFT_BIN_SIZE);
        int endbin = ceilf(bar_freq_next / FFT_BIN_SIZE);

        //printf("%d %f %f %d %d\n", i, bar_freq, bar_freq_next, startbin, endbin);

        // resolution at the first bars is too low, just draw some bins directly
        if(startbin == 1 && endbin == 2) {
            startbin = i;
            endbin = i + 1;
        }

        // rebin
        float power_sum = 0;
        for(int j = startbin; j < endbin; j++) {
            power_sum += sqrtf(fft_out[j].r * fft_out[j].r + fft_out[j].i * fft_out[j].i) / FFT_SIZE_F;
        }

        power_sum = 20 * log10f(power_sum / (endbin - startbin));

        //float power = 20 * log10f(sqrtf(fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i) / FFT_SIZE_F);

        // 96dB dynamic range with 16 bits + fudge window loss
        old_value_array[i] = target_value_array[i];
        target_value_array[i] = mapRange(0, 90, 0, 64, power_sum);
    }

    interp_step = 0;
    sample_buf_pos = 0;
}

// smoothstep lerp
static void redraw_bars(lv_timer_t * timer) {
    if(interp_step < interp_times) {
        float fac = ((float) interp_step) / ((float) interp_times);
        float lerp = fac * fac * (3.0f - 2.0f * fac);

        for(int i = 0; i < NUM_BARS; i++) {
            float oldval = ((float) old_value_array[i]);
            float newval = ((float) target_value_array[i]);

            float step = (oldval * lerp) + (newval * (1 - lerp));

            value_array[i] = step;
        }

        interp_step++;
    }

    lv_chart_refresh(chart);
}

void spectrum_init(void) {
    fft_cfg = kiss_fftr_alloc(FFT_SIZE, false, fft_mem, &fft_mem_len);

    Spectrum = lv_obj_create(NULL);

    chart = lv_chart_create(Spectrum);
    lv_obj_set_size(chart, 127, 64);
    lv_obj_center(chart);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 64);

    lv_chart_set_div_line_count(chart, 0, 0);

    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);

    lv_obj_set_style_pad_column(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_column(chart, 0, LV_PART_ITEMS);

    lv_obj_set_style_pad_left(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(chart, 0, LV_PART_MAIN);

    lv_color_t white = { .full = 1 };
    lv_chart_series_t * ser = lv_chart_add_series(chart, white, LV_CHART_AXIS_PRIMARY_Y);

    lv_chart_set_ext_y_array(chart, ser, value_array);
    lv_chart_set_point_count(chart, 41);

    lv_coord_t * ser_array =  lv_chart_get_y_array(chart, ser);

    for(int i = 0; i < NUM_BARS; i++) {
        ser_array[i] = 0;
        target_value_array[i] = 0;
    }

    lv_chart_refresh(chart);

    spectrum_timer = lv_timer_create(redraw_bars, 20, NULL);
    lv_timer_pause(spectrum_timer);
}

void spectrum_start(void) {
    sample_buf_pos = 0;
    spectrum_running = 1;
    lv_scr_load_anim(Spectrum, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_resume(spectrum_timer);
}

void spectrum_stop(void) {
    spectrum_running = 0;
    lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_pause(spectrum_timer);
}

