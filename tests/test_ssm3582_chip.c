#include "ssm3582_chip.h"

#include <assert.h>
#include <stdio.h>

/* The curve an SSM3582 board's knob traces, against the chip's ladder.
 * The breakpoints are audio_volume.c's, so a setting means the same
 * loudness on either kind of board; what is checked here is that the
 * translation onto 0.375 dB steps keeps every one of those points, and
 * nothing between them lands on the same rung or backwards. The unity
 * ceiling is the one a board without AUDIO_DAC_MAX_VOLUME_DB gets. */
static void test_the_volume_register_keeps_the_curve(void)
{
    assert(ssm3582_volume_register(0U, SSM3582_VOL_UNITY) == SSM3582_VOL_MUTE);
    assert(ssm3582_volume_register(100U, SSM3582_VOL_UNITY) == SSM3582_VOL_UNITY);
    /* Breakpoints: -4 dB is 11 steps, -20 dB is 53, -60 dB is 160. */
    assert(ssm3582_volume_register(90U, SSM3582_VOL_UNITY) == 0x40U + 11U);
    assert(ssm3582_volume_register(50U, SSM3582_VOL_UNITY) == 0x40U + 53U);
    assert(ssm3582_volume_register(1U, SSM3582_VOL_UNITY) == 0x40U + 160U);
    /* Past the end of a breakpoint the same number comes back: 48% and
     * 52% sit either side of -20 dB, one step apart at least. */
    assert(ssm3582_volume_register(52U, SSM3582_VOL_UNITY) <
           ssm3582_volume_register(50U, SSM3582_VOL_UNITY));
    assert(ssm3582_volume_register(48U, SSM3582_VOL_UNITY) >
           ssm3582_volume_register(50U, SSM3582_VOL_UNITY));

    for (unsigned int percent = 1U; percent < 100U; ++percent) {
        const uint8_t quieter = ssm3582_volume_register((uint8_t)percent, SSM3582_VOL_UNITY);
        const uint8_t louder =
            ssm3582_volume_register((uint8_t)(percent + 1U), SSM3582_VOL_UNITY);
        assert(quieter > louder);
        assert(quieter > SSM3582_VOL_UNITY);
        assert(quieter < SSM3582_VOL_MUTE);
    }
}

/* The board option's whole dB, turned into a rung of the ladder: exactly
 * where board_options.h's AUDIO_DAC_MAX_VOLUME_DB lands. */
static void test_the_ceiling_from_whole_decibels(void)
{
    assert(ssm3582_db_to_volume_register(0) == SSM3582_VOL_UNITY);
    /* A whole dB is 8/3 steps, so only some land exactly; every one
     * lands on the rung nearest it. 12 dB is 32 steps exactly - the
     * figure this repo's board asks for - 1 dB is 1.125. */
    assert(ssm3582_db_to_volume_register(12) == 0x40U - 32U);
    assert(ssm3582_db_to_volume_register(-6) == 0x40U + 16U);
    assert(ssm3582_db_to_volume_register(1) == 0x40U - 3U);
    assert(ssm3582_db_to_volume_register(-1) == 0x40U + 3U);
    /* The ends of the ladder, and past them: the answer stays inside
     * it, and the mute - a setting of its own, not a rung - is never
     * returned. */
    assert(ssm3582_db_to_volume_register(24) == SSM3582_VOL_LOUDEST);
    assert(ssm3582_db_to_volume_register(25) == SSM3582_VOL_LOUDEST);
    assert(ssm3582_db_to_volume_register(100) == SSM3582_VOL_LOUDEST);
    assert(ssm3582_db_to_volume_register(-71) == SSM3582_VOL_QUIETEST - 1U);
    assert(ssm3582_db_to_volume_register(-100) == SSM3582_VOL_QUIETEST);
    /* And the whole range between the option's ends stays inside the
     * ladder and monotonic, so no figure a board may name surprises. */
    uint8_t previous = SSM3582_VOL_LOUDEST;
    for (int db = 23; db >= -71; --db) {
        const uint8_t rung = ssm3582_db_to_volume_register(db);
        assert(rung > previous);
        assert(rung <= SSM3582_VOL_QUIETEST);
        previous = rung;
    }
}

