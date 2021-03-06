/*
 * biquad_eq.c
 *
 *  Created on: 25 Sep 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/irq.h"

#include "arm_math.h"

#define NUM_EQ_STAGES 8

#define FILTER_Q 0.707

#define Q28_SCALE_FACTOR 268435456.0f
#define COEFF_POSTSHIFT 3

#define TMP_BUFFER_LEN 384

static uint8_t eq_enabled = 0;

//static int freq_bands[NUM_EQ_STAGES] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static int freq_bands[NUM_EQ_STAGES] = { 64, 125, 250, 500, 1000, 2000, 4000, 8000 };
static float freq_band_gains[NUM_EQ_STAGES] = { 0.0f };

// 5 coefficients per filter stage, in the following order:
// b10 b11 b12 a11 a12 .. b20 b21
static volatile q31_t freq_band_coeffs[5 * NUM_EQ_STAGES];

// 4 vars * NUM_EQ_STAGES bands
static volatile q31_t biquad_state_l[4 * NUM_EQ_STAGES];
static volatile q31_t biquad_state_r[4 * NUM_EQ_STAGES];

// Temp buffer for scaled-down samples
static volatile q31_t samples32[TMP_BUFFER_LEN];

static int curr_fs = 48000;

static volatile int sample_cnt = 0;

typedef enum {
   LEFT_CHANNEL,
   RIGHT_CHANNEL,
   BOTH_CHANNELS
} channel_sel_t;

// based on http://www.earlevel.com/scripts/widgets/20131013/biquads2.js
// equations from http://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
static void calc_biquad_peaking_coeff(double Q, double peakGain, double Fc, double Fs, volatile q31_t* coeffs) {
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
    memset((q31_t*) biquad_state_l, 0, 4 * NUM_EQ_STAGES * sizeof(q31_t));
    memset((q31_t*) biquad_state_r, 0, 4 * NUM_EQ_STAGES * sizeof(q31_t));

    __dmb();
}

void biquad_eq_set_fs(int fs) {
    curr_fs = fs;
    biquad_eq_update_coeffs();
}

void biquad_eq_set_enabled(uint8_t enabled) {
    eq_enabled = enabled;
}

uint8_t biquad_eq_get_enabled(void) {
    return eq_enabled;
}

void biquad_eq_set_stage_gain(uint8_t stage, float gain) {
    if(stage >= NUM_EQ_STAGES) return;
    freq_band_gains[stage] = gain;
}

//static int mulhs(int u, int v) {
//    unsigned u0, v0, w0;
//    int u1, v1, w1, w2, t;
//    u0 = u & 0xFFFF; u1 = u >> 16;
//    v0 = v & 0xFFFF; v1 = v >> 16;
//    w0 = u0*v0;
//    t = u1*v0 + (w0 >> 16);
//    w1 = t & 0xFFFF;
//    w2 = t >> 16;
//    w1 = u0*v1 + w1;
//    return u1*v1 + w2 + (w1 >> 16);
//}

// Based on https://community.arm.com/developer/ip-products/processors/f/cortex-m-forum/49526/mulshift32-in-14-cycles
static inline int mulhs(int a, int b) {
    register int t0, t1;
    __asm__ volatile (
            " .syntax unified\n"

            "asrs %[t1], %[a], #16 //Factor0 hi [16:31] \n"
            "uxth %[a], %[a] //Factor0 lo [0:15] \n"
            "uxth %[t0], %[b] //Factor1 lo [0:15] \n"
            "asrs %[b], %[b], #16 //Factor1 hi [16:31] \n"

            "muls %[a], %[b] //Factor0 lo * Factor1 hi \n"
            "muls %[t0], %[t1] //Factor1 lo * Factor0 hi \n"
            "muls %[b], %[t1] //Factor1 hi * Factor0 hi \n"

            "adds %[a], %[t0] //(Factor0 lo * Factor1 hi) + (Factor1 lo * Factor0 hi) \n"

            // TODO surely this is important? but output is 1 off with negative numbers
            //"movs %[t0], #0 \n"
            //"adcs %[t0], %[t0] //C --> bit 16 (t0 contains $00000000 or $00010000) \n"
            //"lsls %[t0], %[t0], #16 // \n"

            "asrs %[t1], %[a], #16 //Extract partial result [bits 16-31] \n"

            //"adds %[t0], %[t1] //Partial [bits 16-47] \n"
            "adds %[b], %[t1] //Results [bit 32-63] \n" // should be t0

            : [a] "+l" (a), [b] "+l" (b), [t0] "+l" (t0), [t1] "+l" (t1)
            : );
    return b;
}

//static int mulhs(int a, int b) {
//    register int t0, t1, t2;
//    __asm__ volatile (
//            " .syntax unified\n"
//
//            "uxth    %[t0],%[a]           // b \n"
//            "asrs    %[a],%[a],#16       // a \n"
//            "asrs    %[t1],%[b],#16       // c \n"
//            "uxth    %[b],%[b]           // d \n"
//            "movs    %[t2],%[b]           // d \n"
//
//            "muls    %[b],%[t0]           // bd \n"
//            "muls    %[t2],%[a]           // ad \n"
//            "muls    %[a],%[t1]           // ac \n"
//            "muls    %[t1],%[t0]           // bc \n"
//
//            "lsls    %[t0],%[t2],#16 \n"
//            "asrs    %[t2],%[t2],#16 \n"
//            "adds    %[b],%[t0] \n"
//            "adcs    %[a],%[t2] \n"
//            "lsls    %[t0],%[t1],#16 \n"
//            "asrs    %[t1],%[t1],#16 \n"
//            "adds    %[b],%[t0] \n"
//            "adcs    %[a],%[t1] \n"
//
//            : [a] "+l" (a), [b] "+l" (b), [t0] "+l" (t0), [t1] "+l" (t1), [t2] "+l" (t2)
//            : );
//    return a;
//}

static void biquad_step(volatile q31_t *pIn, volatile q31_t *pOut, volatile q31_t *pStateLeft,
        volatile q31_t *pStateRight, volatile q31_t *pCoeffs, uint32_t blockSize, channel_sel_t channel_sel) {
    // Reading the coefficients
    const q31_t b0 = pCoeffs[0];
    const q31_t b1 = pCoeffs[1];
    const q31_t b2 = pCoeffs[2];
    const q31_t a1 = pCoeffs[3];
    const q31_t a2 = pCoeffs[4];

    volatile q31_t *pState = (channel_sel == RIGHT_CHANNEL) ? pStateRight : pStateLeft;

    // Reading the pState values
    q31_t Xn1 = pState[0];
    q31_t Xn2 = pState[1];
    q31_t Yn1 = pState[2];
    q31_t Yn2 = pState[3];

    int stride = channel_sel == BOTH_CHANNELS ? 1 : 2;
#pragma GCC unroll 4
    for(int i = (channel_sel == RIGHT_CHANNEL) ? 1 : 0; i < blockSize * 2; i += stride) {

#ifdef ENABLE_BOTH_CHANNELS
        if(channel_sel == BOTH_CHANNELS) {
            pState = (i % 2 == 0) ? pStateLeft : pStateRight;

            Xn1 = pState[0];
            Xn2 = pState[1];
            Yn1 = pState[2];
            Yn2 = pState[3];
        }
#endif

        // Read the input
        q31_t Xn = pIn[i];

        // acc =  b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] + a1 * y[n-1] + a2 * y[n-2]
        q31_t acc = mulhs(b0, Xn) + mulhs(b1, Xn1) + mulhs(b2, Xn2) + mulhs(a1, Yn1) + mulhs(a2, Yn2);

        // The result is converted to 1.31
        acc = acc << COEFF_POSTSHIFT + 1;

        // Store output in destination buffer.
        pOut[i] = acc;

        /* Every time after the output is computed state should be updated.
         * The states should be updated as:
         * Xn2 = Xn1
         * Xn1 = Xn
         * Yn2 = Yn1
         * Yn1 = acc */
        Xn2 = Xn1;
        Xn1 = Xn;
        Yn2 = Yn1;
        Yn1 = (q31_t) acc;

