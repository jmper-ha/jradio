#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The SSM3582 class-D amplifier: its registers and their encodings.

   The part is a DAC and a bridge-tied class-D output in one package -
   two channels, 18 W each into 4 ohms at 20 V - that takes samples over
   the same Philips I2S timing the PCM5102 takes, and is controlled over
   I2C. That control is why this file exists: the chip has a volume
   register of its own (soft-ramped, 0.375 dB a step, mute as a value),
   powers up and down without a click under register control, and needs
   to be told which sample-rate band its BCLK belongs to. A board built
   on it keeps its listening level and its silence in the chip instead of
   in the samples, so the bits the decoder produced reach the output
   whole.

   The chip comes out of reset in TDM mode with 32-bit slots; the format
   this firmware drives - plain stereo, 16-bit samples left-aligned in a
   32-bit slot - is what the SAI_CTRL writes below set. The chip is a
   slave of the bus like every codec here; it provides no clocks.

   Pure: registers and conversions, no bus, tested on the host. The I2C
   side is ssm3582.c; where its calls come from in the audio path is
   board.c. */

/* Seven-bit address with both ADDR pins at ground - the chip's own default.
 * Its two ADDR pins are sampled at power-up and choose one of sixteen
 * addresses, 0x10…0x1F, from five levels each. A board that wires them
 * elsewhere names the address in board_options.h's AUDIO_DAC_I2C_ADDRESS
 * instead, and this default stands aside; the bus code reads it, pure
 * logic here does not. */
#define SSM3582_I2C_ADDRESS_DEFAULT 0x10U

/* Read once at setup to see whether a chip answers. The content is a
 * factory scratch register, of no use to us; the transaction itself is
 * the presence check. */
#define SSM3582_REG_SSA_ID 0x00U

#define SSM3582_REG_POWER_CTRL 0x04U
/* SPWDN under register control is the datasheet's pop-free way to close
 * the output, and the reason the firmware does it before it lets the
 * clock stop. APWDN_EN keeps the chip's own standby after 2048 zero
 * samples - free power saving for the silence between sources. Nothing
 * reads the temperature sensor, so it stays down. */
#define SSM3582_POWER_APWDN_EN 0x80U
#define SSM3582_POWER_TEMP_PWDN 0x20U
#define SSM3582_POWER_SPWDN 0x01U
#define SSM3582_POWER_DOWN (SSM3582_POWER_APWDN_EN | SSM3582_POWER_TEMP_PWDN | \
                            SSM3582_POWER_SPWDN)
#define SSM3582_POWER_UP (SSM3582_POWER_APWDN_EN | SSM3582_POWER_TEMP_PWDN)

#define SSM3582_REG_AMP_DAC_CTRL 0x05U
#define SSM3582_AMP_DAC_CTRL_LOW_PWR 0x80U
#define SSM3582_AMP_DAC_CTRL_13DB 0x00U
#define SSM3582_AMP_DAC_CTRL_16DB 0x01U
#define SSM3582_AMP_DAC_CTRL_19DB 0x02U
#define SSM3582_AMP_DAC_CTRL_21DB 0x03U

#define SSM3582_REG_DAC_CTRL 0x06U
/* The sample-rate field, as dac_fs_bits() gives it. The other bits stay
 * at their reset values: soft volume ramping (DAC_HV clear), both
 * channels un-muted, the DC filter off. */
#define SSM3582_DAC_CTRL_FS_BITS 0x07U
/* What dac_fs_bits answers for a rate the chip has no band for. */
#define SSM3582_DAC_FS_NONE 0xFFU

#define SSM3582_REG_VOL_LEFT 0x07U
/* 0.375 dB a step: 0x40 is unity, and every step up from it is quieter.
 * VOL_LINK (SAI_CTRL2) keeps the right channel on this one register, so
 * the left is the only one written. The whole ladder runs 0x00 = +24 dB,
 * 0x40 = 0 dB (the reset value), 0xFE = −71.25 dB, and 0xFF = mute —
 * which is why 0xFE is the bottom every computed value has to stop at:
 * the mute is a setting of its own, not one more rung. */
