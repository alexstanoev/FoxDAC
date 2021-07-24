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

static uint8_t spectrum_running = 0;

lv_obj_t * Spectrum;

lv_obj_t * chart;
lv_coord_t value_array[41];


lv_coord_t target_value_array[41];
lv_coord_t old_value_array[41];

lv_timer_t * spectrum_timer;

static int runs = 0;

static int j = 0;
static int N = 3;

// called from usb_spdif.c, runs in IRQ
void spectrum_consume_samples(int16_t* samples, uint32_t sample_count) {

}

static void redraw_bars(lv_timer_t * timer) {
    runs++;

    if(runs == 3) {
        runs = 0;
        j = 0;
        for(int i = 0; i < 41; i++) {
            old_value_array[i] = target_value_array[i];
            target_value_array[i] = lv_rand(0, 64);
        }
    }

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