/* A board whose loudest setting is not unity - this repo's +12 dB -
 * shifts the whole curve and keeps every interval in it. */
static void test_a_shifted_ceiling_moves_the_whole_curve(void)
{
    const uint8_t ceiling = ssm3582_db_to_volume_register(12);
    assert(ceiling == SSM3582_VOL_UNITY - 32U);
    assert(ssm3582_volume_register(0U, ceiling) == SSM3582_VOL_MUTE);
    assert(ssm3582_volume_register(100U, ceiling) == ceiling);
    /* The same breakpoints, counted from the new top. */
    assert(ssm3582_volume_register(90U, ceiling) == ceiling + 11U);
    assert(ssm3582_volume_register(50U, ceiling) == ceiling + 53U);

    for (unsigned int percent = 1U; percent < 100U; ++percent) {
        const uint8_t shifted = ssm3582_volume_register((uint8_t)percent, ceiling);
        const uint8_t louder =
            ssm3582_volume_register((uint8_t)(percent + 1U), ceiling);
        assert(shifted > louder);
        assert(shifted <= SSM3582_VOL_QUIETEST);
        /* The same number of steps below the new top as below unity -
         * the unity curve never runs off the ladder, so it is a
         * measure of the steps themselves - stopped at the bottom when
         * that lands past it. */
        const uint8_t plain = ssm3582_volume_register((uint8_t)percent, SSM3582_VOL_UNITY);
        const uint32_t steps_below = (uint32_t)plain - (uint32_t)SSM3582_VOL_UNITY;
        const uint32_t exact = (uint32_t)ceiling + steps_below;
        assert(shifted == (uint8_t)(exact > (uint32_t)SSM3582_VOL_QUIETEST
                                       ? (uint32_t)SSM3582_VOL_QUIETEST
                                       : exact));
    }

    /* A ceiling so quiet that the bottom of the curve falls off the
     * ladder: it stops at its quietest rung, never at the mute. */
    const uint8_t low = ssm3582_db_to_volume_register(-60);
    assert(low == 0x40U + 160U);
    assert(ssm3582_volume_register(1U, low) == SSM3582_VOL_QUIETEST);
    assert(ssm3582_volume_register(10U, low) == SSM3582_VOL_QUIETEST);
    /* Where the ladder still has room, the steps are the curve's own,
     * and every setting stays a step below the one above it - only
     * clamped together at the bottom, which is what quiet means on a
     * ceiling that low. */
    assert(ssm3582_volume_register(80U, low) == 0xE0U + 21U);
    assert(ssm3582_volume_register(80U, low) < ssm3582_volume_register(10U, low));
    assert(ssm3582_volume_register(100U, low) == low);
}

/* The chip's own bands: the field value the DAC_CTRL wants for a rate,
 * and the holes between them where there is no field at all. */
static void test_the_sample_rate_bands(void)
{
    assert(ssm3582_dac_fs_bits(8000U) == 0U);
    assert(ssm3582_dac_fs_bits(12000U) == 0U);
    assert(ssm3582_dac_fs_bits(13000U) == SSM3582_DAC_FS_NONE);
    assert(ssm3582_dac_fs_bits(15999U) == SSM3582_DAC_FS_NONE);
    assert(ssm3582_dac_fs_bits(16000U) == 1U);
    assert(ssm3582_dac_fs_bits(22050U) == 1U);
    assert(ssm3582_dac_fs_bits(32000U) == 2U);
    assert(ssm3582_dac_fs_bits(44100U) == 2U);
    assert(ssm3582_dac_fs_bits(48000U) == 2U);
    assert(ssm3582_dac_fs_bits(50000U) == 5U);
    assert(ssm3582_dac_fs_bits(72000U) == 5U);
    assert(ssm3582_dac_fs_bits(88200U) == 3U);
    assert(ssm3582_dac_fs_bits(96000U) == 3U);
    assert(ssm3582_dac_fs_bits(176400U) == 4U);
    assert(ssm3582_dac_fs_bits(192000U) == 4U);
    assert(ssm3582_dac_fs_bits(193000U) == SSM3582_DAC_FS_NONE);
    assert(ssm3582_dac_fs_bits(0U) == SSM3582_DAC_FS_NONE);
}

