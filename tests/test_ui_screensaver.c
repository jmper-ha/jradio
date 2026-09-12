#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_screensaver.h"

#define AREA_W 480
#define AREA_H 320
#define BLOCK_W 300
#define BLOCK_H 160

static device_settings_t settings_with(device_screensaver_t mode)
{
    device_settings_t settings = {
        .brightness = 50,
        .screensaver = mode,
        .screensaver_seconds = 30,
        .screensaver_brightness = 20,
    };
    return settings;
}

static void test_nothing_happens_while_the_saver_is_off(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_OFF);
    ui_screensaver_view_t view;
    /* The first pass is a change by definition: the caller has never seen a
     * view. After that, an hour of nothing is nothing. */
    assert(ui_screensaver_step(&saver, &settings, 1000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(!view.active && view.backlight == 50 && !view.cover && !view.clock);
    assert(!ui_screensaver_step(&saver, &settings, 3600000, AREA_W, AREA_H, BLOCK_W, BLOCK_H,
                                &view));
    assert(!view.active);
    /* And a press is an ordinary press. */
    assert(!ui_screensaver_wake(&saver, settings.screensaver, 3600001));
}

static void test_dim_takes_the_backlight_down_after_the_wait_and_a_press_passes(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_DIM);
    ui_screensaver_view_t view;
    (void)ui_screensaver_step(&saver, &settings, 0, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    /* One millisecond short is still awake. */
    assert(!ui_screensaver_step(&saver, &settings, 29999, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(!view.active && view.backlight == 50);
    assert(ui_screensaver_step(&saver, &settings, 30000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.active && view.backlight == 20 && !view.cover && !view.clock);
    /* The screen is still readable, so the press that wakes it also does its
     * job - it is not swallowed. */
    assert(!ui_screensaver_wake(&saver, settings.screensaver, 40000));
    assert(ui_screensaver_step(&saver, &settings, 40000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(!view.active && view.backlight == 50);
    /* And the wait starts again from the press, not from where it was. */
    assert(!ui_screensaver_step(&saver, &settings, 69999, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(ui_screensaver_step(&saver, &settings, 70000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.active);
}

static void test_blank_turns_the_panel_off_and_the_waking_press_goes_no_further(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_BLANK);
    ui_screensaver_view_t view;
    (void)ui_screensaver_step(&saver, &settings, 0, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(ui_screensaver_step(&saver, &settings, 30000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.active && view.backlight == 0 && view.cover && !view.clock);
    /* Nobody can see what they are pressing on a dark panel, so the press
     * that wakes it must not also change the station. */
    assert(ui_screensaver_wake(&saver, settings.screensaver, 31000));
    /* The rest of the same turn of the knob goes with it - the detents come
     * in a burst and the panel is not lit yet - and only once the grace has
     * run out does a press count again. */
    assert(ui_screensaver_wake(&saver, settings.screensaver, 31010));
    assert(ui_screensaver_wake(&saver, settings.screensaver,
                               31000 + UI_SCREENSAVER_WAKE_GRACE_MS - 1));
    assert(!ui_screensaver_wake(&saver, settings.screensaver,
                                31000 + UI_SCREENSAVER_WAKE_GRACE_MS));
    assert(!ui_screensaver_wake(&saver, settings.screensaver, 31500));
    (void)ui_screensaver_step(&saver, &settings, 31500, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(!view.active && view.backlight == 50 && !view.cover);
}

static void test_the_clock_floats_and_bounces(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_CLOCK);
    ui_screensaver_view_t view;
    (void)ui_screensaver_step(&saver, &settings, 0, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(ui_screensaver_step(&saver, &settings, 30000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.active && view.cover && view.clock);
    /* Lit at the idle level - the clock is there to be read in the dark. */
    assert(view.backlight == 20);
    /* Placed where it fits. */
    assert(view.x >= 0 && view.x + BLOCK_W <= AREA_W);
    assert(view.y >= 0 && view.y + BLOCK_H <= AREA_H);
    /* Time alone moves nothing here: the drift is the panel's, driven by
     * LVGL's animation. A later pass reports the same corner. */
    const int placed_x = view.x;
    const int placed_y = view.y;
    assert(!ui_screensaver_step(&saver, &settings, 40000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.x == placed_x && view.y == placed_y);
    /* And somewhere else the next time it comes up - over a run of them,
     * not the same row every night. */
    bool moved = false;
    for (uint32_t round = 1; round <= 8 && !moved; ++round) {
        (void)ui_screensaver_wake(&saver, settings.screensaver, 40000 + round * 100000);
        (void)ui_screensaver_step(&saver, &settings, 40000 + round * 100000 + 30000, AREA_W, AREA_H,
                                  BLOCK_W, BLOCK_H, &view);
        assert(view.x >= 0 && view.x + BLOCK_W <= AREA_W);
        assert(view.y >= 0 && view.y + BLOCK_H <= AREA_H);
        moved = view.y != placed_y || view.x != placed_x;
    }
    assert(moved);

    /* A leg's length follows the speed, and never rounds down to nothing:
     * a zero-length animation is one LVGL finishes at once, and the ready
     * callback would start the next leg in the same call, for ever. */
    assert(ui_screensaver_leg_ms(0, UI_SCREENSAVER_SPEED_PX_PER_S) == 1000U);
    assert(ui_screensaver_leg_ms(UI_SCREENSAVER_SPEED_PX_PER_S, 0) == 1000U);
    assert(ui_screensaver_leg_ms(0, 3 * UI_SCREENSAVER_SPEED_PX_PER_S) == 3000U);
    assert(ui_screensaver_leg_ms(7, 7) == 40U);

    /* Waking swallows the press, like the blank mode. */
    assert(ui_screensaver_wake(&saver, settings.screensaver, 40020));
}

static void test_a_block_wider_than_the_panel_starts_at_the_corner(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_CLOCK);
    ui_screensaver_view_t view;
    (void)ui_screensaver_step(&saver, &settings, 30000, 200, 100, 300, 160, &view);
    assert(view.x == 0 && view.y == 0);
}

static void test_switching_the_mode_off_from_the_page_wakes_the_panel(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_BLANK);
    ui_screensaver_view_t view;
    (void)ui_screensaver_step(&saver, &settings, 30000, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(view.active);
    settings.screensaver = DEVICE_SCREENSAVER_OFF;
    assert(ui_screensaver_step(&saver, &settings, 30010, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(!view.active && view.backlight == 50 && !view.cover);
    /* And a change of mode while resting is applied where it stands: the
     * dark panel becomes the clock without waiting for a press. */
    settings.screensaver = DEVICE_SCREENSAVER_CLOCK;
    (void)ui_screensaver_wake(&saver, settings.screensaver, 30020);
    (void)ui_screensaver_step(&saver, &settings, 60020, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(view.active && view.clock);
    settings.screensaver = DEVICE_SCREENSAVER_DIM;
    assert(ui_screensaver_step(&saver, &settings, 60030, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.active && !view.cover && !view.clock && view.backlight == 20);
    /* A new brightness typed on the page while dimmed is not what the panel
     * shows - the idle level is - but it is what it comes back to. */
    settings.brightness = 70;
    assert(!ui_screensaver_step(&saver, &settings, 60040, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view));
    assert(view.backlight == 20);
    (void)ui_screensaver_wake(&saver, settings.screensaver, 60050);
    (void)ui_screensaver_step(&saver, &settings, 60050, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(view.backlight == 70);
}

static void test_the_wraparound_of_the_millisecond_clock_is_survived(void)
{
    ui_screensaver_t saver;
    ui_screensaver_init(&saver, 0xFFFFFFF0U);
    device_settings_t settings = settings_with(DEVICE_SCREENSAVER_DIM);
    ui_screensaver_view_t view;
    (void)ui_screensaver_step(&saver, &settings, 0xFFFFFFF0U, AREA_W, AREA_H, BLOCK_W, BLOCK_H,
                              &view);
    assert(!view.active);
    (void)ui_screensaver_step(&saver, &settings, 29984U, AREA_W, AREA_H, BLOCK_W, BLOCK_H, &view);
    assert(view.active);
}

static void test_the_time_line(void)
{
    char text[16];
    ui_screensaver_time_text(text, sizeof(text), true, 9, 5);
    assert(strcmp(text, "09:05") == 0);
    ui_screensaver_time_text(text, sizeof(text), true, 23, 59);
    assert(strcmp(text, "23:59") == 0);
    /* Unset, or nonsense, is dashes of the same width - the seven-segment
     * face has a glyph for the dash and the block does not change size. */
    ui_screensaver_time_text(text, sizeof(text), false, 12, 0);
    assert(strcmp(text, "--:--") == 0);
    ui_screensaver_time_text(text, sizeof(text), true, 24, 0);
    assert(strcmp(text, "--:--") == 0);
    ui_screensaver_time_text(NULL, 0, true, 1, 1);
    ui_screensaver_time_text(text, 0, true, 1, 1);
}

static void test_the_date_line_in_both_languages(void)
{
    char text[64];
    /* Thursday 11 September: the day first in Russian and in the genitive,
     * the weekday first in English. Weekday 4 is Thursday counting from
     * Sunday, as struct tm does. */
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_RU, true, 11, 9, 4);
    assert(strcmp(text, "11 сентября, четверг") == 0);
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_EN, true, 11, 9, 4);
    assert(strcmp(text, "Thursday, 11 September") == 0);
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_RU, true, 1, 1, 0);
    assert(strcmp(text, "1 января, воскресенье") == 0);
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_EN, true, 31, 12, 6);
    assert(strcmp(text, "Saturday, 31 December") == 0);
    /* Nothing until the clock is set, and nothing for a date off the
     * calendar. */
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_RU, false, 11, 9, 4);
    assert(text[0] == '\0');
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_RU, true, 11, 13, 4);
    assert(text[0] == '\0');
    ui_screensaver_date_text(text, sizeof(text), DEVICE_LANGUAGE_RU, true, 11, 9, 7);
    assert(text[0] == '\0');
    ui_screensaver_date_text(NULL, 0, DEVICE_LANGUAGE_RU, true, 11, 9, 4);
}

static void test_the_track_line(void)
{
    char text[128];
    ui_screensaver_track_text(text, sizeof(text), "Radio Montmartre", "Django Reinhardt", "Nuages");
    assert(strcmp(text, "Django Reinhardt - Nuages") == 0);
    /* No performer known: the track alone, not a dangling dash. */
    ui_screensaver_track_text(text, sizeof(text), "Radio Montmartre", "", "Nuages");
    assert(strcmp(text, "Nuages") == 0);
    ui_screensaver_track_text(text, sizeof(text), "Radio Montmartre", NULL, "Nuages");
    assert(strcmp(text, "Nuages") == 0);
    /* Nothing said about the track yet: the station, so the line is never
     * blank while something plays. */
    ui_screensaver_track_text(text, sizeof(text), "Radio Montmartre", "", "");
    assert(strcmp(text, "Radio Montmartre") == 0);
    ui_screensaver_track_text(text, sizeof(text), "", "", "");
    assert(text[0] == '\0');
    ui_screensaver_track_text(text, sizeof(text), NULL, NULL, NULL);
    assert(text[0] == '\0');
    /* A long line is cut, not overrun. */
    char small[8];
    ui_screensaver_track_text(small, sizeof(small), "", "Django Reinhardt", "Nuages");
    assert(strlen(small) == sizeof(small) - 1U);
    ui_screensaver_track_text(NULL, 0, "a", "b", "c");
}

int main(void)
{
    test_nothing_happens_while_the_saver_is_off();
    test_dim_takes_the_backlight_down_after_the_wait_and_a_press_passes();
    test_blank_turns_the_panel_off_and_the_waking_press_goes_no_further();
    test_the_clock_floats_and_bounces();
    test_a_block_wider_than_the_panel_starts_at_the_corner();
    test_switching_the_mode_off_from_the_page_wakes_the_panel();
    test_the_wraparound_of_the_millisecond_clock_is_survived();
    test_the_time_line();
    test_the_date_line_in_both_languages();
    test_the_track_line();
    printf("ui_screensaver tests passed\n");
    return 0;
}
