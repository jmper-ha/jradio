#include <assert.h>
#include <stdio.h>

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

int main(void)
{
    test_navigation_wraps_and_stays_one_step();
    test_initial_index_is_normalized();
    test_activation_maps_only_the_implemented_sources();
    puts("ui_feed_model tests passed");
    return 0;
}
