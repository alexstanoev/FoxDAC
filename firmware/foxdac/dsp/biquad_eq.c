/*
 * biquad_eq.c
 *
 *  Created on: 25 Sep 2021
 *      Author: alex
 */

/**
 * @defgroup GEQ5Band Graphic Audio Equalizer Example
 *
 * \par Description:
 * \par
 * This example demonstrates how a 5-band graphic equalizer can be constructed
 * using the Biquad cascade functions.
 * A graphic equalizer is used in audio applications to vary the tonal quality
 * of the audio.
 *
 * \par Block Diagram:
 * \par
 * The design is based on a cascade of 5 filter sections.
 * \image html GEQ_signalflow.gif
 * Each filter section is 4th order and consists of a cascade of two Biquads.
 * Each filter has a nominal gain of 0 dB (1.0 in linear units) and
 * boosts or cuts signals within a specific frequency range.
 * The edge frequencies between the 5 bands are 100, 500, 2000, and 6000 Hz.
 * Each band has an adjustable boost or cut in the range of +/- 9 dB.
 * For example, the band that extends from 500 to 2000 Hz has the response shown below:
 * \par
 * \image html GEQ_bandresponse.gif
 * \par
 * With 1 dB steps, each filter has a total of 19 different settings.
 * The filter coefficients for all possible 19 settings were precomputed
 * in MATLAB and stored in a table.  With 5 different tables, there are
 * a total of 5 x 19 = 95 different 4th order filters.
 * All 95 responses are shown below:
 * \par
 * \image html GEQ_allbandresponse.gif
 * \par
 * Each 4th order filter has 10 coefficents for a grand total of 950 different filter
 * coefficients that must be tabulated. The input and output data is in Q31 format.
 * For better noise performance, the two low frequency bands are implemented using the high
 * precision 32x64-bit Biquad filters. The remaining 3 high frequency bands use standard
 * 32x32-bit Biquad filters. The input signal used in the example is a logarithmic chirp.
 * \par
 * \image html GEQ_inputchirp.gif
 * \par
 * The array <code>bandGains</code> specifies the gain in dB to apply in each band.
 * For example, if <code>bandGains={0, -3, 6, 4, -6};</code> then the output signal will be:
 * \par
 * \image html GEQ_outputchirp.gif
 * \par
 * \note The output chirp signal follows the gain or boost of each band.
 * \par
 *
 * \par Variables Description:
 * \par
 * \li \c testInput_f32 points to the input data
 * \li \c testRefOutput_f32 points to the reference output data
 * \li \c testOutput points to the test output data
 * \li \c inputQ31 temporary input buffer
 * \li \c outputQ31 temporary output buffer
 * \li \c biquadStateBand1Q31 points to state buffer for band1
 * \li \c biquadStateBand2Q31 points to state buffer for band2
 * \li \c biquadStateBand3Q31 points to state buffer for band3
 * \li \c biquadStateBand4Q31 points to state buffer for band4
 * \li \c biquadStateBand5Q31 points to state buffer for band5
 * \li \c coeffTable points to coefficient buffer for all bands
 * \li \c gainDB gain buffer which has gains applied for all the bands
 *
 * \par CMSIS DSP Software Library Functions Used:
 * \par
 * - arm_biquad_cas_df1_32x64_init_q31()
 * - arm_biquad_cas_df1_32x64_q31()
 * - arm_biquad_cascade_df1_init_q31()
 * - arm_biquad_cascade_df1_q31()
 * - arm_scale_q31()
 * - arm_scale_f32()
 * - arm_float_to_q31()
 * - arm_q31_to_float()
 *
 * <b> Refer  </b>
 * \link arm_graphic_equalizer_example_q31.c \endlink
 *
 */

#include "stdio.h"

#include "arm_math.h"

/* Block size for the underlying processing */
#define BLOCKSIZE 32

/* Number of 2nd order Biquad stages per filter */
#define NUMSTAGES 2

/* ----------------------------------------------------------------------
 ** Q31 state buffers for Band1, Band2, Band3, Band4, Band5
 ** ------------------------------------------------------------------- */

static q63_t biquadStateBand1Q31[4 * 2 * 2];
static q63_t biquadStateBand2Q31[4 * 2 * 2];
static q31_t biquadStateBand3Q31[4 * 2 * 2];
static q31_t biquadStateBand4Q31[4 * 2 * 2];
static q31_t biquadStateBand5Q31[4 * 2 * 2];

static arm_biquad_cas_df1_32x64_ins_q31 S1[2];
static arm_biquad_cas_df1_32x64_ins_q31 S2[2];
static arm_biquad_casd_df1_inst_q31 S3[2];
static arm_biquad_casd_df1_inst_q31 S4[2];
static arm_biquad_casd_df1_inst_q31 S5[2];

/* ----------------------------------------------------------------------
 ** Q31 input and output buffers
 ** ------------------------------------------------------------------- */

