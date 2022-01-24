/*
 * tpa6130.c
 *
 *  Created on: 31 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <stdio.h>

#define TPA_I2C_PORT i2c0
#define TPA_I2C_SDA 12
#define TPA_I2C_SCL 13
#define TPA_I2C_ADDR 0x60

#define TPA6130_TWI_ADDRESS 0x60

#define TPA6130_VOL_MIN       ((int8_t)(  0)) // -59dB
#define TPA6130_VOL_MAX       ((int8_t)( 63)) // : +4dB
#define TPA6130_VOL_DEFAULT   ((int8_t)( 10))

#define TPA6130_CONTROL              0x1
#define TPA6130_VOLUME_AND_MUTE      0x2
#define TPA6130_OUTPUT_IMPEDANCE     0x3
#define TPA6130_I2C_ADDRESS_VERSION  0x4

/* Register bitfield macros */
// Control register
#define HP_EN_L          0x80
#define HP_EN_R          0x40
#define STEREO_HP        0x00
#define DUAL_MONO_HP     0x10
#define BRIDGE_TIED_LOAD 0x20
#define SW_SHUTDOWN      0x01
#define THERMAL          0x02
// Volume and mute register
#define MUTE_L           0x80
#define MUTE_R           0x40
// Output impedance register
#define HIZ_L            0x80
#define HIZ_R            0x40
// I2C address version register
#define VERSION          0x02

/* Maximum volume that can be set on the amplifier
 * 0x3F = 63 = 4dB, minimum is 0x0 = -59dB.
 */
#define TPA6130_MAX_VOLUME  0x3F

#define TPA6130_VOL_CNT 64
static const char* tpa_vol_to_str[TPA6130_VOL_CNT] = {
        " MUTE", "-53.5", "-50.0", "-47.5", "-45.5", "-43.9",
        "-41.4", "-39.5", "-36.5", "-35.3", "-33.3", "-31.7", "-30.4", "-28.6", "-27.1",
        "-26.3", "-24.7", "-23.7",
        "-22.5", "-21.7", "-20.5", "-19.6", "-18.8", "-17.8", "-17.0", "-16.2", "-15.2",
        "-14.5", "-13.7", "-13.0",
        "-12.3", "-11.6", "-10.9", "-10.3", "-9.7", "-9.0", "-8.5", "-7.8", "-7.2",
        "-6.7", "-6.1", "-5.6", "-5.1",
        "-4.5", "-4.1", "-3.5", "-3.1", "-2.6", "-2.1", "-1.7", "-1.2", "-0.8", "-0.3",
        "+0.1", "+0.5", "+0.9", "+1.4",
        "+1.7", "+2.1", "+2.5", "+2.9", "+3.3", "+3.6", "+4.0"
};

static uint8_t new_volume_mute = TPA6130_VOL_MIN;
static uint8_t volume_pre_mute = TPA6130_VOL_DEFAULT;
static bool tpa6130_muted = false;

void tpa6130_init_i2c(void) {
    i2c_init(TPA_I2C_PORT, 400 * 1000);
    gpio_set_function(TPA_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TPA_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TPA_I2C_SDA);
    gpio_pull_up(TPA_I2C_SCL);
}

static void write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg,  data };
    i2c_write_blocking_until(TPA_I2C_PORT, TPA_I2C_ADDR, buf, 2, false, make_timeout_time_ms(10));
}

static uint8_t read_reg(uint8_t reg) {
    uint8_t buf = 0;

    i2c_write_blocking_until(TPA_I2C_PORT, TPA_I2C_ADDR, &reg, 1, true, make_timeout_time_ms(10));
    i2c_read_blocking_until(TPA_I2C_PORT, TPA_I2C_ADDR, &buf, 1, false, make_timeout_time_ms(10));

    return buf;
}

/*! \brief Shuts down the amplifier and sets it into low power mode.
 *  This is the software low power mode described in the datasheet.
 */
void tpa6130_shutdown(void) {
    uint8_t data = read_reg(TPA6130_CONTROL);
    write_reg(TPA6130_CONTROL, data | SW_SHUTDOWN);
}

/*! \brief Powers up the amplifier from low power mode.
 */
void tpa6130_powerup(void) {
    uint8_t data = read_reg(TPA6130_CONTROL);
    write_reg(TPA6130_CONTROL, data & (~SW_SHUTDOWN));
}

/*! \brief Gets the current volume settings.
 *  \returns Current volume settings. Value is between 0 (-59dB) and
 *  63 (4dB).
 */
int8_t tpa6130_get_volume(void)
{
    return read_reg(TPA6130_VOLUME_AND_MUTE);
}

/*! \brief Gets the current muted state.
 *  \returns Current muted state.
 */
bool tpa6130_get_muted(void)
{
    return tpa6130_muted;
}

/*! \brief Sets the volume of the amplifier.
 *  Valid values are between 0 (min -59dB) and 63 (max 4dB) although
 *  the function takes care of any values higher than that by setting
 *  it to max.
 *  A volume of 0 will mute both channels. Any other value will unmute
 *  them.
 */
void tpa6130_set_volume(int8_t volume)
{
    // TODO: if moving encoder after pressing mute, restore the volume before the button press
    // int8_t new_volume = tpa6130_get_muted() ? volume_pre_mute :
    //                                           volume;
    int8_t new_volume = volume;

    if(volume > TPA6130_VOL_MAX) {
        new_volume = TPA6130_VOL_MAX;
    } else if(volume <= TPA6130_VOL_MIN) {
        // MUTE Left and Right;
        new_volume = MUTE_L|MUTE_R;
        tpa6130_muted = true;
    } else {
        volume_pre_mute = new_volume;
    }

    if(new_volume > TPA6130_VOL_MIN) {
        tpa6130_muted = false;
    }

    write_reg(TPA6130_VOLUME_AND_MUTE, new_volume);
}

void tpa6130_init(void) {
    tpa6130_init_i2c();

    uint8_t ver = read_reg(TPA6130_I2C_ADDRESS_VERSION);
    printf("Found TPA: %x\n", ver);

    //tpa6130_powerup();

    /* un-mute the output channels, the volume is still 0 and
     * should be increased by an application (fade-in/fade-out) */
    write_reg(TPA6130_VOLUME_AND_MUTE, 0x00); // TPA6130_VOLUME_AND_MUTE_DEFAULT);

    // set stereo/mono mode and enable both amplifiers
    write_reg(TPA6130_CONTROL,(HP_EN_L | HP_EN_R));

    sleep_ms(50);

    tpa6130_set_volume(TPA6130_VOL_DEFAULT);

    tpa6130_muted = false;

    printf("TPA VOL: %d\n", tpa6130_get_volume());
}

const char* tpa6130_get_volume_str(uint8_t volume) {
    if(volume >= TPA6130_VOL_CNT) {
        return "N/A";
    }

    return tpa_vol_to_str[volume];
}
