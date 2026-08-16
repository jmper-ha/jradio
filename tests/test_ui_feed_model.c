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

static void test_supported_items_and_activation(void)
{
    ui_feed_model_t model;
    audio_source_t source = AUDIO_SOURCE_NONE;

    ui_feed_model_init(&model, UI_FEED_INTERNET_RADIO);
    assert(ui_feed_model_is_supported(UI_FEED_INTERNET_RADIO));
    assert(ui_feed_model_activate(UI_FEED_INTERNET_RADIO, &source));
    assert(source == AUDIO_SOURCE_INTERNET_RADIO);
    ui_feed_model_init(&model, UI_FEED_USB);
    assert(ui_feed_model_is_supported(UI_FEED_USB));
    assert(ui_feed_model_activate(UI_FEED_USB, &source));
    assert(source == AUDIO_SOURCE_USB);
    assert(!ui_feed_model_is_supported(UI_FEED_BLUETOOTH));
    assert(!ui_feed_model_activate(UI_FEED_BLUETOOTH, &source));
    assert(!ui_feed_model_is_supported(UI_FEED_SETTINGS));
    assert(!ui_feed_model_activate(UI_FEED_SETTINGS, &source));
    assert(!ui_feed_model_activate(UI_FEED_INTERNET_RADIO, NULL));
}

int main(void)
{
    test_navigation_wraps_and_stays_one_step();
    test_initial_index_is_normalized();
    test_supported_items_and_activation();
    puts("ui_feed_model tests passed");
    return 0;
}
