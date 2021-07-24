/*
 * ui.c
 *
 *  Created on: 15 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"

#include "../drivers/ssd1306/ssd1306.h"
#include "../drivers/ssd1306/ssd1306_tests.h"

#include "../drivers/encoder/encoder.h"

#include "lvgl/lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "spectrum.h"

#include "dac_lvgl_ui.h"

static void lv_init_ui(void) {
	lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
}

static bool lvgl_timer_cb(repeating_timer_t *rt) {
    lv_task_handler();
    return true;
}

static repeating_timer_t lv_timer;

void ui_init(void) {
    i2c_init(SSD1306_I2C_PORT, 400 * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C); // GP14
    gpio_set_function(15, GPIO_FUNC_I2C); // GP15
    gpio_pull_up(14);
    gpio_pull_up(15);

    ssd1306_Init();

    busy_wait_ms(50);

    lv_init_ui();

    encoder_init();

    //DAC_BuildPages();

    //alarm_pool_t* pool = alarm_pool_create(1, 1);
    //alarm_pool_add_repeating_timer_ms(pool, 5, lvgl_timer_cb, NULL, &lv_timer);

    spectrum_init();

    spectrum_start();

    //badapple_init();

    //badapple_start();
}

void __attribute__((noinline)) __scratch_x("ui_loop") ui_loop(void) {
	lv_task_handler();
	busy_wait_ms(5);
}
