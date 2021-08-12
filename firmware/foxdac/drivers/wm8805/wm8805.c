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

#define bitRead(x, n) (((x) & (1 << (n))) != 0)

static void init_spi(void) {
    spi_init(SPI_PORT, 500 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

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

static uint8_t read_reg(uint8_t regaddr) {
    uint8_t buf;

    regaddr |= READ_BIT;

    cs_select();
    spi_write_blocking(SPI_PORT, &regaddr, 1);
    spi_read_blocking(SPI_PORT, 0, &buf, 1);
    cs_deselect();

    return buf;
}


static void write_reg(uint8_t regaddr, uint8_t dataval) {
    regaddr &= ~READ_BIT;

    cs_select();
    spi_write_blocking(SPI_PORT, &regaddr, 1);
    spi_write_blocking(SPI_PORT, &dataval, 1);
    cs_deselect();
}

static void init_device(void) {
    // reset device
    write_reg(0, 0);

    sleep_ms(10);

    // REGISTER 7
    // bit 7:6 - always 0
    // bit 5:4 - CLKOUT divider select => 00 = 512 fs, 01 = 256 fs, 10 = 128 fs, 11 = 64 fs
    // bit 3 - MCLKDIV select => 0
    // bit 2 - FRACEN => 1
    // bit 1:0 - FREQMODE (is written by S/PDIF receiver) => 00
    write_reg(7, 0b00000100);

    // REGISTER 8
    // set clock outputs and turn off last data hold
    // bit 7 - MCLK output source select is CLK2 => 0
    // bit 6 - always valid => 0
    // bit 5 - fill mode select => 1 (we need to see errors when they happen)
    // bit 4 - CLKOUT pin disable => 1
    // bit 3 - CLKOUT pin select is CLK1 => 0
    // bit 2:0 - always 0
    write_reg(8, 0b00110000);

    // set masking for interrupts
    write_reg(10, 126);  // 1+2+3+4+5+6 => 0111 1110. We only care about unlock and rec_freq

    // set the AIF TX
    // bit 7:6 - always 0
    // bit   5 - LRCLK polarity => 0
    // bit   4 - BCLK invert => 0
    // bit 3:2 - data word length => 10 (24b) or b0 (16b)
    // bit 1:0 - format select: 11 (dsp), 10 (i2s), 01 (LJ), 00 (RJ)
    write_reg(27, 0b00001010);

    // set the AIF RX
    // bit   7 - SYNC => 1
    // bit   6 - master mode => 1
    // bit   5 - LRCLK polarity => 0
    // bit   4 - BCLK invert => 0
    // bit 3:2 - data word length => 10 (24b) or 00 (16b)
    // bit 1:0 - format select: 11 (dsp), 10 (i2s), 01 (LJ), 00 (RJ)
    write_reg(28, 0b11001010);

    // set PLL K and N factors
    // this should be sample rate dependent, but makes hardly any difference
    write_reg(6, 7);                  // set PLL_N to 7
    write_reg(5, 0x36);                 // set PLL_K to 36FD21 (36)
    write_reg(4, 0xFD);                 // set PLL_K to 36FD21 (FD)
    write_reg(3, 0x21);                 // set PLL_K to 36FD21 (21)

    // set all inputs for TTL
    write_reg(9, 0);

    // power up device
    write_reg(30, 0);

    // select input
    // bit   7 - MCLK Output Source Select => 0 (CLK2)
    // bit   6 - Always Valid Select => 0
    // bit   5 - Fill Mode Select => 0
    // bit   4 - CLKOUT Pin Disable => 1
    // bit   3 - CLKOUT Pin Source Select => 1)
    // bit 2:0 - S/PDIF Rx Input Select: 000 – RX0, 001 – RX1, 010 – RX2, 011 – RX3, 100 – RX4, 101 – RX5, 110 – RX6, 111 – RX7
    //write_reg(8, 0b00011000);           // Select Input 1 (Coax)
    write_reg(8, 0b00011011);           // Select Input 4 (TOSLINK)
}

void wm8805_init(void) {
    init_spi();
    init_device();

    sleep_ms(10);

    // check device id
    uint8_t did = read_reg(1);
    printf("Found WM: %x\n", did);
}

void wm8805_set_input(uint8_t input) {
    write_reg(8, 0b00011000 | (input & 0b0000111));
}

static uint8_t pll_mode = 0;
static uint8_t fs = 0;

static const char* sr_str = "N/A";

static const char* get_samplerate_str(void) {
    uint8_t samplerate = read_reg(16);

    switch(samplerate) {
      case 0b0001:
      return "N/A";
      break;

      case 0b0011:
      return "32 kHz";
      break;

      case 0b1110:
      return "192 kHz";
      break;

      case 0b1010:
      return "96 kHz";
      break;

      case 0b0010:
      return "48 kHz";
      break;

      case 0b0110:
      return "24 kHz";
      break;

      case 0b1100:
      return "176.4 kHz";
      break;

      case 0b1000:
      return "88.2 kHz";
      break;

      case 0b0000:
      if (fs == 192) {
          return "192 kHz";
        }
        else
        return "44.1 kHz";
      break;

      case 0b0100:
      return "22.05 kHz";
      break;
    }
}

void wm8805_poll_intstat(void) {

    uint8_t INTSTAT = read_reg(11); // poll (and clear) interrupt register every 100ms.

    // decode why interrupt was triggered
    // most of these shouldn't happen as they are masked off
    // but still useful for debugging

    uint8_t SPDSTAT = 0;
    if (bitRead(INTSTAT, 0)) {                                         // UPD_UNLOCK
        SPDSTAT = read_reg(12);
        puts("UPD_UNLOCK: ");
        if (bitRead(SPDSTAT,6)) {
            //delay(500);

            // try again to try to reject transient errors
            SPDSTAT = read_reg(12);
            if (bitRead(SPDSTAT,6)) {

                puts("S/PDIF PLL unlocked");

                ui_set_sr_text("PLL ERROR");

                // switch PLL coeffs around to try to find stable setting

//                if (pll_mode) {
//                    puts("trying 192 kHz mode...");
//                    fs = 192;
//                    write_reg(6, 8);                  // set PLL_N to 8
//                    write_reg(5, 12);                 // set PLL_K to 0C49BA (0C)
//                    write_reg(4, 73);                 // set PLL_K to 0C49BA (49)
//                    write_reg(3, 186);                // set PLL_K to 0C49BA (BA)
//                    pll_mode = 0;
//                    //delay(500);
//                }
//                else {
                    //fs = 0;
                    puts("trying normal mode...");
                    write_reg(6, 7);                  // set PLL_N to 7
                    write_reg(5, 54);                 // set PLL_K to 36FD21 (36)
                    write_reg(4, 253);                // set PLL_K to 36FD21 (FD)
                    write_reg(3, 33);                 // set PLL_K to 36FD21 (21)
                    pll_mode = 1;
                    //delay(500);
//                } // if toggle


            } // if (bitRead(SPDSTAT,6))

        }
        else {
            puts("S/PDIF PLL locked");
        }
        //delay(50); // sort of debounce
    }

    if (bitRead(INTSTAT, 1)) {                                         // INT_INVALID
        puts("INT_INVALID");
    }

    if (bitRead(INTSTAT, 2)) {                                         // INT_CSUD
        puts("INT_CSUD");

        printf("CSU %x\n", read_reg(0x0D));
    }

    if (bitRead(INTSTAT, 3)) {                                         // INT_TRANS_ERR
        puts("INT_TRANS_ERR");
    }

    if (bitRead(INTSTAT, 4)) {                                         // UPD_NON_AUDIO
        puts("UPD_NON_AUDIO");
    }

    if (bitRead(INTSTAT, 5)) {                                         // UPD_CPY_N
        puts("UPD_CPY_N");
    }

    if (bitRead(INTSTAT, 6)) {                                         // UPD_DEEMPH
        puts("UPD_DEEMPH");
    }

    if (bitRead(INTSTAT, 7)) {                                         // UPD_REC_FREQ

        SPDSTAT = read_reg(12);
        int samplerate = 2*bitRead(SPDSTAT,5) + bitRead(SPDSTAT,4);      // calculate indicated rate
        //Serial.print("UPD_REC_FREQ: ");
        puts("Sample rate: ");

        switch (samplerate) {
        case 3:
            puts("32 kHz");
            fs = 32;
            write_reg(29, 0);                 // set SPD_192K_EN to 0
            //delay(500);
            break;

        case 2:
            puts("44 / 48 kHz");
            fs = 48;
            write_reg(29, 0);                 // set SPD_192K_EN to 0
            //delay(500);
            break;

        case 1:
            puts("88 / 96 kHz");
            fs = 96;
            write_reg(29, 0);                 // set SPD_192K_EN to 0
            //delay(500);
            break;

        case 0:
            puts("192 kHz");
            fs = 192;
            write_reg(29, 128);                 // set SPD_192K_EN to 1
            //delay(500);
            break;
        } // switch samplerate


        const char* new_sr_str = get_samplerate_str();

        if(sr_str != new_sr_str) {
            sr_str = new_sr_str;

            printf("Detected SR: %s\n", sr_str);

            ui_set_sr_text(sr_str);
        }


    } // if (bitRead(INTSTAT, 7))


}