static q31_t inputQ31[BLOCKSIZE];

// b10 b11 b12 a11 a12 .. b20 b21
static q31_t coeffCalc[5];

/* ----------------------------------------------------------------------
 ** Desired gains, in dB, per band
 ** ------------------------------------------------------------------- */

//int gainDB[5] = {0, 0, 0, 0, 0};

/* ----------------------------------------------------------------------
 * Graphic equalizer Example
 * ------------------------------------------------------------------- */

// based on http://www.earlevel.com/scripts/widgets/20131013/biquads2.js
static void calc_biquad_peaking_coeff(double Q, double peakGain, double Fc, double Fs) {
    double norm, a0, a1, a2, b1, b2;

    double V = pow(10, fabs(peakGain) / 20.0);
    double K = tan(M_PI * (Fc / Fs));

    if (peakGain >= 0) {
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

    // negate feedback terms
    b1 *= -1.0;
    b2 *= -1.0;

    printf("%f %f %f %f %f\n", a0, a1, a2, b1, b2);

    // store as Q29 + postshift 2 for Q31
    coeffCalc[0] = clip_q63_to_q31((q63_t) (a0 * 536870912.0f)); // b10
    coeffCalc[1] = clip_q63_to_q31((q63_t) (a1 * 536870912.0f)); // b11
    coeffCalc[2] = clip_q63_to_q31((q63_t) (a2 * 536870912.0f)); // b12
    coeffCalc[3] = clip_q63_to_q31((q63_t) (b1 * 536870912.0f)); // a11
    coeffCalc[4] = clip_q63_to_q31((q63_t) (b2 * 536870912.0f)); // a12

    printf("%d %d %d %d %d\n", coeffCalc[0], coeffCalc[1], coeffCalc[2], coeffCalc[3], coeffCalc[3]);
}

void biquad_eq_init(void)
{
    int i;
    int32_t status;

    /* Initialize the state and coefficient buffers for all Biquad sections */

    calc_biquad_peaking_coeff(0.7071, 0.0, 100.0, 48000.0);

//    arm_biquad_cas_df1_32x64_init_q31(&S1[0], 1,
//                    (q31_t *) coeffCalc,
//                    &biquadStateBand1Q31[0 * 8], 2);
//
//    arm_biquad_cas_df1_32x64_init_q31(&S1[1], 1,
//                    (q31_t *) coeffCalc,
//                    &biquadStateBand1Q31[1 * 8], 2);

    arm_biquad_cascade_df1_init_q31(&S3[0], 1, coeffCalc, &biquadStateBand3Q31[0 * 8], 2);

    arm_biquad_cascade_df1_init_q31(&S3[1], 1, coeffCalc, &biquadStateBand3Q31[1 * 8], 2);

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

    /* Call the process functions and needs to change filter coefficients
       for varying the gain of each band */

    for(int i = 0; i < numblocks; i++) {
        for(int chan = 0; chan <= 1; chan++) {

            int k = 0;
            for(int j = 0; j < BLOCKSIZE * 2; j += 2) {
                inputQ31[k++] = ((q31_t) samples[(i * BLOCKSIZE * 2) + j + chan]) << 16;
            }

            /* ----------------------------------------------------------------------
             ** Scale down by 1/8.  This provides additional headroom so that the
             ** graphic EQ can apply gain.
             ** ------------------------------------------------------------------- */

            //arm_scale_q31(inputQ31, 0x7FFFFFFF, -3, inputQ31, BLOCKSIZE);

            /* ----------------------------------------------------------------------
             ** Call the Q31 Biquad Cascade DF1 32x64 process function for band1, band2
             ** ------------------------------------------------------------------- */

            //arm_biquad_cas_df1_32x64_q31(&S1[chan], inputQ31, inputQ31, BLOCKSIZE);
//            arm_biquad_cas_df1_32x64_q31(&S2[chan], inputQ31, inputQ31, BLOCKSIZE);
//
//            /* ----------------------------------------------------------------------
//             ** Call the Q31 Biquad Cascade DF1 process function for band3, band4, band5
//             ** ------------------------------------------------------------------- */
            arm_biquad_cascade_df1_q31(&S3[chan], inputQ31, inputQ31, BLOCKSIZE);
//            arm_biquad_cascade_df1_q31(&S4[chan], inputQ31, inputQ31, BLOCKSIZE);
//            arm_biquad_cascade_df1_q31(&S5[chan], inputQ31, inputQ31, BLOCKSIZE);

            //arm_scale_q31(inputQ31, 0x7FFFFFFF, 3, inputQ31, BLOCKSIZE);

            /* ----------------------------------------------------------------------
             ** Convert Q31 result back to float
             ** ------------------------------------------------------------------- */

            k = 0;
            for(int j = 0; j < BLOCKSIZE * 2; j += 2) {
                samples[(i * BLOCKSIZE * 2) + j + chan] = (int16_t) (inputQ31[k++] >> 16);
            }
        }
    }
}




