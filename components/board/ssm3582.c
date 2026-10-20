#include "ssm3582.h"

#include <stdbool.h>
#include <stddef.h>

/* Breakpoints of the knob's curve as attenuation codes: setting, and the code
 * that is worth its decibels at 0.375 dB each. The decibels are the ones
 * audio_volume.c used to apply to the samples (100 is 0 dB, every ten per
 * cent down to 30 worth 4 dB, then the curve steepens to -60 dB at 1), so a
 * setting sounds the same whether the chip attenuates or the samples were
 * scaled. Interpolated linearly between them, which in code space is
 * linearly in decibels - codes are dB. Integer only, as everywhere in the
 * audio path. */
typedef struct {
    uint8_t percent;
    uint8_t code;
} ssm3582_volume_point_t;

#if 1
static const ssm3582_volume_point_t s_volume_curve[] = {
    {100U, 44U},    /*    0 dB */
    {90U, 54U},    /*   +4 dB */
    {80U, 64U},    /*   0 dB */
    {70U, 74U},    /*  -12 dB */
    {60U, 84U},    /*  -16 dB */
    {50U, 94U},    /*  -20 dB */
    {40U, 104U},    /*  -24 dB */
    {30U, 114U},    /*  -28 dB */
    {20U, 124U},    /*  -36 dB */
    {15U, 134U},   /*  -41 dB */
    {10U, 154U},   /*  -46 dB */
    {5U, 164U},    /*  -53 dB */
    {1U, 174U},    /*  -60 dB */
};
#else
static const ssm3582_volume_point_t s_volume_curve[] = {
    {100U, 0U},    /*    0 dB */
    {90U, 11U},    /*   -4 dB */
    {80U, 21U},    /*   -8 dB */
    {70U, 32U},    /*  -12 dB */
    {60U, 43U},    /*  -16 dB */
    {50U, 53U},    /*  -20 dB */
    {40U, 64U},    /*  -24 dB */
    {30U, 75U},    /*  -28 dB */
    {20U, 96U},    /*  -36 dB */
    {15U, 109U},   /*  -41 dB */
    {10U, 123U},   /*  -46 dB */
    {5U, 141U},    /*  -53 dB */
    {1U, 160U},    /*  -60 dB */
};
#endif
uint8_t ssm3582_volume_register(uint8_t percent)
{
    if (percent == 0U) return SSM3582_VOLUME_SILENT;
    if (percent >= SSM3582_VOLUME_MAX_PERCENT) return 0U;

    const size_t count = sizeof(s_volume_curve) / sizeof(s_volume_curve[0]);
    for (size_t index = 1U; index < count; ++index) {
        if (percent > s_volume_curve[index].percent) {
            const ssm3582_volume_point_t high = s_volume_curve[index - 1U];
            const ssm3582_volume_point_t low = s_volume_curve[index];
            const uint32_t span = (uint32_t)(high.percent - low.percent);
            /* Down from the louder breakpoint, so a setting that lands on a
             * breakpoint exactly is that breakpoint's code and not the next
             * one's end. */
            const uint32_t into = (uint32_t)(high.percent - percent);
            const uint32_t range = (uint32_t)(low.code - high.code);
            return (uint8_t)(high.code + (into * range) / span);
        }
    }
    return s_volume_curve[count - 1U].code;
}

/* The proven initialization, one entry per register, in the order it was
 * written on the bench: the serial port and slots first, then the limiter,
 * the clipper and the fault controls. Pure, so the values themselves are
 * held by a host test - a later refactor cannot quietly "improve" them.
 *
 * 04h power: auto power-down on, nothing held down.
 * 05h amp/DAC: the bench's word.
 * 06h DAC: 0x03 - the rate family, no high-pass, both channels passing.
 * 09h/0Ah SAI: SAI mode on, 16-bit data, both channels linked, slots auto.
 * 0Bh/0Ch: left channel on slot 0, right on slot 1 - the two halves of a
 * stereo I2S frame, which is what the S3 sends. */
const ssm3582_reg_write_t ssm3582_init_writes[SSM3582_INIT_WRITES] = {
    {SSM3582_REG_POWER_CTRL, 0x80U},          // POWER_CTRL
    {SSM3582_REG_AMP_DAC_CTRL, 0x80U},        // AMP_DAC_CTRL
    {SSM3582_REG_DAC_CTRL, 0x03U},            // DAC_CTRL
    {SSM3582_REG_SAI_CTRL1, 0x01U},           // SAI_CTRL1
    {SSM3582_REG_SAI_CTRL2, 0x17U},           // SAI_CTRL2
    {SSM3582_REG_SLOT_LEFT_CTRL, 0x00U},      // SLOT_LEFT_CTRL
    {SSM3582_REG_SLOT_RIGHT_CTRL, 0x01U},     // SLOT_RIGHT_CTRL
    {SSM3582_REG_LIM_LEFT_CTRL1, 0xA0U},      // LIM_LEFT_CTRL1
    {SSM3582_REG_LIM_LEFT_CTRL2, 0xF9U},      // LIM_LEFT_CTRL2
    {SSM3582_REG_LIM_LEFT_CTRL3, 0x22U},      // LIM_LEFT_CTRL3
    {SSM3582_REG_LIM_RIGHT_CTRL1, 0xA8U},     // LIM_RIGHT_CTRL1
    {SSM3582_REG_LIM_RIGHT_CTRL2, 0xF9U},     // LIM_RIGHT_CTRL2
    {SSM3582_REG_LIM_RIGHT_CTRL3, 0x22U},     // LIM_RIGHT_CTRL3
    {SSM3582_REG_CLIP_LEFT_CTRL, 0xFFU},      // CLIP_LEFT_CTRL
    {SSM3582_REG_CLIP_RIGHT_CTRL, 0xFFU},     // CLIP_RIGHT_CTRL
    {SSM3582_REG_FAULT_CTRL1, 0x00U},         // FAULT_CTRL1
    {SSM3582_REG_FAULT_CTRL2, 0x30U},         // FAULT_CTRL2
};

uint8_t ssm3582_power_ctrl_word(bool powered)
{
    // The init's word either way; SPWDN only when the driver takes it down.
    return powered ? SSM3582_POWER_CTRL_INIT
                   : (uint8_t)(SSM3582_POWER_CTRL_INIT | SSM3582_POWER_CTRL_MASTER_PWDN);
}

bool ssm3582_identity_matches(uint8_t vendor, uint8_t device1, uint8_t device2)
{
    return vendor == SSM3582_VENDOR_ID && device1 == SSM3582_DEVICE_ID1 &&
           device2 == SSM3582_DEVICE_ID2;
}

bool ssm3582_should_mute(bool gate_closed, uint8_t percent)
{
    // Either one silences; only an open gate and a knob above zero play.
    return gate_closed || percent == 0U;
}