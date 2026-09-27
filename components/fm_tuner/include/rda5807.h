#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The RDA5807's registers as numbers: what to write for a state, and what a
 * status read says. Pure, so the bit layout is tested on the host; putting
 * the words on the bus is fm_tuner.c.
 *
 * The whole family - the M module on the bench, the FP with I2S later -
 * shares 02h-05h and the status words. The FP adds its I2S setup in 04h and
 * 06h, which is not here yet. */

// Random access: the register's address, then its 16 bits, high byte first.
#define RDA5807_I2C_ADDRESS 0x11U

#define RDA5807_REG_CHIP_ID 0x00U
#define RDA5807_REG_CONTROL 0x02U
#define RDA5807_REG_CHANNEL 0x03U
#define RDA5807_REG_OPTIONS 0x04U
#define RDA5807_REG_VOLUME 0x05U
#define RDA5807_REG_STATUS 0x0AU
#define RDA5807_REG_SIGNAL 0x0BU

// The high byte of 00h on every chip of the family.
#define RDA5807_CHIP_ID_FAMILY 0x58U

/* The band is fixed at 87-108 MHz - the one Russia and Europe listen on - and
 * the spacing at 100 kHz, which every station on it sits on. */
#define RDA5807_BAND_MIN_KHZ 87000U
#define RDA5807_BAND_MAX_KHZ 108000U
#define RDA5807_SPACING_KHZ 100U
#define RDA5807_VOLUME_MAX 15U

typedef struct {
    bool enabled;
    bool muted;
    bool mono;
    bool bass;
    uint8_t volume;  // 0..RDA5807_VOLUME_MAX; 0 is quiet, not silent
} rda5807_state_t;

typedef struct {
    bool tune_complete;  // STC: the last tune or seek has finished
    bool seek_failed;    // SF: a seek went round the band and found nothing
    bool stereo;
    bool station;        // FM_TRUE: what is on the channel is a station
    uint8_t rssi;        // 0..127, logarithmic
    uint32_t khz;        // the channel the chip is on now
} rda5807_status_t;

/* The nearest channel to `khz`, clamped to the band. */
uint16_t rda5807_channel_for_khz(uint32_t khz);
uint32_t rda5807_khz_for_channel(uint16_t channel);

/* 02h: power, mute, mono, bass, and the seek bits. `soft_reset` is written
 * once, at power-up; `seek` starts a seek in the direction `seek_up`. The
 * clock is always the 32.768 kHz crystal (CLK_MODE 000). */
uint16_t rda5807_control_word(const rda5807_state_t *state, bool soft_reset, bool seek,
                              bool seek_up);

/* 03h with TUNE set: go to `khz`. */
uint16_t rda5807_tune_word(uint32_t khz);

/* 04h: 50 us de-emphasis, the curve European and Russian stations are sent
 * with; the chip's default is the American 75 us, which sounds dull here. */
uint16_t rda5807_options_word(void);

/* 05h: the seek threshold, the antenna input and the volume. */
uint16_t rda5807_volume_word(uint8_t volume);

/* 0Ah and 0Bh, as read. */
void rda5807_parse_status(uint16_t status, uint16_t signal, rda5807_status_t *out);

#ifdef __cplusplus
}
#endif
