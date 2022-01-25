/*
 * ui.c
 *
 *  Created on: 15 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/structs/usb.h"
#include "hardware/i2c.h"

#include "../drivers/wm8805/wm8805.h"
#include "../drivers/tpa6130/tpa6130.h"
#include "../drivers/ssd1306/ssd1306.h"
#include "../drivers/encoder/encoder.h"

#include "../dsp/biquad_eq.h"

#include "lvgl/lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "ui.h"
#include "spectrum.h"
#include "dac_lvgl_ui.h"
#include "persistent_storage.h"

#define BTN_MENU 26
#define BTN_OK   27

#define INPUT_COUNT 4

#define OLED_SUSPEND_TIMEOUT_MS (60 * 1000)

static void lv_init_ui(void) {
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
}

static repeating_timer_t lv_timer, wm_timer, persist_timer;

static uint8_t btn_ok_press = 0, btn_menu_press = 0;

static uint8_t cur_input = 0, cur_screen = 0;
static uint8_t input_to_wm[INPUT_COUNT] = { 3, 0, 1, 2 };

static uint8_t do_wm_tick = 0, do_lvgl_tick = 0, do_persist_tick = 0;

static uint32_t prev_slider_value;
static uint32_t last_activity_time = 0;

volatile uint8_t ui_suspended = 0;
alarm_pool_t* core1_alarm_pool;

void ui_update_activity(void) {
    last_activity_time = to_ms_since_boot(get_absolute_time());
}

static void buttons_init(void) {
    gpio_init(BTN_MENU);
    gpio_set_dir(BTN_MENU, GPIO_IN);
    gpio_pull_up(BTN_MENU);

    gpio_init(BTN_OK);
    gpio_set_dir(BTN_OK, GPIO_IN);
    gpio_pull_up(BTN_OK);
}

static void buttons_read(void) {

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

                // spectrum to eq

                spectrum_stop();
                eq_curve_start();

                cur_screen = 2;
                break;
            case 2:

                // eq to apple

                eq_curve_stop();
                badapple_start();

                cur_screen = 3;
                break;
            case 3:

                // apple to breakout

                badapple_stop();
                breakout_start();

                cur_screen = 4;
                break;
            case 4:

                // breakout to main

                breakout_stop();

                //lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 100, false);

                cur_screen = 0;
                break;
            }

            ui_update_activity();
        }

        btn_menu_press = 1;
    } else {
        btn_menu_press = 0;
    }

    if (!gpio_get(BTN_OK)) {

        if(!btn_ok_press) {
            // ok pressed - toggle inputs

            if(lv_disp_get_scr_act(NULL) == EqCurve) {
                // use the ok button to switch through EQ bands
                eq_curve_next_band();
            } else {
                cur_input = (cur_input + 1) % INPUT_COUNT;
                ui_select_input(cur_input);

                wm8805_set_input(input_to_wm[cur_input]);

                persist_write_byte(&input_file, cur_input);
            }

            ui_update_activity();
        }

        btn_ok_press = 1;
    } else {
        btn_ok_press = 0;
    }

    if(encoder_get_pressed()) {
        // encoder pressed - toggle mute

        if(lv_disp_get_scr_act(NULL) == EqCurve) {
            // use the encoder button to toggle the EQ on/off
            biquad_eq_set_enabled(!biquad_eq_get_enabled());
        } else {
            uint32_t new_slider_value;

            if(!tpa6130_get_muted()) {
                prev_slider_value = (uint32_t)tpa6130_get_volume();
                new_slider_value = 0;
            } else {
                new_slider_value = prev_slider_value;
            }

            lv_slider_set_value(VolumeSlider, new_slider_value, LV_ANIM_ON);
            lv_event_send(VolumeSlider, LV_EVENT_VALUE_CHANGED, NULL);
        }

        ui_update_activity();
    }
}

static void load_persistence(void) {
    // last selected input
    cur_input = persist_read_byte(&input_file, 0);
    ui_select_input(cur_input);
    wm8805_set_input(input_to_wm[cur_input]);

    // last volume
    int8_t vol = (int8_t) persist_read_byte(&vol_file, (uint8_t) tpa6130_get_volume());
    lv_slider_set_value(VolumeSlider, vol, LV_ANIM_ON);
    lv_event_send(VolumeSlider, LV_EVENT_VALUE_CHANGED, NULL);
}

static void check_suspend(void) {
    extern volatile uint8_t usb_host_seen;
    static uint8_t prev_suspended = 1;
    uint8_t usb_suspended = ((usb_hw->sie_status & USB_SIE_STATUS_SUSPENDED_BITS) != 0);

    if(usb_suspended && !prev_suspended) {
        // usb just suspended, force the screen to suspend
        last_activity_time = 0;
    } else if(!usb_suspended && prev_suspended) {
        // waking up from suspend
        ui_update_activity();
    }

    uint32_t idle_time = to_ms_since_boot(get_absolute_time()) - last_activity_time;

    if(!ui_suspended && (usb_suspended || !usb_host_seen) && idle_time > OLED_SUSPEND_TIMEOUT_MS) {
        // UI has been idle for OLED_SUSPEND_TIMEOUT_MS and there's no USB host -> suspend the oled
        ssd1306_SetDisplayOn(0);
        ui_suspended = 1;

        // turn off underrun led
        gpio_put(18, 0);

        //if(cur_input == 0) {
        //    ui_set_sr_text("NO HOST");
        //}
    } else if(ui_suspended && idle_time <= OLED_SUSPEND_TIMEOUT_MS) {
        // if the UI is suspended but there has been activity, wake up
        ssd1306_SetDisplayOn(1);
        ui_suspended = 0;
    }

    prev_suspended = usb_suspended;
}

static bool lvgl_timer_cb(repeating_timer_t *rt) {
    do_lvgl_tick = 1;
    return true;
}

static bool wm_timer_cb(repeating_timer_t *rt) {
    do_wm_tick = 1;
    return true;
}

static bool persist_timer_cb(repeating_timer_t *rt) {
    do_persist_tick = 1;
    return true;
}

void ui_init(void) {
    lv_init_ui();

    buttons_init();
    encoder_init();

    DAC_BuildPages();

    core1_alarm_pool = alarm_pool_create(1, 8);
    alarm_pool_add_repeating_timer_ms(core1_alarm_pool, 5, lvgl_timer_cb, NULL, &lv_timer);
    alarm_pool_add_repeating_timer_ms(core1_alarm_pool, 100, wm_timer_cb, NULL, &wm_timer);
    alarm_pool_add_repeating_timer_ms(core1_alarm_pool, 1000*60, persist_timer_cb, NULL, &persist_timer);

    persist_init();
    eq_curve_init();
    spectrum_init();
    breakout_init();

    load_persistence();
    //ui_select_input(0);
    //ui_set_vol_text(tpa6130_get_volume_str(10));

    ui_update_activity();
}

void ui_loop(void) {
    if(do_wm_tick) {
        do_wm_tick = 0;
        wm8805_poll_intstat();
    }

    spectrum_loop();
    badapple_next_frame();

    if(do_lvgl_tick) {
        do_lvgl_tick = 0;
        buttons_read();
        lv_task_handler();
    }

    if(do_persist_tick) {
        do_persist_tick = 0;
        persist_flush_all();
    }

    check_suspend();
}
