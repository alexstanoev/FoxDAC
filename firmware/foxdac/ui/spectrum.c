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

#include <arm_math.h>

// don't forget to enable ARM_TABLE_TWIDDLECOEF_Q15_x in CMSISDSP/CommonTables/CMakeLists.txt
#define FFT_SIZE 1024
#define FFT_SIZE_F (1024.0f)

#define NUM_BARS 41
#define NUM_BARS_F 41.0f

#define MIN_BAR_FREQ 120.0f
#define MAX_BAR_FREQ 20000.0f

#define FFT_BIN_SIZE (sample_rate / FFT_SIZE)

#define BAR_MIN_DB 28
#define BAR_MAX_DB 68

static uint8_t spectrum_running = 0;
static uint32_t sample_rate = 48000;

static int interp_step = 0;
static const int interp_times = 1; // off temporarily

static q15_t sample_buf[FFT_SIZE];
static volatile int sample_buf_pos = 0;

static lv_obj_t * chart;
static lv_coord_t value_array[NUM_BARS];

static lv_coord_t target_value_array[NUM_BARS];
static lv_coord_t old_value_array[NUM_BARS];

static lv_timer_t * spectrum_timer;

static arm_rfft_instance_q15 fft_instance;
static q15_t fft_output[FFT_SIZE * 2];

static q15_t window[FFT_SIZE];
static int startbins[NUM_BARS];
static int endbins[NUM_BARS];

lv_obj_t * Spectrum;

static float hanning(short in, size_t i, size_t s) {
    return in * 0.5f * (1.0f - cosf(2.0f * ((float)M_PI) * (float)(i) / (float)(s - 1.0f)));
}

static void init_tables(void) {
    // precompute window
    for(int i = 0; i < FFT_SIZE; i++) {
        float wnd = hanning(1, i, FFT_SIZE);
        window[i] = clip_q31_to_q15((q31_t) (wnd * 32768.0f));
    }

    // precompute log bins
    for (int i = 0; i < NUM_BARS; i++) {
        // log scale, bins spaced at:
        // min * (max/min) ^ x
        float bar_freq = MIN_BAR_FREQ * powf((MAX_BAR_FREQ / MIN_BAR_FREQ), i / NUM_BARS_F);
        float bar_freq_next = ((i + 1) == NUM_BARS) ? MAX_BAR_FREQ
                : (MIN_BAR_FREQ * powf((MAX_BAR_FREQ / MIN_BAR_FREQ), (i + 1) / NUM_BARS_F));

        startbins[i] = floorf(bar_freq / FFT_BIN_SIZE);
        endbins[i] = ceilf(bar_freq_next / FFT_BIN_SIZE);
    }
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

        // average channels and apply window
        //q31_t channel_avg = (((q31_t) samples[i]) + ((q31_t) samples[i + 1])) >> 1;

        // take only left channel
        q31_t channel_avg = ((q31_t) samples[i]);

        sample_buf[sample_buf_pos++] = clip_q31_to_q15((((q31_t) (window[sample_buf_pos]) * (channel_avg)) >> 15));

        if(sample_buf_pos == FFT_SIZE) {
            return;
        }
    }
}

void spectrum_loop(void) {
    if(!spectrum_running || sample_buf_pos != FFT_SIZE || interp_step < interp_times) return;

    arm_rfft_q15(&fft_instance, sample_buf, fft_output);
    //arm_cmplx_mag_q15(fft_output, fft_output, FFT_SIZE);
    arm_cmplx_mag_squared_q15(fft_output, fft_output, FFT_SIZE);

    for (int i = 0; i < NUM_BARS; i++) {
        // log scale, bins spaced at:
        // min * (max/min) ^ x
        int startbin = startbins[i];
        int endbin = endbins[i];

        // rebin and get max amplitude for bucket
        q31_t power_max = 0;
        for(int j = startbin; j < endbin; j++) {
            power_max = MAX(power_max, fft_output[j]);
        }

        // TODO: scaling needs fixing, arbitrary currently
        // small signals are getting lost somewhere
        power_max = power_max << 5;

        float power = 20.0f * log10f((float) power_max);

        // clamp min
        power -= BAR_MIN_DB;
        power = MAX(0.0f, power);

        // clamp max
        power /= (BAR_MAX_DB - BAR_MIN_DB);
        power = MIN(1.0f, power);

        old_value_array[i] = target_value_array[i];
        target_value_array[i] = ceilf(64.0f * power);
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
    arm_rfft_init_q15(&fft_instance, FFT_SIZE, 0, 1);
    // arm_status status
    //printf("fft status %d\n", status);

    init_tables();

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

    spectrum_timer = lv_timer_create(redraw_bars, 5, NULL);
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
    //lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_pause(spectrum_timer);
}

