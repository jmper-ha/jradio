#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_status_bar.h"

static void test_no_connection_lights_nothing(void)
{
    /* Zero bars has to mean "not connected", and a weak signal must never
     * reach it: the two look the same on the strip otherwise, and the fix for
     * each is different - move the device, or set up Wi-Fi. */
    assert(ui_status_wifi_bars(false, -40) == 0U);
    assert(ui_status_wifi_bars(false, 0) == 0U);
    assert(ui_status_wifi_bars(true, -100) == 1U);
    assert(ui_status_wifi_bars(true, -127) == 1U);
}

static void test_the_thresholds_land_where_the_link_changes(void)
{
    /* -67 dBm is the practical floor for streaming without stutter, so it has
     * to be the boundary a listener can see. */
    assert(ui_status_wifi_bars(true, -30) == 4U);
    assert(ui_status_wifi_bars(true, -55) == 4U);
    assert(ui_status_wifi_bars(true, -56) == 3U);
    assert(ui_status_wifi_bars(true, -67) == 3U);
    assert(ui_status_wifi_bars(true, -68) == 2U);
    assert(ui_status_wifi_bars(true, -78) == 2U);
    assert(ui_status_wifi_bars(true, -79) == 1U);
}

static void test_the_reading_never_falls_as_the_signal_improves(void)
{
    uint8_t previous = 0U;
    for (int rssi = -110; rssi <= 0; ++rssi) {
        const uint8_t bars = ui_status_wifi_bars(true, (int8_t)rssi);
        assert(bars >= previous);
        assert(bars <= UI_STATUS_WIFI_BARS);
        previous = bars;
    }
}

static void test_the_clock_pads_both_fields(void)
{
    char text[8];
    ui_status_clock_text(text, sizeof(text), true, 21, 47);
    assert(strcmp(text, "21:47") == 0);
    /* Without padding the strip would jump a character wide every hour. */
    ui_status_clock_text(text, sizeof(text), true, 9, 5);
    assert(strcmp(text, "09:05") == 0);
    ui_status_clock_text(text, sizeof(text), true, 0, 0);
    assert(strcmp(text, "00:00") == 0);
}

static void test_an_unset_clock_says_so(void)
{
    /* Until the time has been fetched there is no time to show, and a
     * plausible wrong one is worse than an obvious placeholder. */
    char text[8];
    ui_status_clock_text(text, sizeof(text), false, 21, 47);
    assert(strcmp(text, "--:--") == 0);

    /* A value out of range means the source is broken, which is the same
     * answer. */
    ui_status_clock_text(text, sizeof(text), true, 24, 0);
    assert(strcmp(text, "--:--") == 0);
    ui_status_clock_text(text, sizeof(text), true, 12, 60);
    assert(strcmp(text, "--:--") == 0);
    ui_status_clock_text(text, sizeof(text), true, -1, 0);
    assert(strcmp(text, "--:--") == 0);

    ui_status_clock_text(NULL, sizeof(text), true, 12, 0);
    ui_status_clock_text(text, 0U, true, 12, 0);
}


static void test_a_volume_change_waits_for_the_knob_to_stop(void)
{
    /* Nothing pending, nothing to write - however long ago it was. */
    assert(!ui_volume_commit_due(false, 1000U, 9999U, 1500U));

    /* Still turning: the whole point is not to touch flash mid-gesture. */
    assert(!ui_volume_commit_due(true, 1000U, 1000U, 1500U));
    assert(!ui_volume_commit_due(true, 1000U, 2499U, 1500U));

    /* Settled. */
    assert(ui_volume_commit_due(true, 1000U, 2500U, 1500U));
    assert(ui_volume_commit_due(true, 1000U, 60000U, 1500U));
}

static void test_a_tick_wrap_does_not_defer_the_write_forever(void)
{
    /* The tick counter wraps every 49 days of uptime. Signed arithmetic here
     * would leave the last volume change unsaved until the next reboot. */
    const uint32_t before_wrap = 0xFFFFF000U;
    assert(ui_volume_commit_due(true, before_wrap, before_wrap + 2000U, 1500U));
    assert(!ui_volume_commit_due(true, before_wrap, before_wrap + 100U, 1500U));
}

int main(void)
{
    test_no_connection_lights_nothing();
    test_the_thresholds_land_where_the_link_changes();
    test_the_reading_never_falls_as_the_signal_improves();
    test_the_clock_pads_both_fields();
    test_an_unset_clock_says_so();
    test_a_volume_change_waits_for_the_knob_to_stop();
    test_a_tick_wrap_does_not_defer_the_write_forever();
    puts("ui_status_bar tests passed");
    return 0;
}
