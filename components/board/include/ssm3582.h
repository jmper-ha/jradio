#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The SSM3582's registers as numbers: what to write for a state, and what a
 * read says. Pure, so the words are built and the volume curve is measured on
 * the host; putting them on the bus is board_ssm3582.c.
 *
 * The chip is an I2S Class-D amplifier with the controls of a codec: sound
 * arrives on the same three wires a PCM5102 would take, and gain, mute and
 * power are reached over I2C instead of through the samples. That is what
 * makes it worth more than the MUTE pin an analogue amplifier gets - the knob
 * then acts on the chip, and the sixteen bits the software volume throws away
 * per 6 dB stay in the music.
 *
 * Register layout follows the SSM3582/SSM3582A datasheet, as implemented by
 * the ALSA driver for the same part (github.com/rnewbie/ssm3582a_alsa):
 * 8-bit registers, one address byte, no auto-increment. */

/* The address the ADDR0/ADDR1 pins strap: any of sixteen from 0x10, and 0x10
 * is the one both pins low answers at - the usual wiring. */
#define SSM3582_I2C_ADDRESS_MIN 0x10U
#define SSM3582_I2C_ADDRESS_MAX 0x1FU
#define SSM3582_I2C_ADDRESS 0x10U

#define SSM3582_REG_VENDOR_ID 0x00U
#define SSM3582_REG_DEVICE_ID1 0x01U
#define SSM3582_REG_DEVICE_ID2 0x02U
#define SSM3582_REG_REVISION 0x03U
#define SSM3582_REG_POWER_CTRL 0x04U
#define SSM3582_REG_AMP_DAC_CTRL 0x05U
#define SSM3582_REG_DAC_CTRL 0x06U
#define SSM3582_REG_VOL_LEFT_CTRL 0x07U
#define SSM3582_REG_VOL_RIGHT_CTRL 0x08U
#define SSM3582_REG_SAI_CTRL1 0x09U
#define SSM3582_REG_SAI_CTRL2 0x0AU
#define SSM3582_REG_SLOT_LEFT_CTRL 0x0BU
#define SSM3582_REG_SLOT_RIGHT_CTRL 0x0CU
// The limiter, the clipper and the fault controls - written by the init
// table below; their bit fields are the chip's business, not ours.
#define SSM3582_REG_LIM_LEFT_CTRL1 0x0EU
#define SSM3582_REG_LIM_LEFT_CTRL2 0x0FU
#define SSM3582_REG_LIM_LEFT_CTRL3 0x10U
#define SSM3582_REG_LIM_RIGHT_CTRL1 0x11U
#define SSM3582_REG_LIM_RIGHT_CTRL2 0x12U
#define SSM3582_REG_LIM_RIGHT_CTRL3 0x13U
#define SSM3582_REG_CLIP_LEFT_CTRL 0x14U
#define SSM3582_REG_CLIP_RIGHT_CTRL 0x15U
#define SSM3582_REG_FAULT_CTRL1 0x16U
#define SSM3582_REG_FAULT_CTRL2 0x17U
#define SSM3582_REG_STATUS1 0x18U  // UVLO, thermal warn/fault
#define SSM3582_REG_STATUS2 0x19U  // clip, overcurrent, limiter per channel
#define SSM3582_REG_SOFT_RESET 0x1CU

/* What the three identity registers answer on every chip of the part. A board
 * whose amplifier does not say this has something else on its address. */
#define SSM3582_VENDOR_ID 0x41U
#define SSM3582_DEVICE_ID1 0x35U
#define SSM3582_DEVICE_ID2 0x82U

/* 04h, POWER_CTRL, as the proven init writes it: 0x80 - auto power-down
 * enabled, so the chip rests whenever the I2S clock stops (which is what
 * happens between tracks) - and no channel power-down bit set. SPWDN is the
 * driver's own power-down on top of that word. */
#define SSM3582_POWER_CTRL_APWDN_EN 0x80U
#define SSM3582_POWER_CTRL_MASTER_PWDN 0x01U
#define SSM3582_POWER_CTRL_INIT 0x80U

/* 06h, DAC_CTRL, as the proven init writes it: 0x03 - the chip's own default
 * rate family, no high-pass, and both mute bits clear. The driver only ever
 * touches the mute bits on top of this word. */
#define SSM3582_DAC_CTRL_MUTE_RIGHT 0x40U
#define SSM3582_DAC_CTRL_MUTE_LEFT 0x20U
#define SSM3582_DAC_CTRL_INIT 0x03U

/* Volume, 07h and 08h: eight bits of attenuation, 0 dB at 0x00 and 0.375 dB
 * per code down. The knob's own curve - the one the software volume used to
 * apply to the samples - is laid over that in decibels, so a setting sounds
 * the same as it did on the PCM5102: 100 is 0 dB, 1 is -60 dB. 0 is not a
 * code here: exact silence is the mute bits, and the driver sets both. */
#define SSM3582_VOLUME_MAX_PERCENT 100U
#define SSM3582_VOLUME_SILENT 0xFFU  // the quietest code the driver writes

/* The proven initialization as data: `SSM3582_INIT_WRITES` register/value
 * pairs, in the order they are written. The driver walks it once at init and
 * repair() checks the chip against it, so there is one copy of the sequence,
 * not two. */
typedef struct {
    uint8_t reg;
    uint8_t value;
} ssm3582_reg_write_t;

#define SSM3582_INIT_WRITES 17U
extern const ssm3582_reg_write_t ssm3582_init_writes[SSM3582_INIT_WRITES];

/* 04h for the state: the init's word, with SPWDN added only when the driver
 * takes the chip down. */
uint8_t ssm3582_power_ctrl_word(bool powered);

/* Whether the three identity registers are the part's. */
bool ssm3582_identity_matches(uint8_t vendor, uint8_t device1, uint8_t device2);

/* Whether the chip's mute bits should be set, from the two things that decide
 * it - the pure half of the driver, so the rule is tested on the host the way
 * board_amplifier_should_play() is.
 *
 * `gate_closed` - the pause, or a sound going to a Bluetooth speaker: the
 *   amplifier has nothing to pass whatever the knob says;
 * `percent` - the knob; zero is the off position and is exact silence.
 * Either silences; only an open gate and a knob above zero play. */
bool ssm3582_should_mute(bool gate_closed, uint8_t percent);

/* The attenuation code for a knob setting, 0..100. Interpolated between the
 * same breakpoints the software volume used, in decibels: 100 -> 0x00,
 * clamped above the range, and SSM3582_VOLUME_SILENT at 0. */
uint8_t ssm3582_volume_register(uint8_t percent);

#ifdef __cplusplus
}
#endif