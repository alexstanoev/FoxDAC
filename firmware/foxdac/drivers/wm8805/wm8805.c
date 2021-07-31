/*
 * wm8805.c
 *
 *  Created on: 30 Jul 2021
 *      Author: alex
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  2
#define PIN_MOSI 3

#define SPI_PORT spi0
#define READ_BIT 0x80

void wm8805_init_spi(void) {
    spi_init(SPI_PORT, 500 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
}

static inline void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

uint8_t ReadRegister(uint8_t devaddr, uint8_t regaddr) {
    uint8_t buf;

    regaddr |= READ_BIT;
    cs_select();
    spi_write_blocking(SPI_PORT, &regaddr, 1);
    sleep_ms(1);
    spi_read_blocking(SPI_PORT, 0, &buf, 1);
    cs_deselect();

    return buf;
}


void WriteRegister(int devaddr, uint8_t regaddr, uint8_t dataval) {
    regaddr &= ~READ_BIT;
    cs_select();
    spi_write_blocking(SPI_PORT, &regaddr, 1);
    //sleep_ms(1);
    spi_write_blocking(SPI_PORT, &dataval, 1);
    cs_deselect();
}

void DeviceInit(int devaddr) {
  // reset device
  WriteRegister(devaddr, 0, 0);

  sleep_ms(10);

  // REGISTER 7
  // 0bit 7:6 - always 0
  // 0bit 5:4 - CLKOUT divider select => 00 = 512 fs, 01 = 256 fs, 10 = 128 fs, 11 = 64 fs
  // 0bit 3 - MCLKDIV select => 0
  // 0bit 2 - FRACEN => 1
  // 0bit 1:0 - FREQMODE (is written 0by S/PDIF receiver) => 00
  WriteRegister(devaddr, 7, 0b00000100);

  // REGISTER 8
  // set clock outputs and turn off last data hold
  // 0bit 7 - MCLK output source select is CLK2 => 0
  // 0bit 6 - always valid => 0
  // 0bit 5 - fill mode select => 1 (we need to see errors when they happen)
  // 0bit 4 - CLKOUT pin disable => 1
  // 0bit 3 - CLKOUT pin select is CLK1 => 0
  // 0bit 2:0 - always 0
  WriteRegister(devaddr, 8, 0b00110000);

  // set masking for interrupts
  WriteRegister(devaddr, 10, 126);  // 1+2+3+4+5+6 => 0111 1110. We only care about unlock and rec_freq

  // set the AIF TX
  // 0bit 7:6 - always 0
  // 0bit   5 - LRCLK polarity => 0
  // 0bit   4 - 0bCLK invert => 0
  // 0bit 3:2 - data word length => 10 (24b) or 00 (16b)
  // 0bit 1:0 - format select: 11 (dsp), 10 (i2s), 01 (LJ), 00 (RJ)
  WriteRegister(devaddr, 27, 0b00001010);

  // set the AIF RX
  // 0bit   7 - SYNC => 1
  // 0bit   6 - master mode => 1
  // 0bit   5 - LRCLK polarity => 0
  // 0bit   4 - 0bCLK invert => 0
  // 0bit 3:2 - data word length => 10 (24b) or 00 (16b)
  // 0bit 1:0 - format select: 11 (dsp), 10 (i2s), 01 (LJ), 00 (RJ)
  WriteRegister(devaddr, 28, 0b11001010);

  // set PLL K and N factors
  // this should 0be sample rate dependent, 0but makes hardly any difference
  WriteRegister(devaddr, 6, 7);                  // set PLL_N to 7
  WriteRegister(devaddr, 5, 0x36);                 // set PLL_K to 36FD21 (36)
  WriteRegister(devaddr, 4, 0xFD);                 // set PLL_K to 36FD21 (FD)
  WriteRegister(devaddr, 3, 0x21);                 // set PLL_K to 36FD21 (21)

  // set all inputs for TTL
  WriteRegister(devaddr, 9, 0);

  // power up device
  WriteRegister(devaddr, 30, 0);

  // select input
  // 0bit   7 - MCLK Output Source Select => 0 (CLK2)
  // 0bit   6 - Always Valid Select => 0
  // 0bit   5 - Fill Mode Select => 0
  // 0bit   4 - CLKOUT Pin Disable => 1
  // 0bit   3 - CLKOUT Pin Source Select => 1)
  // 0bit 2:0 - S/PDIF Rx Input Select: 000 – RX0, 001 – RX1, 010 – RX2, 011 – RX3, 100 – RX4, 101 – RX5, 110 – RX6, 111 – RX7
  //WriteRegister(devaddr, 8, 0b00011000);           // Select Input 1 (Coax)
  WriteRegister(devaddr, 8, 0b00011011);           // Select Input 4 (TOSLINK)
}

void wm8805_init(void) {
    wm8805_init_spi();
    DeviceInit(0);

    sleep_ms(10);

    // check device id
    uint8_t did = ReadRegister(0, 1);
    printf("Found WM: %x\n", did);
}
