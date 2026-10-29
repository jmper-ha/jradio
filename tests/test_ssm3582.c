#include <assert.h>
#include <stdio.h>

#include "ssm3582.h"

/* The words against the datasheet's tables, by hand: a refactor that
 * "improves" a bit here is a chip that plays noise. */

static void test_the_rate_bands(void)
{
    assert(ssm3582_dac_ctrl_word(8000U) == 0x00U);
    assert(ssm3582_dac_ctrl_word(11025U) == 0x00U);
    assert(ssm3582_dac_ctrl_word(16000U) == 0x01U);
    assert(ssm3582_dac_ctrl_word(22050U) == 0x01U);
    assert(ssm3582_dac_ctrl_word(32000U) == 0x02U);
    assert(ssm3582_dac_ctrl_word(44100U) == 0x02U);
    assert(ssm3582_dac_ctrl_word(48000U) == 0x02U);
    assert(ssm3582_dac_ctrl_word(88200U) == 0x03U);
    assert(ssm3582_dac_ctrl_word(96000U) == 0x03U);
    assert(ssm3582_dac_ctrl_word(176400U) == 0x04U);
    assert(ssm3582_dac_ctrl_word(192000U) == 0x04U);
    assert(ssm3582_dac_ctrl_word(56000U) == 0x05U);
    // Between the bands and past them: nothing to write.
    assert(ssm3582_dac_ctrl_word(14000U) == 0xFFU);
    assert(ssm3582_dac_ctrl_word(0U) == 0xFFU);
    assert(ssm3582_dac_ctrl_word(384000U) == 0xFFU);
}

/* Table 13: 64 x Fs from 32 kHz up, not below. */
static void test_which_rates_a_64fs_clock_serves(void)
{
    assert(!ssm3582_rate_takes_64fs(22050U));
    assert(!ssm3582_rate_takes_64fs(24000U));
    assert(ssm3582_rate_takes_64fs(32000U));
    assert(ssm3582_rate_takes_64fs(44100U));
    assert(ssm3582_rate_takes_64fs(96000U));
    assert(ssm3582_rate_takes_64fs(192000U));
    assert(!ssm3582_rate_takes_64fs(384000U));
}

static void test_power_gain_and_identity(void)
{
    // Reset 0xA1 is powered down; up is the same word with SPWDN clear.
    assert(ssm3582_power_ctrl_word(false) == 0xA1U);
    assert(ssm3582_power_ctrl_word(true) == 0xA0U);
    // Reset 0x8A is 19 dB; MONO (bit 4) is never set - 0 is stereo.
    assert((ssm3582_power_ctrl_word(true) & 0x10U) == 0U);
    assert(ssm3582_amp_dac_ctrl_word(0U) == 0x88U);
    assert(ssm3582_amp_dac_ctrl_word(2U) == 0x8AU);
    assert(ssm3582_amp_dac_ctrl_word(3U) == 0x8BU);
    assert(ssm3582_amp_dac_ctrl_word(9U) == 0x88U);
    assert(ssm3582_identity_matches(0x41U, 0x35U, 0x82U));
    assert(!ssm3582_identity_matches(0x41U, 0x35U, 0x15U));
    assert(SSM3582_SAI_CTRL1_I2S == 0x00U && SSM3582_VOLUME_0DB == 0x40U);
}

int main(void)
{
    test_the_rate_bands();
    test_which_rates_a_64fs_clock_serves();
    test_power_gain_and_identity();
    puts("ssm3582 tests passed");
    return 0;
}
