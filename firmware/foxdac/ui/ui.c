/*
 * ui.c
 *
 *  Created on: 15 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "../drivers/ssd1306/ssd1306.h"
#include "../drivers/ssd1306/ssd1306_tests.h"

#include "../drivers/encoder/encoder.h"

#include "lvgl/lvgl.h"
#include "lv_port_disp.h"

#include "dac_lvgl_ui.h"

static void lv_init_ui(void) {
	lv_init();
    lv_port_disp_init();
    //lv_theme_set_current(lv_theme_get_mono());
}

void lv_btn_test(void) {
    lv_obj_t * btn = lv_btn_create(lv_scr_act());     /*Add a button the current screen*/
    lv_obj_set_pos(btn, 10, 10);                            /*Set its position*/
    lv_obj_set_size(btn, 120, 50);                          /*Set its size*/
    //lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);           /*Assign a callback to the button*/

    lv_obj_t * label = lv_label_create(btn);          /*Add a label to the button*/
    lv_label_set_text(label, "Button");                     /*Set the labels text*/
    lv_obj_center(label);
}

static void anim_x_cb(void * var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void anim_size_cb(void * var, int32_t v)
{
    lv_obj_set_size(var, v, v);
}

/**
 * Create a playback animation
 */
void lv_example_anim_2(void)
{

    lv_obj_t * obj = lv_obj_create(lv_scr_act());
    lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);

    lv_obj_align(obj, LV_ALIGN_LEFT_MID, 10, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 10, 50);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_playback_delay(&a, 100);
    lv_anim_set_playback_time(&a, 300);
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

    lv_anim_set_exec_cb(&a, anim_size_cb);
    lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, 5, 70);
    lv_anim_start(&a);
}


void ui_init(void) {
    i2c_init(SSD1306_I2C_PORT, 400 * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C); // GP14
    gpio_set_function(15, GPIO_FUNC_I2C); // GP15
    gpio_pull_up(14);
    gpio_pull_up(15);

    ssd1306_Init();

    lv_init_ui();

    encoder_init();

    //lv_btn_create(lv_scr_act());
    //lv_example_anim_2();

    DAC_BuildPages();

    badapple_init();
    //badapple_start();
}

void ui_loop(void) {
	//ssd1306_TestAll();
	lv_task_handler();
	//printf("enc: %d\n", encoder_get_count());
	sleep_ms(3);
}
