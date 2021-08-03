/*
 * wm8805.h
 *
 *  Created on: 30 Jul 2021
 *      Author: alex
 */

#ifndef FOXDAC_DRIVERS_WM8805_WM8805_H_
#define FOXDAC_DRIVERS_WM8805_WM8805_H_


void wm8805_init(void);
void wm8805_set_input(uint8_t input);
void wm8805_poll_intstat(void);

#endif /* FOXDAC_DRIVERS_WM8805_WM8805_H_ */
