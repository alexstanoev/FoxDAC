/*
 * biquad_eq.h
 *
 *  Created on: 25 Sep 2021
 *      Author: alex
 */

#ifndef FOXDAC_DSP_BIQUAD_EQ_H_
#define FOXDAC_DSP_BIQUAD_EQ_H_


void biquad_eq_init(void);
void biquad_eq_init_core1(void);
void biquad_eq_update_coeffs(void);
void biquad_eq_set_fs(int fs);
void biquad_eq_process_inplace(int16_t* samples, int16_t len);
void biquad_eq_set_stage_gain(uint8_t stage, float gain);

#endif /* FOXDAC_DSP_BIQUAD_EQ_H_ */
