/*
 * bargraph.c
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

static volatile uint8_t spectrum_running = 0;

lv_obj_t * Spectrum;

lv_obj_t * chart;
lv_coord_t value_array[41];


volatile lv_coord_t target_value_array[41];
volatile lv_coord_t old_value_array[41];

lv_timer_t * spectrum_timer;

static int runs = 0;

static volatile int j = 0;
static const int N = 3;

#define NSAMP 80

uint8_t cap_buf[NSAMP];
volatile kiss_fft_scalar fft_in[NSAMP];
volatile int fft_in_pos = 0;

volatile kiss_fft_cpx fft_out[NSAMP];

#define FFT_MEM_LEN_BYTES 10240
uint8_t fft_mem[FFT_MEM_LEN_BYTES];
size_t fft_mem_len = FFT_MEM_LEN_BYTES;

kiss_fftr_cfg fft_cfg;

float mapRange(float a1,float a2,float b1,float b2,float s)
{
    return b1 + (s-a1)*(b2-b1)/(a2-a1);
}

float hanningWindow(short in, size_t i, size_t s) {
    return in*0.5f*(1.0f-cosf(2.0f*M_PI*(float)(i)/(float)(s-1.0f)));
}

// called from usb_spdif.c, runs in IRQ
void spectrum_consume_samples(int16_t* samples, uint32_t sample_count) {
    if(!spectrum_running || fft_in_pos == NSAMP) return;

    for (int i = 0; i < sample_count * 2; i += 2) {
        // average channels
        fft_in[fft_in_pos++] = hanningWindow((float) samples[i], fft_in_pos - 1, NSAMP); //(((float) samples[i]) + ((float) samples[i + 1])) / 2.0f;

        //fft_in_pos++;

        //fft_in[fft_in_pos++] = (float) samples[i];

        if(fft_in_pos == NSAMP) {
            return;
        }
        //printf("%d %f \n", samples[i], fft_in[fft_in_pos - 1]);
    }
}

void spectrum_core0_loop(void) {
    if(!spectrum_running || fft_in_pos != NSAMP || j < N) return;

    // compute fast fourier transform
    kiss_fftr(fft_cfg, fft_in, fft_out);

    // compute power and calculate max freq component
    float max_power = 0;
    int max_idx = 0;
    // any frequency bin over NSAMP/2 is aliased (nyquist sampling theorum)
    // skip DC and go up to 41 instead of NSAMP/2
    for (int i = 0; i < 40; i++) {
        float power = 20 * log10(sqrtf(fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i) / 40.0f);

        power -= 20;
        if(power < 0) power = 0;

        //printf("%d : %f %f\n", i, power, mapRange(0, 130, 0, 64, power));

        old_value_array[i] = target_value_array[i];
        target_value_array[i] = mapRange(0, 100, 0, 64, power);
    }

    j = 0;

    fft_in_pos = 0;

    // should never get here
    //kiss_fft_free(cfg);
}




static void redraw_bars(lv_timer_t * timer) {
    //    runs++;
    //
    //    if(runs == 3) {
    //        runs = 0;
    //        j = 0;
    //        for(int i = 0; i < 41; i++) {
    //            old_value_array[i] = target_value_array[i];
    //            target_value_array[i] = lv_rand(0, 64);
    //        }
    //    }

    //spectrum_core0_loop();

    if(j < N) {
        float fac = ((float) j) / ((float) N);
        float lerp = fac * fac * (3.0f - 2.0f * fac);

        for(int i = 0; i < 41; i++) {
            float oldval = ((float) old_value_array[i]);
            float newval = ((float) target_value_array[i]);

            float step = (oldval * lerp) + (newval * (1 - lerp));

            value_array[i] = step;

        }
        j++;
    }

    lv_chart_refresh(chart);
}


void spectrum_init(void) {
    fft_cfg = kiss_fftr_alloc(NSAMP, false, fft_mem, &fft_mem_len);

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
    spectrum_running = 1;
    lv_scr_load_anim(Spectrum, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_resume(spectrum_timer);
}

void spectrum_stop(void) {
    spectrum_running = 0;
    lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_pause(spectrum_timer);
}