#define SSM3582_VOL_UNITY 0x40U
#define SSM3582_VOL_LOUDEST 0x00U
#define SSM3582_VOL_QUIETEST 0xFEU
#define SSM3582_VOL_MUTE 0xFFU

/* The ends of that ladder in whole dB, for a board which names its loudest
 * setting in whole dB too (board_options.h's AUDIO_DAC_MAX_VOLUME_DB). A
 * whole dB is 8/3 steps, so anything written in those units lands on the
 * rung nearest it. */
#define SSM3582_VOL_DB_LOUDEST 24
#define SSM3582_VOL_DB_QUIETEST (-71)

#define SSM3582_REG_SAI_CTRL1 0x09U
/* Stereo mode (the reset value selects TDM), I2S with data delayed one
 * BCLK - Philips, the timing the S3 drives - left channel on the low
 * half of word-select, data taken on the rising edge of BCLK. */
#define SSM3582_SAI_CTRL1_STEREO_I2S 0x00U

#define SSM3582_REG_SAI_CTRL2 0x0AU
/* Read 16-bit samples from each slot, keep both channels on the left
 * volume register and both clippers on the left one, leave the volume
 * changes immediate (the soft ramp is the controller's own business),
 * and keep AUTO_SLOT as reset. */
#define SSM3582_SAI_CTRL2_16_BIT_LINKED 0x17U

/* The clock geometry the firmware drives into the part: two 32-bit slots
 * with a 16-bit sample at the top of each, so BCLK is 64 x Fs. The slots
 * cannot be the 16 bits the PCM5102 gets: the chip wants a BCLK no
 * shorter than 2.048 MHz, and 32 x Fs is under that at every rate this
 * radio plays. */
#define SSM3582_BCLK_RATIO 64U

/* The DAC_CTRL field for a sample rate, or SSM3582_DAC_FS_NONE outside
 * the chip's bands. The bands are the datasheet's own; 48 kHz belongs to
 * two of them and takes the lower, as the chip's oversampling ratios
 * allow both. A rate between 12 kHz and 16 kHz has no band at all -
 * there is no field value that names it. */
uint8_t ssm3582_dac_fs_bits(uint32_t sample_rate);

/* Whether the chip takes `sample_rate` with `ratio` BCLKs per frame.
 * Three rules, none implied by the others: the ratio is one of the ones
 * the chip lists (50 to 4096, Table 13); the pair falls inside one of
 * the chip's bands, which give each rate its own span of ratios; and the
 * clock itself is between the 2.048 MHz its internals need and the
 * 49.152 MHz its pins take. 32 kHz at 50 x Fs is in the table and still
 * fails the floor - which is why all three are checked. */
bool ssm3582_bclk_rate_ok(uint32_t sample_rate, uint32_t ratio);

/* The chip's loudest allowed setting, in whole dB above unity, as the
   volume register. board_options.h's AUDIO_DAC_MAX_VOLUME_DB is written in
   those units and this is where it turns into a register: 0 dB is the
   chip's reset value and the top of the curve every other board has,
   positive values are the digital boost the chip offers up to +24 dB, and
   negative ones lower the ceiling instead. Outside the ladder the answer
   is the nearest end of it; the mute, which is not a rung, is never
   returned. */
uint8_t ssm3582_db_to_volume_register(int max_db);

/* The listening level as the volume register, 0..100 percent.

   The same curve as audio_volume.c - the same breakpoints at the same
   percentages - stepped on the chip's 0.375 dB ladder instead of on
   sample bits, so a setting means the same loudness either way and
   nobody has to relearn the knob when the board changes. 0 is the
   chip's own mute rather than a zeroed multiplier. Between breakpoints
   the steps are interpolated, as the digital curve interpolates its
   gains.

   `max_register` is where 100 percent lands, from db_to_volume_register()
   above: the curve is a number of steps below that ceiling, so a board
   whose loudest setting is a +12 dB boost keeps every interval between
   two settings the curve has always had. Steps falling off the bottom of
   the ladder stop at its quietest rung rather than reaching the mute. */
uint8_t ssm3582_volume_register(uint8_t percent, uint8_t max_register);