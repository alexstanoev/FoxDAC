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
#include "lv_port_indev.h"

#include "spectrum.h"

#include "dac_lvgl_ui.h"

static void lv_init_ui(void) {
	lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
}

void ui_init(void) {
    i2c_init(SSD1306_I2C_PORT, 400 * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C); // GP14
    gpio_set_function(15, GPIO_FUNC_I2C); // GP15
    gpio_pull_up(14);
    gpio_pull_up(15);

    ssd1306_Init();

    sleep_ms(50);

    lv_init_ui();

    encoder_init();

    //DAC_BuildPages();

    spectrum_init();

    spectrum_start();

    //badapple_init();

    //badapple_start();
}

void ui_loop(void) {
	//ssd1306_TestAll();
	lv_task_handler();
	//printf("enc: %d\n", encoder_get_count());
	sleep_ms(3);
}
