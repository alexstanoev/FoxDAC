// https://github.com/pmarques-dev/pico-examples/blob/master/pio/quadrature_encoder/quadrature_encoder.c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

#include "quadrature_encoder.pio.h"

//
// ---- quadrature encoder interface example
//
// the PIO program reads phase A/B of a quadrature encoder and increments or
// decrements an internal counter to keep the current absolute step count
// updated. At any point, the main code can query the current count by using
// the quadrature_encoder_*_count functions. The counter is kept in a full
// 32 bit register that just wraps around. Two's complement arithmetic means
// that it can be interpreted as a 32 bit signed or unsigned value and it will
// work anyway.
//
// As an example, a two wheel robot being controlled at 100Hz, can use two
// state machines to read the two encoders and in the main control loop it can
// simply ask for the current encoder counts to get the absolute step count. It
// can also subtract the values from the last sample to check how many steps
// each wheel as done since the last sample period.
//
// One advantage of this approach is that it requires zero CPU time to keep the
// encoder count updated and because of that it supports very high step rates.
//

// Base pin to connect the A phase of the encoder.
// The B phase must be connected to the next pin
const uint PIN_AB = 12;

int32_t new_value, delta, old_value = 0;

PIO pio = pio1;
uint sm = 0;

void encoder_init(void) {
	sm = pio_claim_unused_sm(pio, true);

	uint offset = pio_add_program(pio, &quadrature_encoder_program);
	quadrature_encoder_program_init(pio, sm, offset, PIN_AB, 0);
}

int32_t encoder_get_count(void) {
	new_value = quadrature_encoder_get_count(pio, sm);
	delta = new_value - old_value;
	old_value = new_value;

	printf("position %8d, delta %6d\n", new_value, delta);

	return new_value;
}
