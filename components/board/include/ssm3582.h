#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The SSM3582's registers as numbers: what to write for a state. Pure, so the
 * words are checked on the host; putting them on the bus is board_ssm3582.c.
 *
 * Every value here is from the datasheet (Rev. A, register details, pages
 * 39-50), not from a driver: the ALSA driver the first version leaned on has
 * MONO the wrong way round in its comments, and the register table that
 * version wrote put the chip in TDM mode and in the 64-96 kHz band whatever
 * the stream was (PR #3, 2026-10-21).
 *
 * What the firmware asks of the chip is little: the stereo I2S its slots are
 * sent in, the rate band, the analogue gain, and its own power-down as the
 * mute. Its digital volume stays at 0 dB - the volume, the tone and the
 * levelling are the samples', as on every other DAC, and the headroom they
 * keep is counted against a chip that passes the samples as they are. */

/* The addresses its two ADDRx pins strap, five levels each (table 16). */
#define SSM3582_I2C_ADDRESS_MIN 0x10U
#define SSM3582_I2C_ADDRESS_MAX 0x1FU

#define SSM3582_REG_VENDOR_ID 0x00U
#define SSM3582_REG_DEVICE_ID1 0x01U
#define SSM3582_REG_DEVICE_ID2 0x02U
#define SSM3582_REG_POWER_CTRL 0x04U
#define SSM3582_REG_AMP_DAC_CTRL 0x05U
#define SSM3582_REG_DAC_CTRL 0x06U
#define SSM3582_REG_VOL_LEFT_CTRL 0x07U
#define SSM3582_REG_SAI_CTRL1 0x09U
#define SSM3582_REG_SAI_CTRL2 0x0AU
#define SSM3582_REG_STATUS1 0x18U
#define SSM3582_REG_STATUS2 0x19U
#define SSM3582_REG_SOFT_RESET 0x1CU

#define SSM3582_VENDOR_ID 0x41U
#define SSM3582_DEVICE_ID1 0x35U
#define SSM3582_DEVICE_ID2 0x82U

// SOFT_RESET's S_RST: back to the reset values of every register.
#define SSM3582_SOFT_RESET_WORD 0x01U

/* VOL_L at 0 dB - 0x40 of 0x00 (+24 dB) to 0xFE (-71.25 dB), 0.375 dB a
 * step. VOL_LINK is on at reset, so the right channel follows the left. */
#define SSM3582_VOLUME_0DB 0x40U

/* SAI_CTRL1 for stereo I2S: SAI_MODE 0 (stereo, not the reset's TDM),
 * SDATA_FMT 0 (I2S, data one BCLK after the edge), FSYNC_MODE 0 (low is the
 * left channel), BCLK_POL 0 (captured on the rising edge) - the Philips
 * timing the S3 sends every DAC. The slot width bits are TDM's alone. */
#define SSM3582_SAI_CTRL1_I2S 0x00U

/* Whether this rate can be sent at 64 x Fs, the widest a standard-mode slot
 * pair on the S3 makes: table 13 lists 64 from 32 kHz up; 16-24 kHz wants
 * 100 and 8-12 kHz 200. */
bool ssm3582_rate_takes_64fs(uint32_t sample_rate);

/* How many times over a stream at this rate is sent so that the chip gets
 * one it can take: 1 from 32 kHz up, 2 for 16-24 kHz (22.05 goes out at
 * 44.1), 4 below 16 kHz (11.025 at 44.1, 8 at 32). The bus then runs at the
 * multiplied rate and the samples are filled in between - see
 * audio_pcm_upsample_s16(). The S3 has no wider clock to give the low rates,
 * and they were silent (nikkov's test, 2026-10-31). */
uint32_t ssm3582_rate_factor(uint32_t sample_rate);

/* DAC_CTRL for a rate: DAC_FS, the band the rate is in, with every other bit
 * at its reset value - soft volume ramp, no high-pass, both channels on.
 * 0xFF for a rate in none of the bands. */
uint8_t ssm3582_dac_ctrl_word(uint32_t sample_rate);

/* POWER_CTRL: the reset word - automatic power-down on silence, the
 * temperature sensor off, stereo - with SPWDN as asked. The datasheet's
 * click-free stop is SPWDN set before the clock goes, which is what the
 * firmware's mute does. */
uint8_t ssm3582_power_ctrl_word(bool powered);

/* AMP_DAC_CTRL: the reset word - DAC low-power mode, low-EMI edges, both
 * channels the right way up - with ANA_GAIN set to `gain_code` (0..3 for
 * 13, 16, 19, 21 dB). A code past 3 is taken as 0, the quietest. */
uint8_t ssm3582_amp_dac_ctrl_word(uint8_t gain_code);

// What the three identity registers answer on every chip of the part.
bool ssm3582_identity_matches(uint8_t vendor, uint8_t device1, uint8_t device2);
