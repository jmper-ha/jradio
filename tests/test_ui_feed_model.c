#include <assert.h>
#include <stdio.h>

#include "board_features.h"
#include "ui_feed_model.h"

static void test_navigation_wraps_and_stays_one_step(void)
{
    ui_feed_model_t model;

    ui_feed_model_init(&model, 0U);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
    ui_feed_model_move(&model, 1);
    assert(ui_feed_model_selected(&model) == UI_FEED_USB);
    ui_feed_model_move(&model, -1);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
    ui_feed_model_move(&model, -1);
    assert(ui_feed_model_selected(&model) == UI_FEED_SETTINGS);
}

static void test_initial_index_is_normalized(void)
{
    ui_feed_model_t model;
    ui_feed_model_init(&model, 99U);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
}

static void test_activation_maps_only_the_implemented_sources(void)
{
    /* activate() is the single answer to "can this item start playback": the
     * feed screen shows a placeholder when it says no. Settings is a false
     * here and still works - it opens a screen rather than a source, and the
     * caller handles it before ever asking. */
    audio_source_t source = AUDIO_SOURCE_NONE;

    assert(ui_feed_model_activate(UI_FEED_INTERNET_RADIO, &source));
    assert(source == AUDIO_SOURCE_INTERNET_RADIO);
    assert(ui_feed_model_activate(UI_FEED_SD_CARD, &source));
    assert(source == AUDIO_SOURCE_SD);
    assert(ui_feed_model_activate(UI_FEED_USB, &source));
    assert(source == AUDIO_SOURCE_USB);

    /* Unimplemented modes must report failure rather than silently selecting
     * nothing, or the feed would look like it opened them. */
    source = AUDIO_SOURCE_INTERNET_RADIO;
    assert(!ui_feed_model_activate(UI_FEED_BLUETOOTH, &source));
    assert(source == AUDIO_SOURCE_NONE);
    assert(!ui_feed_model_activate(UI_FEED_FM, &source));
    assert(!ui_feed_model_activate(UI_FEED_DLNA, &source));
    assert(!ui_feed_model_activate(UI_FEED_YANDEX, &source));
    assert(!ui_feed_model_activate(UI_FEED_SETTINGS, &source));

    assert(!ui_feed_model_activate(UI_FEED_INTERNET_RADIO, NULL));
}

static void test_the_cursor_follows_the_source_that_started(void)
{
    /* Leaving the player lands on the home screen, and it should land on the
     * icon of what was playing - not on whatever the cursor last touched. */
    ui_feed_model_t model;
    ui_feed_model_init(&model, UI_FEED_INTERNET_RADIO);
    assert(ui_feed_model_select_source(&model, AUDIO_SOURCE_USB));
    assert(ui_feed_model_selected(&model) == UI_FEED_USB);
    assert(ui_feed_model_select_source(&model, AUDIO_SOURCE_INTERNET_RADIO));
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);

    /* Nothing on the feed starts these, so the cursor stays where the user
     * put it rather than jumping to item 0. */
    ui_feed_model_init(&model, UI_FEED_SETTINGS);
    assert(!ui_feed_model_select_source(&model, AUDIO_SOURCE_NONE));
    assert(ui_feed_model_selected(&model) == UI_FEED_SETTINGS);
    assert(!ui_feed_model_select_source(NULL, AUDIO_SOURCE_USB));

    /* And it is the exact inverse of activation, for every item that has one:
     * the two mappings must not be able to drift apart. */
    for (uint8_t index = 0U; index < UI_FEED_ITEM_COUNT; ++index) {
        audio_source_t source = AUDIO_SOURCE_NONE;
        if (!ui_feed_model_activate((ui_feed_item_t)index, &source)) continue;
        ui_feed_model_init(&model, UI_FEED_SETTINGS);
        assert(ui_feed_model_select_source(&model, source));
        assert(ui_feed_model_selected(&model) == (ui_feed_item_t)index);
    }
}

static void test_the_carousel_skips_a_hidden_item(void)
{
#if BOARD_HAS_YANDEX_MUSIC
    ui_feed_model_t model;
    ui_feed_model_init(&model, UI_FEED_DLNA);
    assert(ui_feed_model_yandex_visible(&model));
    ui_feed_model_move(&model, 1);
    assert(ui_feed_model_selected(&model) == UI_FEED_YANDEX);

    /* Same step, with the item switched off: straight past it to Settings,
     * which is what the list home screen does with the same rule. */
    ui_feed_model_init(&model, UI_FEED_DLNA);
    ui_feed_model_set_yandex_visible(&model, false);
    assert(!ui_feed_model_yandex_visible(&model));
    ui_feed_model_move(&model, 1);
    assert(ui_feed_model_selected(&model) == UI_FEED_SETTINGS);
    ui_feed_model_move(&model, -1);
    assert(ui_feed_model_selected(&model) == UI_FEED_DLNA);

    /* Turning it off while the cursor is on it moves the cursor. */
    ui_feed_model_init(&model, UI_FEED_YANDEX);
    ui_feed_model_set_yandex_visible(&model, false);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
#else
    ui_feed_model_t model;
    ui_feed_model_init(&model, UI_FEED_DLNA);
    ui_feed_model_set_yandex_visible(&model, true);
    assert(!ui_feed_model_yandex_visible(&model));
    ui_feed_model_move(&model, 1);
    assert(ui_feed_model_selected(&model) == UI_FEED_SETTINGS);
#endif
}

int main(void)
{
    test_navigation_wraps_and_stays_one_step();
    test_initial_index_is_normalized();
    test_activation_maps_only_the_implemented_sources();
    test_the_cursor_follows_the_source_that_started();
    test_the_carousel_skips_a_hidden_item();
    puts("ui_feed_model tests passed");
    return 0;
}
