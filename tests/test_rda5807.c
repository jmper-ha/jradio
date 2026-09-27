#include <assert.h>
#include <stdio.h>

#include "rda5807.h"

static void test_frequencies_map_to_channels_and_back(void)
{
    assert(rda5807_channel_for_khz(87000U) == 0U);
    assert(rda5807_channel_for_khz(101200U) == 142U);
    assert(rda5807_khz_for_channel(142U) == 101200U);
    assert(rda5807_channel_for_khz(108000U) == 210U);
    // Off the grid rounds to the nearest channel; off the band clamps to it.
    assert(rda5807_channel_for_khz(101249U) == 142U);
    assert(rda5807_channel_for_khz(101250U) == 143U);
    assert(rda5807_channel_for_khz(76000U) == 0U);
    assert(rda5807_channel_for_khz(120000U) == 210U);
}

static void test_the_tune_word_carries_the_channel_and_the_tune_bit(void)
{
    // Channel in 15:6, TUNE at 4, band and spacing left at 87-108 / 100 kHz.
    assert(rda5807_tune_word(101200U) == ((142U << 6) | 0x10U));
    assert((rda5807_tune_word(87000U) & 0x000FU) == 0U);
}

static void test_the_control_word(void)
{
    rda5807_state_t state = {.enabled = true, .volume = 8U};
    const uint16_t on = rda5807_control_word(&state, false, false, false);
    // Outputs driven, not muted, stops at the band's edge, new demodulator, on.
    assert(on == 0xC085U);
    assert(rda5807_control_word(&state, true, false, false) == (on | 0x0002U));
    assert(rda5807_control_word(&state, false, true, true) == (on | 0x0300U));
    assert(rda5807_control_word(&state, false, true, false) == (on | 0x0100U));
    state.muted = true;
    state.mono = true;
    state.bass = true;
    assert(rda5807_control_word(&state, false, false, false) == ((on & ~0x4000U) | 0x3000U));
    state = (rda5807_state_t){0};
    assert((rda5807_control_word(&state, false, false, false) & 0x0001U) == 0U);
}

static void test_options_and_volume(void)
{
    assert(rda5807_options_word() == 0x0A00U);  // 50 us de-emphasis, soft mute
    assert(rda5807_volume_word(0U) == 0x8880U);
    assert(rda5807_volume_word(15U) == 0x888FU);
    assert(rda5807_volume_word(200U) == 0x888FU);
}

static void test_the_status_words(void)
{
    rda5807_status_t status;
    // STC, stereo, channel 142; RSSI 40, a station.
    rda5807_parse_status((uint16_t)(0x4000U | 0x0400U | 142U), (uint16_t)((40U << 9) | 0x0100U),
                         &status);
    assert(status.tune_complete);
    assert(!status.seek_failed);
    assert(status.stereo);
    assert(status.khz == 101200U);
    assert(status.rssi == 40U);
    assert(status.station);
    rda5807_parse_status(0x6000U, 0x0000U, &status);
    assert(status.seek_failed);
    assert(!status.stereo);
    assert(!status.station);
    assert(status.khz == 87000U);
}

int main(void)
{
    test_frequencies_map_to_channels_and_back();
    test_the_tune_word_carries_the_channel_and_the_tune_bit();
    test_the_control_word();
    test_options_and_volume();
    test_the_status_words();
    puts("rda5807 tests passed");
    return 0;
}
