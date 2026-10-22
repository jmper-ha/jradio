#include "ssm3582_chip.h"

#include <assert.h>
#include <stdio.h>

/* The curve an SSM3582 board's knob traces, against the chip's ladder.
 * The breakpoints are audio_volume.c's, so a setting means the same
 * loudness on either kind of board; what is checked here is that the
 * translation onto 0.375 dB steps keeps every one of those points, and
 * nothing between them lands on the same rung or backwards. */
static void test_the_volume_register_keeps_the_curve(void)
{
    assert(ssm3582_volume_register(0U) == SSM3582_VOL_MUTE);
    assert(ssm3582_volume_register(100U) == SSM3582_VOL_UNITY);
    /* Breakpoints: -4 dB is 11 steps, -20 dB is 53, -60 dB is 160. */
    assert(ssm3582_volume_register(90U) == 0x40U + 11U);
    assert(ssm3582_volume_register(50U) == 0x40U + 53U);
    assert(ssm3582_volume_register(1U) == 0x40U + 160U);
    /* Past the end of a breakpoint the same number comes back: 48% and
     * 52% sit either side of -20 dB, one step apart at least. */
    assert(ssm3582_volume_register(52U) < ssm3582_volume_register(50U));
    assert(ssm3582_volume_register(48U) > ssm3582_volume_register(50U));

    for (unsigned int percent = 1U; percent < 100U; ++percent) {
        const uint8_t quieter = ssm3582_volume_register((uint8_t)percent);
        const uint8_t louder = ssm3582_volume_register((uint8_t)(percent + 1U));
        assert(quieter > louder);
        assert(quieter > SSM3582_VOL_UNITY);
        assert(quieter < SSM3582_VOL_MUTE);
    }
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
    test_the_sample_rate_bands();
    test_the_clock_geometry_the_firmware_drives();
    test_the_chip_s_own_ratio_table();
    puts("ssm3582_chip tests passed");
    return 0;
}