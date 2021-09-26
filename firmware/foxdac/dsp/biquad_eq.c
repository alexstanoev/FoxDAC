/*
 * biquad_eq.c
 *
 *  Created on: 25 Sep 2021
 *      Author: alex
 */

#include "arm_math.h"

// Block size for the underlying processing
#define BLOCKSIZE 16

// Q31 state buffers

//static q63_t biquadStateBand1Q31[4 * 2 * 2];
//static q63_t biquadStateBand2Q31[4 * 2 * 2];
static q31_t biquadStateBand3Q31[4 * 2 * 2];
//static q31_t biquadStateBand4Q31[4 * 2 * 2];
//static q31_t biquadStateBand5Q31[4 * 2 * 2];

//static arm_biquad_cas_df1_32x64_ins_q31 S1[2];
//static arm_biquad_cas_df1_32x64_ins_q31 S2[2];
static arm_biquad_casd_df1_inst_q31 S3[2];
//static arm_biquad_casd_df1_inst_q31 S4[2];
//static arm_biquad_casd_df1_inst_q31 S5[2];

// Sample and coefficient buffers
static q31_t samplesQ31[BLOCKSIZE];

// 5 coefficients per filter stage, in the following order:
// b10 b11 b12 a11 a12 .. b20 b21
static q31_t coeffCalc[5];

// based on http://www.earlevel.com/scripts/widgets/20131013/biquads2.js
// equations from http://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
static void calc_biquad_peaking_coeff(double Q, double peakGain, double Fc, double Fs) {
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
    coeffCalc[0] = clip_q63_to_q31((q63_t) (a0 * 268435456.0f)); // b10
    coeffCalc[1] = clip_q63_to_q31((q63_t) (a1 * 268435456.0f)); // b11
    coeffCalc[2] = clip_q63_to_q31((q63_t) (a2 * 268435456.0f)); // b12
    coeffCalc[3] = clip_q63_to_q31((q63_t) (b1 * 268435456.0f)); // a11
    coeffCalc[4] = clip_q63_to_q31((q63_t) (b2 * 268435456.0f)); // a12
}

void biquad_eq_init(void) {
    // Initialize the state and coefficient buffers for all Biquad sections

    calc_biquad_peaking_coeff(0.707, 10, 70, 48000);

    arm_biquad_cascade_df1_init_q31(&S3[0], 1, coeffCalc, &biquadStateBand3Q31[0 * 8], 3);
    arm_biquad_cascade_df1_init_q31(&S3[1], 1, coeffCalc, &biquadStateBand3Q31[1 * 8], 3);

//    for(int chan = 0; chan <= 1; chan++) {
//        arm_biquad_cas_df1_32x64_init_q31(&S1[chan], NUMSTAGES,
//                (q31_t *) &coeffTable[190*0 + 10*(gainDB[0] + 9)],
//                &biquadStateBand1Q31[chan * 8], 2);
//
//        arm_biquad_cas_df1_32x64_init_q31(&S2[chan], NUMSTAGES,
//                (q31_t *) &coeffTable[190*1 + 10*(gainDB[1] + 9)],
//                &biquadStateBand2Q31[chan * 8], 2);
//
//        arm_biquad_cascade_df1_init_q31(&S3[chan], NUMSTAGES,
//                (q31_t *) &coeffTable[190*2 + 10*(gainDB[2] + 9)],
//                &biquadStateBand3Q31[chan * 8], 2);
//
//        arm_biquad_cascade_df1_init_q31(&S4[chan], NUMSTAGES,
//                (q31_t *) &coeffTable[190*3 + 10*(gainDB[3] + 9)],
//                &biquadStateBand4Q31[chan * 8], 2);
//
//        arm_biquad_cascade_df1_init_q31(&S5[chan], NUMSTAGES,
//                (q31_t *) &coeffTable[190*4 + 10*(gainDB[4] + 9)],
//                &biquadStateBand5Q31[chan * 8], 2);
//    }

}

void biquad_eq_process_inplace(int16_t* samples, int16_t len) {
    int numblocks = len / BLOCKSIZE;

    for(int i = 0; i < numblocks; i++) {
        for(int chan = 0; chan <= 1; chan++) {
            // q16 -> q31
            int k = 0;
            for(int j = 0; j < BLOCKSIZE * 2; j += 2) {
                samplesQ31[k++] = ((q31_t) samples[(i * BLOCKSIZE * 2) + j + chan]) << 16;
            }

            // Scale down by 1/8 so we don't clip when adding gain
            arm_scale_q31(samplesQ31, 0x7FFFFFFF, -3, samplesQ31, BLOCKSIZE);

            //arm_biquad_cas_df1_32x64_q31(&S1[chan], samplesQ31, samplesQ31, BLOCKSIZE);
            //arm_biquad_cas_df1_32x64_q31(&S2[chan], samplesQ31, samplesQ31, BLOCKSIZE);

            arm_biquad_cascade_df1_q31(&S3[chan], samplesQ31, samplesQ31, BLOCKSIZE);
            //arm_biquad_cascade_df1_q31(&S4[chan], samplesQ31, samplesQ31, BLOCKSIZE);
            //arm_biquad_cascade_df1_q31(&S5[chan], samplesQ31, samplesQ31, BLOCKSIZE);

            // q31 -> q16
            k = 0;
            for(int j = 0; j < BLOCKSIZE * 2; j += 2) {
                samples[(i * BLOCKSIZE * 2) + j + chan] = (int16_t) (samplesQ31[k++] >> 16);
            }
        }
    }
}
