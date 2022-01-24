/*
 * eq_curve.c
 *
 *  Created on: 21 Nov 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "stdint.h"

#include "lvgl/lvgl.h"

#include "../drivers/encoder/encoder.h"
#include "../dsp/biquad_eq.h"

#include "dac_lvgl_ui.h"
#include "persistent_storage.h"

#define NUM_BANDS 8
#define MAX_RANGE 40

lv_obj_t * EqCurve;

static lv_obj_t * chart;
static lv_chart_series_t * ser;
static lv_chart_cursor_t * cursor;
static lv_coord_t value_array[NUM_BANDS];
static lv_timer_t * eq_timer;

static uint8_t value_array_default[NUM_BANDS];
static uint8_t value_array_persist[NUM_BANDS];

static uint8_t current_band = 0;

// set to 1 to stop lvgl from polling the encoder for volume
extern uint8_t lv_indev_pause_encoder;

//const char * freq[] = { "64Hz", "125Hz", "250Hz", "500Hz", "1kHz", "2kHz", "4kHz", "8kHz" };

static float mapRange(float a1, float a2, float b1, float b2, float s) {
    return b1 + (s - a1) * (b2 - b1) / (a2 - a1);
}

static void eq_store(void) {
    for(int i = 0; i < NUM_BANDS; i++) {
        value_array_persist[i] = value_array[i];
    }

    persist_write(&eq_curve_file, value_array_persist, sizeof(value_array_persist));
}

static void eq_load(void) {
    persist_read(&eq_curve_file, value_array_persist, value_array_default, sizeof(value_array_persist));

    for(int i = 0; i < NUM_BANDS; i++) {
        value_array[i] = value_array_persist[i];

        biquad_eq_set_stage_gain(i, mapRange(0.0f, MAX_RANGE, -20.0f, 20.0f, (float) value_array[i]));
    }

    biquad_eq_update_coeffs();

    lv_chart_refresh(chart);
}

static void eq_update(lv_timer_t * timer) {
    int32_t enc_delta = encoder_get_delta();
    if(enc_delta != 0) {
        int32_t curr = value_array[current_band];
        curr += enc_delta;

        if(curr < 0) {
            curr = 0;
        } else if(curr > MAX_RANGE) {
            curr = MAX_RANGE;
        }

        value_array[current_band] = curr;

        biquad_eq_set_stage_gain(current_band, mapRange(0.0f, MAX_RANGE, -20.0f, 20.0f, (float) curr));
        biquad_eq_update_coeffs();

        lv_chart_refresh(chart);

        eq_store();
    }
}

void eq_curve_init(void) {
    EqCurve = lv_obj_create(NULL);

    chart = lv_chart_create(EqCurve);
    lv_obj_set_size(chart, 127, 64);
    lv_obj_center(chart);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, MAX_RANGE);

    lv_chart_set_div_line_count(chart, 3, 0);

    //lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    //lv_obj_set_style_pad_column(chart, 1, LV_PART_MAIN);

    lv_color_t white = { .full = 1 };
    ser = lv_chart_add_series(chart, white, LV_CHART_AXIS_PRIMARY_Y);

    cursor = lv_chart_add_cursor(chart, white, LV_DIR_BOTTOM);

    lv_chart_set_ext_y_array(chart, ser, value_array);
    lv_chart_set_point_count(chart, NUM_BANDS);

    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 5, 2, 6, 3, true, 4);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 3, 2, true, 5);

    lv_coord_t * ser_array =  lv_chart_get_y_array(chart, ser);

    for(int i = 0; i < NUM_BANDS; i++) {
        ser_array[i] = MAX_RANGE / 2;
        value_array_default[i] = MAX_RANGE / 2;
    }

    lv_chart_refresh(chart);

    lv_chart_set_cursor_point(chart, cursor, ser, current_band);

    eq_timer = lv_timer_create(eq_update, 100, NULL);
    lv_timer_pause(eq_timer);

    eq_load();
}

void eq_curve_next_band() {
    current_band = (current_band + 1) % NUM_BANDS;

    lv_chart_set_cursor_point(chart, cursor, ser, current_band);
}

void eq_curve_start(void) {
    lv_indev_pause_encoder = 1;
    encoder_get_delta();
    lv_scr_load_anim(EqCurve, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_resume(eq_timer);
}

void eq_curve_stop(void) {
    lv_indev_pause_encoder = 0;
    lv_timer_pause(eq_timer);
    lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

