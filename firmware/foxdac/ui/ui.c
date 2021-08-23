/*
 * ui.c
 *
 *  Created on: 15 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"

#include "../drivers/wm8805/wm8805.h"
#include "../drivers/tpa6130/tpa6130.h"
#include "../drivers/ssd1306/ssd1306.h"
#include "../drivers/encoder/encoder.h"

#include "lvgl/lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "ui.h"
#include "spectrum.h"
#include "dac_lvgl_ui.h"

#define BTN_MENU 26
#define BTN_OK   27

static void lv_init_ui(void) {
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
}

static repeating_timer_t lv_timer, wm_timer;

static void buttons_init(void) {
    gpio_init(BTN_MENU);
    gpio_set_dir(BTN_MENU, GPIO_IN);
    gpio_pull_up(BTN_MENU);

    gpio_init(BTN_OK);
    gpio_set_dir(BTN_OK, GPIO_IN);
    gpio_pull_up(BTN_OK);
}

static uint8_t btn_ok_lpf = 0, btn_menu_lpf = 0, btn_ok_press = 0, btn_menu_press = 0;

#define INPUT_COUNT 4
#define SCREEN_COUNT 3

static uint8_t cur_input = 0, cur_screen = 0;
static uint8_t input_to_wm[INPUT_COUNT] = { 3, 0, 1, 2 };

static uint8_t do_wm_tick = 0, do_lvgl_tick = 0;

alarm_pool_t* core1_alarm_pool;

static void buttons_read(void) {
    //btn_menu_lpf = (btn_menu_lpf * 9 + (!gpio_get(BTN_MENU)) * 10) / 10;
    //btn_ok_lpf = (btn_ok_lpf * 9 + (!gpio_get(BTN_OK)) * 10) / 10;

    if (!gpio_get(BTN_MENU)) {

        if(!btn_menu_press) {
            // menu pressed

            switch(cur_screen) {
            case 0:

                // main to spectrum

                spectrum_start();

                cur_screen = 1;
                break;
            case 1:

                // spectrum to apple

                spectrum_stop();
                badapple_start();

                cur_screen = 2;
                break;
            case 2:

                // apple to main

                badapple_stop();

                //lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 100, false);

                cur_screen = 0;
                break;
            }


        }

        btn_menu_press = 1;
    } else {
        btn_menu_press = 0;
    }

    if (!gpio_get(BTN_OK)) { //(btn_ok_lpf > 5) {
        //printf("%u\n", btn_ok_lpf);

        if(!btn_ok_press) {
            // ok pressed - toggle inputs

            cur_input = (cur_input + 1) % INPUT_COUNT;
            ui_select_input(cur_input);

            wm8805_set_input(input_to_wm[cur_input]);

            //printf("press\n");

        }

        btn_ok_press = 1;
    } else {
        btn_ok_press = 0;
    }
}

static bool lvgl_timer_cb(repeating_timer_t *rt) {
    do_lvgl_tick = 1;

    return true;
}

static bool wm_timer_cb(repeating_timer_t *rt) {
    do_wm_tick = 1;
    return true;
}

void ui_init(void) {
    lv_init_ui();

    buttons_init();
    encoder_init();

    DAC_BuildPages();

    ui_select_input(0);
    ui_set_vol_text(tpa6130_get_volume_str(10));

    core1_alarm_pool = alarm_pool_create(1, 5);
    alarm_pool_add_repeating_timer_ms(core1_alarm_pool, 5, lvgl_timer_cb, NULL, &lv_timer);
    alarm_pool_add_repeating_timer_ms(core1_alarm_pool, 100, wm_timer_cb, NULL, &wm_timer);

    spectrum_init();
}

void ui_loop(void) {
    if(do_wm_tick) {
        do_wm_tick = 0;
        wm8805_poll_intstat();
    }

    if(do_lvgl_tick) {
        do_lvgl_tick = 0;
        buttons_read();
        spectrum_loop();
        lv_task_handler();
    }

     badapple_next_frame();
}
