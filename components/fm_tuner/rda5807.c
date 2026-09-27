#include "rda5807.h"

#include <stddef.h>

// 02h
#define CTRL_DHIZ (1U << 15)   // audio outputs driven rather than high-Z
#define CTRL_DMUTE (1U << 14)  // 1 is "not muted"
#define CTRL_MONO (1U << 13)
#define CTRL_BASS (1U << 12)
#define CTRL_SEEKUP (1U << 9)
#define CTRL_SEEK (1U << 8)
#define CTRL_SKMODE (1U << 7)  // stop a seek at the band's edge, not wrap
#define CTRL_NEW_METHOD (1U << 2)
#define CTRL_SOFT_RESET (1U << 1)
#define CTRL_ENABLE (1U << 0)

// 03h
#define CHAN_SHIFT 6U
#define CHAN_TUNE (1U << 4)
// BAND 00 = 87-108 MHz, SPACE 00 = 100 kHz: both fields left at zero.

// 04h
#define OPT_DE_50US (1U << 11)
#define OPT_SOFTMUTE (1U << 9)

// 05h
#define VOL_INT_MODE (1U << 15)
#define VOL_SEEKTH_SHIFT 8U
/* The chip's own default: lower stops a seek on noise, higher walks past the
 * weaker stations a short wire antenna brings in. */
#define VOL_SEEKTH 8U
#define VOL_LNA_PORT_LNAP (2U << 6)

// 0Ah
#define STATUS_STC (1U << 14)
#define STATUS_SF (1U << 13)
#define STATUS_ST (1U << 10)
#define STATUS_CHAN_MASK 0x03FFU

// 0Bh
#define SIGNAL_RSSI_SHIFT 9U
#define SIGNAL_FM_TRUE (1U << 8)

uint16_t rda5807_channel_for_khz(uint32_t khz)
{
    if (khz < RDA5807_BAND_MIN_KHZ) khz = RDA5807_BAND_MIN_KHZ;
    if (khz > RDA5807_BAND_MAX_KHZ) khz = RDA5807_BAND_MAX_KHZ;
    return (uint16_t)((khz - RDA5807_BAND_MIN_KHZ + RDA5807_SPACING_KHZ / 2U) /
                      RDA5807_SPACING_KHZ);
}

uint32_t rda5807_khz_for_channel(uint16_t channel)
{
    return RDA5807_BAND_MIN_KHZ + (uint32_t)channel * RDA5807_SPACING_KHZ;
}

uint16_t rda5807_control_word(const rda5807_state_t *state, bool soft_reset, bool seek,
                              bool seek_up)
{
    uint16_t word = CTRL_DHIZ | CTRL_SKMODE | CTRL_NEW_METHOD;
    if (state != NULL) {
        if (!state->muted) word |= CTRL_DMUTE;
        if (state->mono) word |= CTRL_MONO;
        if (state->bass) word |= CTRL_BASS;
        if (state->enabled) word |= CTRL_ENABLE;
    }
    if (soft_reset) word |= CTRL_SOFT_RESET;
    if (seek) {
        word |= CTRL_SEEK;
        if (seek_up) word |= CTRL_SEEKUP;
    }
    return word;
}

uint16_t rda5807_tune_word(uint32_t khz)
{
    return (uint16_t)((rda5807_channel_for_khz(khz) << CHAN_SHIFT) | CHAN_TUNE);
}

uint16_t rda5807_options_word(void)
{
    return OPT_DE_50US | OPT_SOFTMUTE;
}

uint16_t rda5807_volume_word(uint8_t volume)
{
    if (volume > RDA5807_VOLUME_MAX) volume = RDA5807_VOLUME_MAX;
    return (uint16_t)(VOL_INT_MODE | (VOL_SEEKTH << VOL_SEEKTH_SHIFT) | VOL_LNA_PORT_LNAP | volume);
}

void rda5807_parse_status(uint16_t status, uint16_t signal, rda5807_status_t *out)
{
    if (out == NULL) return;
    out->tune_complete = (status & STATUS_STC) != 0U;
    out->seek_failed = (status & STATUS_SF) != 0U;
    out->stereo = (status & STATUS_ST) != 0U;
    out->khz = rda5807_khz_for_channel((uint16_t)(status & STATUS_CHAN_MASK));
    out->rssi = (uint8_t)(signal >> SIGNAL_RSSI_SHIFT);
    out->station = (signal & SIGNAL_FM_TRUE) != 0U;
}
