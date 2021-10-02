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
static volatile q31_t freq_band_coeffs[5 * NUM_EQ_STAGES];

// 4 vars * 2 channels * 10 bands
static volatile q31_t biquad_state[4 * 2 * NUM_EQ_STAGES];

static int curr_fs = 48000;

volatile int16_t* core2_sample_buf;
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
    for(int i = 0; i < NUM_EQ_STAGES * 2; i += 2) {
        for(int chan = 0; chan <= 1; chan++) {
            memset((q31_t*) &biquad_state[4 * (i + chan)], 0, 4 * sizeof(q31_t));
        }
    }
}

void biquad_eq_set_fs(int fs) {
    curr_fs = fs;
    biquad_eq_update_coeffs();
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
static int mulhs(int a, int b) {
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

//static __attribute__((always_inline)) int mulhs(int a, int b) {
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

static void biquad_step(volatile q15_t *pIn, volatile q15_t *pOut, volatile q31_t *pStateLeft,
        volatile q31_t *pStateRight, volatile q31_t *pCoeffs, uint32_t blockSize, channel_sel_t channel_sel) {
    // Reading the coefficients
    const q31_t b0 = *pCoeffs++;
    const q31_t b1 = *pCoeffs++;
    const q31_t b2 = *pCoeffs++;
    const q31_t a1 = *pCoeffs++;
    const q31_t a2 = *pCoeffs++;

    int stride = channel_sel == BOTH_CHANNELS ? 1 : 2;
    for(int step = 0; step < blockSize * 2; step += stride) {
        int i = (channel_sel == RIGHT_CHANNEL) ? step + 1 : step;
        volatile q31_t *pState = (i % 2 == 0) ? pStateLeft : pStateRight;

        // Reading the pState values
        q31_t Xn1 = pState[0];
        q31_t Xn2 = pState[1];
        q31_t Yn1 = pState[2];
        q31_t Yn2 = pState[3];

        // Read the input
        q31_t Xn = ((q31_t) pIn[i]) << 16;

        // Scale
        // TODO should probably scale by less, this makes it too quiet
        //Xn = (((q63_t) Xn) * ((q31_t) 0x7FFFFFFF)) >> 32;
        //Xn = Xn >> 2;

        // acc =  b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] + a1 * y[n-1] + a2 * y[n-2]
        q31_t acc = mulhs(b0, Xn) + mulhs(b1, Xn1) + mulhs(b2, Xn2) + mulhs(a1, Yn1) + mulhs(a2, Yn2);

        // The result is converted to 1.31
        acc = acc << COEFF_POSTSHIFT + 1;

        // Store output in destination buffer.
        pOut[i] = (int16_t) (acc >> 16);

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

        // Store the updated state variables back into the pState array
        pState[0] = Xn1;
        pState[1] = Xn2;
        pState[2] = Yn1;
        pState[3] = Yn2;
    }
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

//    printf("100x100 %lx %lx\n", mulhs(100, 100), mulhs3(100, 100));
//    printf("DB %lx %lx %lx\n", mulhsc(0xDEADBEEF, 0xDEADC0DE), mulhs(0xDEADBEEF, 0xDEADC0DE), mulhs2(0xDEADBEEF, 0xDEADC0DE));
//    printf("DB %lx %lx %lx\n", mulhsc(0x55555555, 0x10000000), mulhs(0x55555555, 0x10000000), mulhs2(0x55555555, 0x10000000));

//    printf("5 %lx %lx\n", mulhs(0x55555555, 0x55555555), mulhs3(0x55555555, 0x55555555));
//    printf("eq %lx %lx\n", mulhs(0xDEADBEEF, 0xDEADBEEF), mulhs3(0xDEADBEEF, 0xDEADBEEF));
//    printf("5sh %lx %lx\n", mulhs(0x55555555, 0x10000000), mulhs3(0x55555555, 0x10000000));
//    printf("5sh %lx %lx\n", mulhs(0x80000000, 0xFFFFFFFF), mulhs3(0x80000000, 0xFFFFFFFF));
}

static void core1_irq_handler() {
    while(multicore_fifo_rvalid()) {
        multicore_fifo_pop_blocking();

        for(int stage = 0; stage < NUM_EQ_STAGES * 2; stage += 2) {
            biquad_step(core2_sample_buf, core2_sample_buf, &biquad_state[4 * (stage + 0)],
                    &biquad_state[4 * (stage + 1)], &freq_band_coeffs[(stage / 2) * 5], sample_cnt, RIGHT_CHANNEL);
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
    sample_cnt = len;
    core2_sample_buf = samples;

    multicore_fifo_push_blocking(0);

    // Run through all cascades
    // TODO 4 should be NUM_EQ_STAGES but that takes too long for the IRQ
    for(int stage = 0; stage < NUM_EQ_STAGES * 2; stage += 2) {
        biquad_step((volatile int16_t*) samples, (volatile int16_t*) samples, &biquad_state[4 * (stage + 0)],
                &biquad_state[4 * (stage + 1)], &freq_band_coeffs[(stage / 2) * 5], len, LEFT_CHANNEL); // BOTH_CHANNELS
    }

    while(multicore_fifo_rvalid()) {
        multicore_fifo_pop_blocking();
    }
}
