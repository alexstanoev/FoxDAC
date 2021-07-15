/*
 * ui.c
 *
 *  Created on: 15 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "drivers/ssd1306/ssd1306.h"
#include "drivers/ssd1306/ssd1306_tests.h"

void ui_init(void) {
	//i2c_deinit(SSD1306_I2C_PORT);
    i2c_init(SSD1306_I2C_PORT, 400 * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C); // GP14
    gpio_set_function(15, GPIO_FUNC_I2C); // GP15
    gpio_pull_up(14);
    gpio_pull_up(15);

    ssd1306_Init();

}

void ui_loop(void) {
	ssd1306_TestAll();
}
