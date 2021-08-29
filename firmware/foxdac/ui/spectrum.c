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

static uint8_t spectrum_running = 0;

lv_obj_t * Spectrum;

lv_obj_t * chart;
lv_coord_t value_array[41];

lv_coord_t target_value_array[41];
lv_coord_t old_value_array[41];

lv_timer_t * spectrum_timer;

static int runs = 0;

static int interp_step = 0;
static const int interp_times = 3;

#define FFT_SIZE 80

uint16_t sample_buf[FFT_SIZE];
volatile int sample_buf_pos = 0;

kiss_fft_scalar fft_in[FFT_SIZE];
int fft_in_pos = 0;

kiss_fft_cpx fft_out[FFT_SIZE];

#define FFT_MEM_LEN_BYTES 8192
uint8_t fft_mem[FFT_MEM_LEN_BYTES];
size_t fft_mem_len = FFT_MEM_LEN_BYTES;

kiss_fftr_cfg fft_cfg;

static float mapRange(float a1, float a2, float b1, float b2, float s) {
    return b1 + (s-a1)*(b2-b1)/(a2-a1);
}

static float hanningWindow(short in, size_t i, size_t s) {
    return in*0.5f*(1.0f-cosf(2.0f*M_PI*(float)(i)/(float)(s-1.0f)));
}

// called from usb_spdif.c, runs in IRQ
void spectrum_consume_samples(int16_t* samples, uint32_t sample_count, uint32_t rate) {
    if(!spectrum_running || sample_buf_pos == FFT_SIZE) return;

    for (int i = 0; i < sample_count * 2; i += 2) {
        // if the sample rate is 96000, decimate by 2 to get 48000
        // this will alias frequencies > 24k but big deal, it's not meant to be a real spectrum analyser
        if(rate == 96000) {
            // skip samples
            if(i + 2 < sample_count * 2) {
                i += 2;
            }
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

    fft_in_pos = 0;
    // apply window
    for (int i = 0; i < sample_buf_pos; i ++) {
        fft_in[fft_in_pos++] = hanningWindow((float) sample_buf[i], i, FFT_SIZE);
    }

    // compute fast fourier transform
    kiss_fftr(fft_cfg, fft_in, fft_out);

    // TODO log frequency axis
    // compute power and calculate max freq component
    float max_power = 0;
    int max_idx = 0;
    for (int i = 0; i < 40; i++) {
        float power = 20 * log10f(sqrtf(fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i) / 40.0f);

        //power -= 20;
        if(power < 0) power = 0;

        //printf("%d : %f %f\n", i, power, mapRange(0, 130, 0, 64, power));

        // 96dB dynamic range with 16 bits
        old_value_array[i] = target_value_array[i];
        target_value_array[i] = mapRange(0, 96, 0, 64, power);
    }

    // empty bar with size 80 FFT
    old_value_array[41] = 0;
    target_value_array[41] = 0;

    interp_step = 0;

    fft_in_pos = 0;
    sample_buf_pos = 0;
}

// smoothstep lerp
static void redraw_bars(lv_timer_t * timer) {
    if(interp_step < interp_times) {
        float fac = ((float) interp_step) / ((float) interp_times);
        float lerp = fac * fac * (3.0f - 2.0f * fac);

        for(int i = 0; i < 41; i++) {
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

    for(int i = 0; i < 41; i++) {
        ser_array[i] = 0;
        target_value_array[i] = 0;
    }

    lv_chart_refresh(chart);

    spectrum_timer = lv_timer_create(redraw_bars, 30, NULL);
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

