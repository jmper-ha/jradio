#include <assert.h>
#include <stdio.h>

#include "ui_peek.h"

static const device_settings_t k_settings = {
    .brightness = 50,
    .screensaver = DEVICE_SCREENSAVER_BLANK,
    .screensaver_seconds = 30,
    .screensaver_brightness = 20,
};

static void test_from_the_player_clock_weather_back(void)
{
    ui_peek_t peek;
    ui_peek_init(&peek);
    const ui_screensaver_view_t awake = {.backlight = 50};
    assert(ui_peek_face(&peek, 0U) == UI_PEEK_NONE);
    ui_peek_press(&peek, &awake, true, 1000U);
    assert(ui_peek_face(&peek, 1000U) == UI_PEEK_CLOCK);
    ui_peek_press(&peek, &awake, true, 3000U);
    assert(ui_peek_face(&peek, 3000U) == UI_PEEK_WEATHER);
    // The wait starts again with each press.
    assert(ui_peek_face(&peek, 3000U + UI_PEEK_MS - 1U) == UI_PEEK_WEATHER);
    ui_peek_press(&peek, &awake, true, 4000U);
    assert(ui_peek_face(&peek, 4000U) == UI_PEEK_NONE);
    // A fourth press begins again.
    ui_peek_press(&peek, &awake, true, 5000U);
    assert(ui_peek_face(&peek, 5000U) == UI_PEEK_CLOCK);
}

static void test_the_wait_running_out_goes_back(void)
{
    ui_peek_t peek;
    ui_peek_init(&peek);
    const ui_screensaver_view_t awake = {0};
    ui_peek_press(&peek, &awake, true, 100U);
    assert(ui_peek_face(&peek, 100U + UI_PEEK_MS - 1U) == UI_PEEK_CLOCK);
    assert(ui_peek_face(&peek, 100U + UI_PEEK_MS) == UI_PEEK_NONE);
    // A press after the wait is a first press again, not a second.
    ui_peek_press(&peek, &awake, true, 100U + UI_PEEK_MS + 50U);
    assert(ui_peek_face(&peek, 100U + UI_PEEK_MS + 50U) == UI_PEEK_CLOCK);
}

static void test_without_weather_only_the_clock(void)
{
    ui_peek_t peek;
    ui_peek_init(&peek);
    const ui_screensaver_view_t awake = {0};
    ui_peek_press(&peek, &awake, false, 0U);
    assert(ui_peek_face(&peek, 0U) == UI_PEEK_CLOCK);
    ui_peek_press(&peek, &awake, false, 10U);
    assert(ui_peek_face(&peek, 10U) == UI_PEEK_NONE);
}

static void test_a_saver_face_is_skipped(void)
{
    ui_peek_t peek;
    ui_peek_init(&peek);
    const ui_screensaver_view_t clock_saver = {.active = true, .cover = true, .clock = true};
    ui_peek_press(&peek, &clock_saver, true, 0U);
    assert(ui_peek_face(&peek, 0U) == UI_PEEK_WEATHER);
    ui_peek_press(&peek, &clock_saver, true, 10U);
    assert(ui_peek_face(&peek, 10U) == UI_PEEK_NONE);
    // Over the clock saver with no weather the key has nothing to show.
    ui_peek_press(&peek, &clock_saver, false, 20U);
    assert(ui_peek_face(&peek, 20U) == UI_PEEK_NONE);

    const ui_screensaver_view_t weather_saver = {.active = true, .cover = true, .weather = true};
    ui_peek_press(&peek, &weather_saver, true, 30U);
    assert(ui_peek_face(&peek, 30U) == UI_PEEK_CLOCK);
    ui_peek_press(&peek, &weather_saver, true, 40U);
    assert(ui_peek_face(&peek, 40U) == UI_PEEK_NONE);
}

static void test_another_input_takes_it_down(void)
{
    ui_peek_t peek;
    ui_peek_init(&peek);
    const ui_screensaver_view_t awake = {0};
    assert(!ui_peek_cancel(&peek));
    ui_peek_press(&peek, &awake, true, 0U);
    assert(ui_peek_cancel(&peek));
    assert(ui_peek_face(&peek, 1U) == UI_PEEK_NONE);
    assert(!ui_peek_cancel(&peek));
}

static void test_the_face_is_laid_over_the_view(void)
{
    // Awake: the cover, the clock in the middle, the normal level.
    ui_screensaver_view_t view = {.backlight = 50};
    ui_peek_apply(UI_PEEK_CLOCK, &k_settings, 480, 320, 300, 160, &view);
    assert(view.cover && view.clock && !view.weather);
    assert(view.backlight == 50U && view.x == 90 && view.y == 80);
    assert(!view.active);

    // Over a dark panel: lit, but at the saver's level.
    view = (ui_screensaver_view_t){.active = true, .cover = true, .backlight = 0};
    ui_peek_apply(UI_PEEK_WEATHER, &k_settings, 480, 320, 300, 160, &view);
    assert(view.cover && view.weather && !view.clock && view.backlight == 20U);
    assert(view.active);

    // A block wider than the area sits at its edge.
    ui_peek_apply(UI_PEEK_CLOCK, &k_settings, 240, 240, 300, 160, &view);
    assert(view.x == 0 && view.y == 40);

    // Nothing up, nothing changed.
    view = (ui_screensaver_view_t){.backlight = 50};
    ui_peek_apply(UI_PEEK_NONE, &k_settings, 480, 320, 300, 160, &view);
    assert(!view.cover && view.backlight == 50U);
}

int main(void)
{
    test_from_the_player_clock_weather_back();
    test_the_wait_running_out_goes_back();
    test_without_weather_only_the_clock();
    test_a_saver_face_is_skipped();
    test_another_input_takes_it_down();
    test_the_face_is_laid_over_the_view();
    puts("ui_peek tests passed");
    return 0;
}