/* The clock geometry the firmware drives (two 32-bit slots, 64 x Fs)
 * against the chip's table, and the floor and ceiling under it. */
static void test_the_clock_geometry_the_firmware_drives(void)
{
    assert(ssm3582_bclk_rate_ok(32000U, SSM3582_BCLK_RATIO)); /* exactly the floor */
    assert(ssm3582_bclk_rate_ok(44100U, SSM3582_BCLK_RATIO));
    assert(ssm3582_bclk_rate_ok(48000U, SSM3582_BCLK_RATIO));
    assert(ssm3582_bclk_rate_ok(96000U, SSM3582_BCLK_RATIO));
    assert(ssm3582_bclk_rate_ok(192000U, SSM3582_BCLK_RATIO));
    /* Below 32 kHz the same geometry falls short of the chip - every
     * station rate under it, and the 32 x Fs the narrower slots used. */
    assert(!ssm3582_bclk_rate_ok(31999U, SSM3582_BCLK_RATIO));
    assert(!ssm3582_bclk_rate_ok(22050U, SSM3582_BCLK_RATIO));
    assert(!ssm3582_bclk_rate_ok(11025U, SSM3582_BCLK_RATIO));
    assert(!ssm3582_bclk_rate_ok(44100U, 32U));
}

/* The datasheet's table itself, beyond the one geometry we drive. */
static void test_the_chip_s_own_ratio_table(void)
{
    /* 32 kHz to 48 kHz takes 50 upwards: 50 x 48 kHz is 2.4 MHz, over
     * the floor, so it is fine. */
    assert(ssm3582_bclk_rate_ok(48000U, 50U));
    assert(ssm3582_bclk_rate_ok(32000U, 1024U));
    assert(!ssm3582_bclk_rate_ok(32000U, 50U)); /* 1.6 MHz: under the floor */
    /* 8 kHz to 12 kHz only takes long frames: 200 x Fs or more. */
    assert(ssm3582_bclk_rate_ok(8000U, 256U));
    assert(!ssm3582_bclk_rate_ok(8000U, 128U));
    /* Each band ends where the chip says it ends, whatever the pins. */
    assert(ssm3582_bclk_rate_ok(96000U, 512U)); /* 49.152 MHz exactly */
    assert(!ssm3582_bclk_rate_ok(96000U, 768U));
    assert(ssm3582_bclk_rate_ok(192000U, 256U));
    assert(!ssm3582_bclk_rate_ok(192000U, 384U));
    /* A ratio the chip does not name at all, and a rate outside every
     * band, whatever the clock. */
    assert(!ssm3582_bclk_rate_ok(44100U, 63U));
    assert(!ssm3582_bclk_rate_ok(14000U, 1024U));
    assert(!ssm3582_bclk_rate_ok(7000U, 4096U));
}

int main(void)
{
    test_the_volume_register_keeps_the_curve();
    test_the_ceiling_from_whole_decibels();
    test_a_shifted_ceiling_moves_the_whole_curve();
    test_the_sample_rate_bands();
    test_the_clock_geometry_the_firmware_drives();
    test_the_chip_s_own_ratio_table();
    puts("ssm3582_chip tests passed");
    return 0;
}