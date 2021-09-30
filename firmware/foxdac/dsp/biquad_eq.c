/*
 * biquad_eq.c
 *
 *  Created on: 25 Sep 2021
 *      Author: alex
 */

#include "arm_math.h"

// Block size for the underlying processing
#define BLOCKSIZE 64

#define NUM_EQ_STAGES 10

#define FILTER_Q 0.707

#define Q28_SCALE_FACTOR 268435456.0f
#define COEFF_POSTSHIFT 3

static int freq_bands[NUM_EQ_STAGES] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static float freq_band_gains[NUM_EQ_STAGES] = { 0.0f };

// 5 coefficients per filter stage, in the following order:
// b10 b11 b12 a11 a12 .. b20 b21
static q31_t freq_band_coeffs[5 * NUM_EQ_STAGES];

// 2 channels * 10 bands
static arm_biquad_casd_df1_inst_q31 biquad_cascade[2 * NUM_EQ_STAGES];

// 4 vars * 2 channels * 10 bands
static q31_t biquad_state[4 * 2 * NUM_EQ_STAGES];

// Q31 sample scratch buffer
static q31_t samplesQ31[BLOCKSIZE];

static int curr_fs = 48000;

// based on http://www.earlevel.com/scripts/widgets/20131013/biquads2.js
// equations from http://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
static void calc_biquad_peaking_coeff(double Q, double peakGain, double Fc, double Fs, q31_t* coeffs) {
    double norm, a0, a1, a2, b1, b2;

    double V = pow(10, fabs(peakGain) / 20.0);
    double K = tan(M_PI * (Fc / Fs));

    if(peakGain >= 0) {
        norm = 1 / (1 + 1/Q * K + K * K);
        a0 = (1 + V/Q * K + K * K) * norm;
        a1 = 2 * (K * K - 1) * norm;
        a2 = (1 - V/Q * K + K * K) * norm;
        b1 = a1;
        b2 = (1 - 1/Q * K + K * K) * norm;
    } else {
        norm = 1 / (1 + V/Q * K + K * K);
        a0 = (1 + 1/Q * K + K * K) * norm;
        a1 = 2 * (K * K - 1) * norm;
        a2 = (1 - 1/Q * K + K * K) * norm;
        b1 = a1;
        b2 = (1 - V/Q * K + K * K) * norm;
    }

    // negate feedback terms for CMSIS
    b1 *= -1.0;
    b2 *= -1.0;

    // store as Q28 + postshift 3 for Q31
    coeffs[0] = clip_q63_to_q31((q63_t) (a0 * Q28_SCALE_FACTOR)); // b10
    coeffs[1] = clip_q63_to_q31((q63_t) (a1 * Q28_SCALE_FACTOR)); // b11
    coeffs[2] = clip_q63_to_q31((q63_t) (a2 * Q28_SCALE_FACTOR)); // b12
    coeffs[3] = clip_q63_to_q31((q63_t) (b1 * Q28_SCALE_FACTOR)); // a11
    coeffs[4] = clip_q63_to_q31((q63_t) (b2 * Q28_SCALE_FACTOR)); // a12
}

void biquad_eq_update_coeffs(void) {
    for(int i = 0; i < NUM_EQ_STAGES; i++) {
        calc_biquad_peaking_coeff(FILTER_Q, freq_band_gains[i], freq_bands[i], curr_fs, &freq_band_coeffs[i * 5]);
    }

    // (re)init cascades
    for(int i = 0; i < NUM_EQ_STAGES * 2; i += 2) {
        for(int chan = 0; chan <= 1; chan++) {
            arm_biquad_cascade_df1_init_q31(&biquad_cascade[i + chan], 1, &freq_band_coeffs[(i / 2) * 5], &biquad_state[4 * (i + chan)], COEFF_POSTSHIFT);
        }
    }
}

void biquad_eq_set_fs(int fs) {
    curr_fs = fs;
    biquad_eq_update_coeffs();
}

void biquad_eq_init(void) {
    // TODO read stored gains
    freq_band_gains[0] = 0;
    freq_band_gains[1] = 2;
    freq_band_gains[2] = 2;
    freq_band_gains[3] = 0;
    freq_band_gains[4] = 0;
    freq_band_gains[5] = 0;
    freq_band_gains[6] = 0;
    freq_band_gains[7] = 0;
    freq_band_gains[8] = 0;
    freq_band_gains[9] = 0;

    // calculate default coefficients and init cascades
    biquad_eq_update_coeffs();
}

void biquad_eq_process_inplace(int16_t* samples, int16_t len) {
    int numblocks = len / BLOCKSIZE;
    int leftover = len % BLOCKSIZE;

    for(int i = 0; i < numblocks; i++) {
        for(int chan = 0; chan <= 1; chan++) {
            // q16 -> q31
            int k = 0;
            for(int j = 0; j < BLOCKSIZE * 2; j += 2) {
                samplesQ31[k++] = ((q31_t) samples[(i * BLOCKSIZE * 2) + j + chan]) << 16;
            }

            // Scale down by 1/8 so we don't clip when adding gain
            arm_scale_q31(samplesQ31, 0x7FFFFFFF, -3, samplesQ31, BLOCKSIZE);

            // Run through all cascades
            // TODO 4 should be NUM_EQ_STAGES but that takes too long for the IRQ
            // TODO compare with arm_biquad_cas_df1_32x64_q31
            for(int stage = 0; stage < 6 * 2; stage += 2) {
                arm_biquad_cascade_df1_q31(&biquad_cascade[stage + chan], samplesQ31, samplesQ31, BLOCKSIZE);
            }

            // q31 -> q16
            k = 0;
            for(int j = 0; j < BLOCKSIZE * 2; j += 2) {
                samples[(i * BLOCKSIZE * 2) + j + chan] = (int16_t) (samplesQ31[k++] >> 16);
            }
        }
    }

    // repeat for any non-even multiples of BLOCKSIZE (44.1)
    if(leftover) {
        for(int chan = 0; chan <= 1; chan++) {
            // q16 -> q31
            int k = 0;
            for(int j = 0; j < leftover * 2; j += 2) {
                samplesQ31[k++] = ((q31_t) samples[(numblocks * BLOCKSIZE * 2) + j + chan]) << 16;
            }

            // Scale down by 1/8 so we don't clip when adding gain
            arm_scale_q31(samplesQ31, 0x7FFFFFFF, -3, samplesQ31, leftover);

            // Run through all cascades
            // TODO 4 should be NUM_EQ_STAGES but that takes too long for the IRQ
            // TODO compare with arm_biquad_cas_df1_32x64_q31
            for(int stage = 0; stage < 6 * 2; stage += 2) {
                arm_biquad_cascade_df1_q31(&biquad_cascade[stage + chan], samplesQ31, samplesQ31, leftover);
            }

            // q31 -> q16
            k = 0;
            for(int j = 0; j < leftover * 2; j += 2) {
                samples[(numblocks * BLOCKSIZE * 2) + j + chan] = (int16_t) (samplesQ31[k++] >> 16);
            }
        }
    }
}
