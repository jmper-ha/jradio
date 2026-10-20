#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ssm3582.h"

/* The chip identifies itself out of three registers, and a board whose
 * amplifier says anything else has something else on the address. */
static void test_the_chip_answers_with_its_own_ids(void)
{
    assert(ssm3582_identity_matches(SSM3582_VENDOR_ID, SSM3582_DEVICE_ID1, SSM3582_DEVICE_ID2));
    assert(!ssm3582_identity_matches(0x00U, SSM3582_DEVICE_ID1, SSM3582_DEVICE_ID2));
    assert(!ssm3582_identity_matches(SSM3582_VENDOR_ID, 0x00U, SSM3582_DEVICE_ID2));
    assert(!ssm3582_identity_matches(SSM3582_VENDOR_ID, SSM3582_DEVICE_ID1, 0x00U));
}

/* Full volume is the top of the range: 0 dB of attenuation, which is 0x00 -
 * the register attenuates, it does not gain. */
static void test_full_volume_is_no_attenuation(void)
{
    assert(ssm3582_volume_register(100U) == 0x00U);
    /* Above the range clamps rather than wrapping into a louder setting. */
    assert(ssm3582_volume_register(200U) == 0x00U);
}

/* Zero is the knob's off position, and it is silence in the mute bits rather
 * than the bottom of the attenuation range - the code is still written so a
 * mute bit a spoiled write dropped does not leave the chip loud. */
static void test_zero_volume_is_silence(void)
{
    assert(ssm3582_volume_register(0U) == SSM3582_VOLUME_SILENT);
}

/* The curve is the one the software volume applied to the samples, in the
 * same decibels: 100 is 0 dB, every ten per cent down to 30 is worth 4 dB,
 * and 1 is -60 dB at 0.375 dB per code (160 codes). */
static void test_the_curve_is_the_decibels_the_software_volume_used(void)
{
    assert(ssm3582_volume_register(1U) == 160U);   /* -60 dB */
    assert(ssm3582_volume_register(5U) == 141U);   /* -53 dB */
    assert(ssm3582_volume_register(10U) == 123U);  /* -46 dB */
    assert(ssm3582_volume_register(30U) == 75U);   /* -28 dB */
    assert(ssm3582_volume_register(50U) == 53U);   /* -20 dB */
    assert(ssm3582_volume_register(80U) == 21U);   /*  -8 dB */
}

/* Turning the knob down must always reach quieter, never louder - the knob's
 * whole promise, over every span of the curve including the steep one. */
static void test_the_curve_only_falls(void)
{
    uint8_t previous = ssm3582_volume_register(100U);
    for (int percent = 99; percent >= 1; --percent) {
        const uint8_t code = ssm3582_volume_register((uint8_t)percent);
        assert(code >= previous);
        previous = code;
    }
}

static void test_between_breakpoints_is_interpolated(void)
{
    /* Halfway between 100 (0) and 90 (11): five codes, not eleven. */
    assert(ssm3582_volume_register(95U) == 5U);
    /* And between 20 (96) and 15 (109), in the steep part: three fifths of
     * thirteen codes is seven, so 103. */
    assert(ssm3582_volume_register(17U) == 103U);
}

/* The serial port word is what the board already clocks: I2S, 16-bit slots,
 * and the rate family the chip decodes against. */
static void test_the_dac_word_mutes_both_channels_and_keeps_the_rate(void)
{
    const uint8_t playing = ssm3582_dac_ctrl_word(false, 44100U);
    assert((playing & (SSM3582_DAC_CTRL_MUTE_LEFT | SSM3582_DAC_CTRL_MUTE_RIGHT)) == 0U);
    assert((playing & SSM3582_DAC_CTRL_FS_MASK) == SSM3582_DAC_CTRL_FS_32K_48K);
    assert((playing & SSM3582_DAC_CTRL_HPF) != 0U);

    const uint8_t muted = ssm3582_dac_ctrl_word(true, 44100U);
    assert((muted & SSM3582_DAC_CTRL_MUTE_LEFT) != 0U);
    assert((muted & SSM3582_DAC_CTRL_MUTE_RIGHT) != 0U);
}

/* The chip's rate families are wide: the board's 44.1 kHz sits in 32-48, and
 * an unusual source asks for the family that holds it. */
static void test_the_rate_family_follows_the_clock(void)
{
    assert((ssm3582_dac_ctrl_word(false, 8000U) & SSM3582_DAC_CTRL_FS_MASK) ==
           SSM3582_DAC_CTRL_FS_8K_12K);
    assert((ssm3582_dac_ctrl_word(false, 22050U) & SSM3582_DAC_CTRL_FS_MASK) ==
           SSM3582_DAC_CTRL_FS_16K_24K);
    assert((ssm3582_dac_ctrl_word(false, 48000U) & SSM3582_DAC_CTRL_FS_MASK) ==
           SSM3582_DAC_CTRL_FS_32K_48K);
    assert((ssm3582_dac_ctrl_word(false, 96000U) & SSM3582_DAC_CTRL_FS_MASK) ==
           SSM3582_DAC_CTRL_FS_64K_96K);
    assert((ssm3582_dac_ctrl_word(false, 192000U) & SSM3582_DAC_CTRL_FS_MASK) ==
           SSM3582_DAC_CTRL_FS_128K_192K);
    /* An unknown rate keeps the board's family rather than muting it. */
    assert((ssm3582_dac_ctrl_word(false, 0U) & SSM3582_DAC_CTRL_FS_MASK) ==
           SSM3582_DAC_CTRL_FS_32K_48K);
}

/* Stereo either way, and the master power-down only when the chip is being
 * put down. */
static void test_the_power_word_is_stereo_and_power_down(void)
{
    assert(ssm3582_power_ctrl_word(true) == SSM3582_POWER_CTRL_STEREO);
    assert(ssm3582_power_ctrl_word(false) ==
           (SSM3582_POWER_CTRL_STEREO | SSM3582_POWER_CTRL_MASTER_PWDN));
}

/* The mute is the two inputs combined: the gate (pause / Bluetooth) and the
 * knob's zero. Either silences on its own; neither does anything to the other
 * - the rule a pause ending must not undo. */
static void test_the_gate_and_the_knob_each_silence_on_their_own(void)
{
    assert(ssm3582_should_mute(true, 100U) == true);   // paused at full volume
    assert(ssm3582_should_mute(true, 0U) == true);     // paused and off
    assert(ssm3582_should_mute(false, 0U) == true);    // playing, knob off
    assert(ssm3582_should_mute(false, 1U) == false);   // the quietest that plays
    assert(ssm3582_should_mute(false, 100U) == false); // wide open
}

int main(void)
{
    test_the_chip_answers_with_its_own_ids();
    test_full_volume_is_no_attenuation();
    test_zero_volume_is_silence();
    test_the_curve_is_the_decibels_the_software_volume_used();
    test_the_curve_only_falls();
    test_between_breakpoints_is_interpolated();
    test_the_dac_word_mutes_both_channels_and_keeps_the_rate();
    test_the_rate_family_follows_the_clock();
    test_the_power_word_is_stereo_and_power_down();
    test_the_gate_and_the_knob_each_silence_on_their_own();
    puts("ssm3582 tests passed");
    return 0;
}