#ifdef ENABLE_BOTH_CHANNELS
        if(channel_sel == BOTH_CHANNELS) {
            // Store the updated state variables back into the pState array
            pState[0] = Xn1;
            pState[1] = Xn2;
            pState[2] = Yn1;
            pState[3] = Yn2;
        }
#endif
    }

    // Store the updated state variables back into the pState array
    pState[0] = Xn1;
    pState[1] = Xn2;
    pState[2] = Yn1;
    pState[3] = Yn2;
}

void biquad_eq_init(void) {
    // TODO read stored gains
    freq_band_gains[0] = 0;
    freq_band_gains[1] = 0;
    freq_band_gains[2] = 0;
    freq_band_gains[3] = 0;
    freq_band_gains[4] = 0;
    freq_band_gains[5] = 0;
    freq_band_gains[6] = 0;
    freq_band_gains[7] = 0;

    // calculate default coefficients and init cascades
    biquad_eq_update_coeffs();
}

static void core1_irq_handler() {
    while(multicore_fifo_rvalid()) {
        multicore_fifo_pop_blocking();

        for(int stage = 0; stage < NUM_EQ_STAGES; stage++) {
            biquad_step(samples32, samples32, &biquad_state_l[4 * stage],
                    &biquad_state_r[4 * stage], &freq_band_coeffs[stage * 5], sample_cnt, RIGHT_CHANNEL);
        }
    }

    multicore_fifo_clear_irq();
    multicore_fifo_push_blocking(0);
}

void biquad_eq_init_core1(void) {
    multicore_fifo_clear_irq();
    irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_irq_handler);
    irq_set_enabled(SIO_IRQ_PROC1, true);
}

void biquad_eq_process_inplace(int16_t* samples, int16_t len) {
    if(!eq_enabled) {
        return;
    }

    sample_cnt = len;
    assert((sample_cnt * 2) <= TMP_BUFFER_LEN);

    // Scale down and convert to Q31
    for(int i = 0; i < len * 2; i++) {
      q31_t Xn = ((q31_t) samples[i]) << 14;
      samples32[i] = mulhs(0x7FFFFFFF, Xn);
      samples32[i] = samples32[i] >> 3;
    }

    __dmb();

    multicore_fifo_push_blocking(0);

    // Run through all cascades
    for(int stage = 0; stage < NUM_EQ_STAGES; stage++) {
        biquad_step(samples32, samples32, &biquad_state_l[4 * stage],
                &biquad_state_r[4 * stage], &freq_band_coeffs[stage * 5], len, LEFT_CHANNEL); // BOTH_CHANNELS
    }

    while(multicore_fifo_rvalid()) {
        multicore_fifo_pop_blocking();
    }

    __dmb();

    // Convert back to Q16
    for(int i = 0; i < len * 2; i++) {
      samples[i] = clip_q31_to_q15(samples32[i] >> 14);
    }
}
