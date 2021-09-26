/*
 * encoder.h
 *
 *  Created on: 16 Jul 2021
 *      Author: alex
 */

#ifndef FOXDAC_DRIVERS_ENCODER_ENCODER_H_
#define FOXDAC_DRIVERS_ENCODER_ENCODER_H_

void encoder_init(void);
uint8_t encoder_get_pressed(void);
int32_t encoder_get_delta(void);

#endif /* FOXDAC_DRIVERS_ENCODER_ENCODER_H_ */
