/*
 * tpa6130.c
 *
 *  Created on: 31 Jul 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define TPA_I2C_PORT i2c0
#define TPA_I2C_SDA 12
#define TPA_I2C_SCL 13
#define TPA_I2C_ADDR 0x60

/* Set TPA6130_MODE to one of the following selections
 * TPA6130_MODE_STEREO, left and right channels are connected to DAC
 * TPA6130_MODE_DUAL_MONO, left channel is connected but this signal is
 * used to feed both amplifiers for right and left channel output
 * TPA6130_MONO_L, only left channel is used with the DAC
 * TPA6130_MONO_R, only right channel is used with the DAC
 */
#define TPA6130_MODE_STEREO 0
#define TPA6130_MODE_MONO 1
/*
#define TPA6130_MODE_MONO_L 2
#define TPA6130_MODE_MONO_R 3
 */


/* TWI slave address of the amplifier
 * This device has a fixed slave address.
 * -> No hardware option to change it
 */
#define TPA6130_TWI_ADDRESS 0x60


/*! \name Volume Control
 */
//! @{

#define TPA6130_VOL_MIN       ((int8_t)(  0)) // -59dB
#define TPA6130_VOL_MAX       ((int8_t)( 63)) // : +4dB

//! @}

/* Register map
 * Use these definitions with the register write/read functions
 */
#define TPA6130_CONTROL              0x1
#define TPA6130_VOLUME_AND_MUTE      0x2
#define TPA6130_OUTPUT_IMPEDANCE     0x3
#define TPA6130_I2C_ADDRESS_VERSION  0x4

/* Default register values after a reset */
#define TPA6130_CONTROL_DEFAULT             0x00
//#define TPA6130_VOLUME_AND_MUTE_DEFAULT   0xC0
#define TPA6130_VOLUME_AND_MUTE_DEFAULT     0x0F
#define TPA6130_OUTPUT_IMPEDANCE_DEFAULT    0x00
#define TPA6130_I2C_ADDRESS_VERSION_DEFAULT 0x02
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

void tpa6130_init_i2c(void) {
    i2c_init(TPA_I2C_PORT, 100 * 1000);
    gpio_set_function(TPA_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TPA_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TPA_I2C_SDA);
    gpio_pull_up(TPA_I2C_SCL);
}

static void write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg,  data };
    i2c_write_blocking_until(TPA_I2C_PORT, TPA_I2C_ADDR, buf, 2, false, make_timeout_time_ms(100));
}

static uint8_t read_reg(uint8_t reg) {
    uint8_t buf = 0;

    i2c_write_blocking_until(TPA_I2C_PORT, TPA_I2C_ADDR, &reg, 1, true, make_timeout_time_ms(100));
    i2c_read_blocking_until(TPA_I2C_PORT, TPA_I2C_ADDR, &buf, 1, false, make_timeout_time_ms(100));

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

/*! \brief Sets the volume of the amplifier.
 *  Valid values are between 0 (min -59dB) and 63 (max 4dB) although
 *  the function takes care of any values higher than that by setting
 *  it to max.
 *  A volume of 0 will mute both channels. Any other value will unmute
 *  them.
 */
void tpa6130_set_volume(int8_t volume)
{
    int8_t new_volume = volume;

    if(volume > TPA6130_VOL_MAX)
    {
        new_volume = TPA6130_VOL_MAX;
    }
    else if(volume <= TPA6130_VOL_MIN )
    {
        // MUTE Left and Right;
        new_volume = MUTE_L|MUTE_R;
    }

    write_reg(TPA6130_VOLUME_AND_MUTE, new_volume);
}

/*! \brief Gets the current volume settings.
 *  \returns Current volume settings. Value is between 0 (-59dB) and
 *  63 (4dB).
 */
int8_t tpa6130_get_volume(void)
{
    return read_reg(TPA6130_VOLUME_AND_MUTE);
}


/*! \brief Returns the current volume of the DAC.
 *         The volume is in the range 0 - 255
 */
uint8_t tpa6130_dac_get_volume(void)
{
    // return volume is num display step for LCD
    //  volume scale is between 10 and 245
    // 0 is -100db
    // 245 is max volume
    uint16_t raw_volume;
    raw_volume = (tpa6130_get_volume() & (~(MUTE_L | MUTE_R)));
    return (uint8_t) ((raw_volume * 255) / TPA6130_VOL_MAX);
}

/*! \brief Set the volume of the DAC.
 */
void tpa6130_dac_set_volume(uint8_t volume)
{
    tpa6130_set_volume(volume);
}

/*! \brief Increases the output volume of the amplifier by one step.
 * Stops at the maximum volume and thus does not wrap to the
 * lowest volume.
 */
void tpa6130_dac_increase_volume(void)
{
    int8_t volume = tpa6130_get_volume()& (~(MUTE_L | MUTE_R));
    if( volume < TPA6130_VOL_MIN )
        volume = TPA6130_VOL_MIN;
    tpa6130_set_volume(volume+1);
}

/*! \brief Decreases the output volume of the amplifier by one step.
 *
 * Stops at the lowest possible volume.
 */
void tpa6130_dac_decrease_volume(void)
{
    int8_t volume = tpa6130_get_volume()& (~(MUTE_L | MUTE_R));
    if( volume > TPA6130_VOL_MIN )
        --volume;
    tpa6130_set_volume( volume );
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

    tpa6130_set_volume(10);

    printf("TPA VOL: %d\n", tpa6130_get_volume());
}
