/*
 * encoder.c
 *
 *  Based on https://github.com/pimoroni/pimoroni-pico/tree/encoder-pio/drivers/encoder-pio
 *
 */

#include "stdint.h"
#include "stdbool.h"
#include <math.h>
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "encoder.h"
#include "encoder.pio.h"

#define LAST_STATE(state)  ((state) & 0b0011)
#define CURR_STATE(state)  (((state) & 0b1100) >> 2)

#define DEFAULT_COUNTS_PER_REV     24
#define DEFAULT_COUNT_MICROSTEPS   0
#define DEFAULT_FREQ_DIVIDER       250

#define STATE_A_MASK       0x80000000
#define STATE_B_MASK       0x40000000
#define STATE_A_LAST_MASK  0x20000000
#define STATE_B_LAST_MASK  0x10000000

#define STATES_MASK (STATE_A_MASK | STATE_B_MASK | STATE_A_LAST_MASK | STATE_B_LAST_MASK)

#define TIME_MASK    0x0fffffff

#define MICROSTEP_0   0b00
#define MICROSTEP_1   0b10
#define MICROSTEP_2   0b11
#define MICROSTEP_3   0b01

enum Direction {
	NO_DIR        = 0,
	CLOCKWISE     = 1,
	COUNTERCLOCK  = -1,
};

const int pinA = 19;
const int pinB = 20;

const float counts_per_revolution   = DEFAULT_COUNTS_PER_REV;
const bool count_microsteps         = DEFAULT_COUNT_MICROSTEPS;
const uint16_t freq_divider         = DEFAULT_FREQ_DIVIDER;
const float clocks_per_time         = 0;

//--------------------------------------------------

static uint enc_sm         = 0;

static volatile uint8_t stateA                = 0;
static volatile uint8_t stateB                = 0;
static volatile int32_t count              = 0;
static volatile int32_t time_since         = 0;
static volatile int8_t last_travel_dir  = NO_DIR;
static volatile int32_t microstep_time     = 0;
static volatile int32_t cumulative_time    = 0;

static int32_t last_captured_count         = 0;

static const PIO enc_pio = pio1;

static void __not_in_flash_func(microstep_up)(int32_t time) {
	count++;
	time_since = time;
	microstep_time = 0;

	if(time + cumulative_time < time)  //Check to avoid integer overflow
		cumulative_time = INT32_MAX;
	else
		cumulative_time += time;
}

static void __not_in_flash_func(microstep_down)(int32_t time) {
	count--;
	time_since = 0 - time;
	microstep_time = 0;

	if(time + cumulative_time < time)  //Check to avoid integer overflow
		cumulative_time = INT32_MAX;
	else
		cumulative_time += time;
}

static void __not_in_flash_func(pio1_interrupt_callback)() {
	while(enc_pio->ints0 & (PIO_IRQ0_INTS_SM0_RXNEMPTY_BITS << enc_sm)) {
		uint32_t received = pio_sm_get(enc_pio, enc_sm);

		// Extract the current and last encoder states from the received value
		stateA = (received & STATE_A_MASK);
		stateB = (received & STATE_B_MASK);
		uint8_t states = (received & STATES_MASK) >> 28;

		// Extract the time (in cycles) it has been since the last received
		int32_t time_received = (received & TIME_MASK) + ENC_DEBOUNCE_TIME;

		// For rotary encoders, only every fourth transition is cared about, causing an inaccurate time value
		// To address this we accumulate the times received and zero it when a transition is counted
		if(!count_microsteps) {
			if(time_received + microstep_time < time_received)  //Check to avoid integer overflow
				time_received = INT32_MAX;
			else
				time_received += microstep_time;
			microstep_time = time_received;
		}

        // Determine what transition occurred
        switch(LAST_STATE(states)) {
        //--------------------------------------------------
        case MICROSTEP_0:
            switch(CURR_STATE(states)) {
            // A ____|‾‾‾‾
            // B _________
            case MICROSTEP_1:
                if(count_microsteps)
                microstep_up(time_received);
                break;

                // A _________
                // B ____|‾‾‾‾
            case MICROSTEP_3:
                if(count_microsteps)
                microstep_down(time_received);
                break;
            }
            break;

            //--------------------------------------------------
        case MICROSTEP_1:
            switch(CURR_STATE(states)) {
            // A ‾‾‾‾‾‾‾‾‾
            // B ____|‾‾‾‾
            case MICROSTEP_2:
                if(count_microsteps || last_travel_dir == CLOCKWISE)
                microstep_up(time_received);

                last_travel_dir = NO_DIR;  //Finished turning clockwise
                break;

                // A ‾‾‾‾|____
                // B _________
            case MICROSTEP_0:
                if(count_microsteps)
                microstep_down(time_received);
                break;
            }
            break;

            //--------------------------------------------------
        case MICROSTEP_2:
            switch(CURR_STATE(states)) {
            // A ‾‾‾‾|____
            // B ‾‾‾‾‾‾‾‾‾
            case MICROSTEP_3:
                if(count_microsteps)
                microstep_up(time_received);

                last_travel_dir = CLOCKWISE;  //Started turning clockwise
                break;

                // A ‾‾‾‾‾‾‾‾‾
                // B ‾‾‾‾|____
            case MICROSTEP_1:
                if(count_microsteps)
                microstep_down(time_received);

                last_travel_dir = COUNTERCLOCK; //Started turning counter-clockwise
                break;
            }
            break;

            //--------------------------------------------------
        case MICROSTEP_3:
            switch(CURR_STATE(states)) {
            // A _________
            // B ‾‾‾‾|____
            case MICROSTEP_0:
                if(count_microsteps)
                microstep_up(time_received);
                break;

                // A ____|‾‾‾‾
                // B ‾‾‾‾‾‾‾‾‾
            case MICROSTEP_2:
                if(count_microsteps || last_travel_dir == COUNTERCLOCK)
                microstep_down(time_received);

                last_travel_dir = NO_DIR;    //Finished turning counter-clockwise
                break;
            }
            break;
        }
    }
}

void encoder_init(void) {
	int enc_sm = pio_claim_unused_sm(enc_pio, true);
	uint pio_idx = pio_get_index(enc_pio);

	//Add the program to the PIO memory and enable the appropriate interrupt
	uint enc_offset = pio_add_program(enc_pio, &encoder_program);
	encoder_program_init(enc_pio, enc_sm, enc_offset, pinA, pinB, freq_divider);
	hw_set_bits(&enc_pio->inte0, PIO_IRQ0_INTE_SM0_RXNEMPTY_BITS << enc_sm);

	irq_set_exclusive_handler(PIO1_IRQ_0, pio1_interrupt_callback);
	irq_set_enabled(PIO1_IRQ_0, true);
	irq_set_priority(PIO1_IRQ_0, PICO_DEFAULT_IRQ_PRIORITY + 2);

	//Read the current state of the encoder pins and start the PIO program on the SM
	stateA = gpio_get(pinA);
	stateB = gpio_get(pinB);
	encoder_program_start(enc_pio, enc_sm, stateA, stateB);
}

int32_t encoder_get_delta(void) {
    int32_t delta = count - last_captured_count;
    last_captured_count = count;

    return delta * -1;
}
