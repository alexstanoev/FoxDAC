/*
 * spectrum.h
 *
 *  Created on: 24 Jul 2021
 *      Author: alex
 */

#ifndef FOXDAC_UI_SPECTRUM_H_
#define FOXDAC_UI_SPECTRUM_H_

#include "lvgl/lvgl.h"
#include "stdint.h"

void spectrum_loop(void);
void spectrum_consume_samples(int16_t* samples, uint32_t sample_count);
void spectrum_init(void);
void spectrum_start(void);
void spectrum_stop(void);

extern lv_obj_t * Spectrum;

#endif /* FOXDAC_UI_SPECTRUM_H_ */
