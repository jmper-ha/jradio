#include "ssm3582.h"

// POWER_CTRL's reset value 0xA1 less SPWDN: APWDN_EN and TEMP_PWDN set.
#define POWER_CTRL_BASE 0xA0U
#define POWER_CTRL_SPWDN 0x01U
// AMP_DAC_CTRL's reset value 0x8A less ANA_GAIN: DAC_LPM and EDGE set.
#define AMP_DAC_CTRL_BASE 0x88U
#define ANA_GAIN_MAX 3U

bool ssm3582_rate_takes_64fs(uint32_t sample_rate)
{
    return sample_rate >= 32000U && sample_rate <= 192000U;
}

uint32_t ssm3582_rate_factor(uint32_t sample_rate)
{
    if (sample_rate >= 32000U) return 1U;
    if (sample_rate >= 16000U) return 2U;
    return 4U;
}

uint8_t ssm3582_dac_ctrl_word(uint32_t sample_rate)
{
    // DAC_FS by table 30; the rest of the register is its reset value, 0.
    if (sample_rate >= 8000U && sample_rate <= 12000U) return 0x00U;
    if (sample_rate >= 16000U && sample_rate <= 24000U) return 0x01U;
    if (sample_rate >= 32000U && sample_rate <= 48000U) return 0x02U;
    if (sample_rate >= 64000U && sample_rate <= 96000U) return 0x03U;
    if (sample_rate >= 128000U && sample_rate <= 192000U) return 0x04U;
    /* The band between 48 and 64 kHz has a code of its own, 101; nothing
     * here plays at those rates, but a stream that did should not fall
     * through. */
    if (sample_rate > 48000U && sample_rate < 64000U) return 0x05U;
    return 0xFFU;
}

uint8_t ssm3582_power_ctrl_word(bool powered)
{
    return powered ? POWER_CTRL_BASE : (uint8_t)(POWER_CTRL_BASE | POWER_CTRL_SPWDN);
}

uint8_t ssm3582_amp_dac_ctrl_word(uint8_t gain_code)
{
    return (uint8_t)(AMP_DAC_CTRL_BASE | (gain_code > ANA_GAIN_MAX ? 0U : gain_code));
}

bool ssm3582_identity_matches(uint8_t vendor, uint8_t device1, uint8_t device2)
{
    return vendor == SSM3582_VENDOR_ID && device1 == SSM3582_DEVICE_ID1 &&
           device2 == SSM3582_DEVICE_ID2;
}
