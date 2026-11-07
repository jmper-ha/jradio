#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "board_features.h"
#include "ui_feed_model.h"

/* Visibility is a mask, not a bool - a bare `true` would mean "internet radio
   only". */
#define ALL UI_MENU_VISIBLE_ALL
#include "ui_menu.h"

static void test_navigation_wraps_and_stays_one_step(void)
{
    ui_feed_model_t model;

    ui_feed_model_init(&model, 0U);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
    ui_feed_model_move(&model, 1);
    /* The next icon along, whichever item that is on this board. */
    assert(ui_feed_model_selected(&model) ==
           (ui_feed_item_t)ui_menu_visible_item_at(1U, ALL));
    ui_feed_model_move(&model, -1);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
    ui_feed_model_move(&model, -1);
    assert(ui_feed_model_selected(&model) == UI_FEED_SETTINGS);
}

static void test_the_carousel_carries_only_the_built_items(void)
{
    /* Deliberately the same rule as the list home screen, and deliberately the
     * same code: the carousel must not roll past an icon for a part this board
     * does not have. Two turns of the whole ring, so a wrap is covered too. */
    ui_feed_model_t model;
    ui_feed_model_init(&model, 0U);
    for (uint8_t step = 0U; step < UI_FEED_ITEM_COUNT * 2U; ++step) {
        ui_feed_model_move(&model, 1);
        assert(ui_menu_item_is_visible((ui_menu_item_t)ui_feed_model_selected(&model), ALL));
    }
    for (uint8_t step = 0U; step < UI_FEED_ITEM_COUNT * 2U; ++step) {
        ui_feed_model_move(&model, -1);
        assert(ui_menu_item_is_visible((ui_menu_item_t)ui_feed_model_selected(&model), ALL));
    }
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
    /* The media server plays like the rest, so the carousel has to open it
       like the rest. It did not, and the icon answered "функция пока
       недоступна" on a source that was finished and working - the one place
       where forgetting a case is invisible until somebody presses it. */
    assert(ui_feed_model_activate(UI_FEED_DLNA, &source));
    assert(source == AUDIO_SOURCE_DLNA);
    /* And the same lesson a second time, with the phone: the tile said
       "функция пока недоступна" the first time the module was plugged in.
       Whether the tile is shown at all is the board's business (the menu
       hides it without a module); opening it is this function's. */
    assert(ui_feed_model_activate(UI_FEED_BLUETOOTH, &source));
    assert(source == AUDIO_SOURCE_BLUETOOTH);

    /* And the tuner, which has a source of its own now. */
    assert(ui_feed_model_activate(UI_FEED_FM, &source));
    assert(source == AUDIO_SOURCE_FM);

    /* Items that are not a plain source must report failure rather than
     * silently selecting nothing, or the feed would look like it opened them. */
    source = AUDIO_SOURCE_INTERNET_RADIO;
    assert(!ui_feed_model_activate(UI_FEED_YANDEX, &source));
    assert(source == AUDIO_SOURCE_NONE);
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

    /* And it is the exact inverse of activation, for every item that has one
     * and that this board carries: the two mappings must not be able to drift
     * apart. */
    for (uint8_t index = 0U; index < UI_FEED_ITEM_COUNT; ++index) {
        audio_source_t source = AUDIO_SOURCE_NONE;
        if (!ui_feed_model_activate((ui_feed_item_t)index, &source)) continue;
        if (!ui_menu_item_is_visible((ui_menu_item_t)index, ALL)) continue;
        ui_feed_model_init(&model, UI_FEED_SETTINGS);
        assert(ui_feed_model_select_source(&model, source));
        assert(ui_feed_model_selected(&model) == (ui_feed_item_t)index);
    }
}

static void test_the_carousel_skips_a_hidden_item(void)
{
    ui_feed_model_t model;
#if BOARD_HAS_YANDEX_MUSIC
    /* Whichever icon sits before Yandex Music on this board: naming one would
     * tie the test to a part the board may not carry. */
    const uint8_t before = (uint8_t)ui_menu_item_step(UI_MENU_ITEM_YANDEX_MUSIC, -1, ALL);

    ui_feed_model_init(&model, before);
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC,
                                   ui_feed_model_visible_mask(&model)));
    ui_feed_model_move(&model, 1);
    assert(ui_feed_model_selected(&model) == UI_FEED_YANDEX);

    /* Same step, with the item switched off: straight past it to Settings,
     * which is what the list home screen does with the same rule. */
    ui_feed_model_init(&model, before);
    ui_feed_model_set_source_visible(&model, UI_MENU_ITEM_YANDEX_MUSIC, false);
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC,
                                    ui_feed_model_visible_mask(&model)));
    ui_feed_model_move(&model, 1);
    assert(ui_feed_model_selected(&model) == UI_FEED_SETTINGS);
    ui_feed_model_move(&model, -1);
    assert(ui_feed_model_selected(&model) == (ui_feed_item_t)before);

    /* Turning it off while the cursor is on it moves the cursor. */
    ui_feed_model_init(&model, UI_FEED_YANDEX);
    ui_feed_model_set_source_visible(&model, UI_MENU_ITEM_YANDEX_MUSIC, false);
    assert(ui_feed_model_selected(&model) == UI_FEED_INTERNET_RADIO);
#else
    ui_feed_model_init(&model, UI_FEED_INTERNET_RADIO);
    ui_feed_model_set_source_visible(&model, UI_MENU_ITEM_YANDEX_MUSIC, true);
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC,
                                    ui_feed_model_visible_mask(&model)));
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, ALL));
#endif
}

/* Which way the picture moved under one detent: +1 right, -1 left. The ring
 * is read off the tiles - every one of them has to have shifted by the same
 * slot - and the dot off its index; a wrap of the dot row is not a move. */
static int ring_shift(const ui_feed_item_t *before, const ui_feed_item_t *after,
                      unsigned slots)
{
    for (int shift = -1; shift <= 1; shift += 2) {
        bool all = true;
        for (unsigned slot = 0U; slot < slots; ++slot) {
            const int from = (int)slot - shift;
            if (from < 0 || from >= (int)slots) continue;
            if (after[slot] != before[from]) all = false;
        }
        if (all) return shift;
    }
    return 0;
}

static void check_ring_and_dot_agree(ui_menu_visible_mask_t shown)
{
    /* Both slot counts a panel can have: 3 on 240 px, 5 on everything wider. */
    const unsigned slot_counts[] = {3U, 5U};
    for (size_t count = 0U; count < 2U; ++count) {
        const unsigned slots = slot_counts[count];
        for (int reversed = 0; reversed <= 1; ++reversed) {
            for (int clockwise = 0; clockwise <= 1; ++clockwise) {
                int direction = 0;
                for (uint8_t start = 0U; start < ui_menu_visible_count(shown); ++start) {
                    ui_feed_model_t model;
                    ui_feed_model_init(&model, 0U);
                    model.visible = shown;
                    model.selected = (uint8_t)ui_menu_visible_item_at(start, shown);
                    ui_feed_item_t before[5];
                    ui_feed_item_t after[5];
                    for (unsigned slot = 0U; slot < slots; ++slot) {
                        before[slot] = ui_feed_model_item_in_slot(&model, slot, slots);
                    }
                    assert(before[slots / 2U] == ui_feed_model_selected(&model));
                    const int dot_before = (int)ui_feed_model_lit_dot(&model);
                    ui_feed_model_move(&model,
                                       ui_feed_model_turn_step(clockwise != 0, reversed != 0));
                    for (unsigned slot = 0U; slot < slots; ++slot) {
                        after[slot] = ui_feed_model_item_in_slot(&model, slot, slots);
                    }
                    const int ring = ring_shift(before, after, slots);
                    assert(ring != 0);
                    const int dot = (int)ui_feed_model_lit_dot(&model) - dot_before;
                    if (dot == 1 || dot == -1) assert(dot == ring);
                    else assert(dot == -ring * ((int)ui_menu_visible_count(shown) - 1));
                    /* The same way from every starting item. */
                    if (direction == 0) direction = ring;
                    assert(ring == direction);
                }
                /* As it was before the switch: clockwise slides left. */
                assert(direction == ((clockwise != 0) != (reversed != 0) ? -1 : 1));
            }
        }
    }
}

static void test_the_ring_and_the_dot_move_the_same_way(void)
{
    check_ring_and_dot_agree(ALL);
    // A dot fewer and a tile skipped must not change which way anything goes.
    check_ring_and_dot_agree(ALL & ~UI_MENU_VISIBLE(UI_MENU_ITEM_SETTINGS - 1));
}

int main(void)
{
    test_the_ring_and_the_dot_move_the_same_way();
    test_navigation_wraps_and_stays_one_step();
    test_initial_index_is_normalized();
    test_activation_maps_only_the_implemented_sources();
    test_the_cursor_follows_the_source_that_started();
    test_the_carousel_carries_only_the_built_items();
    test_the_carousel_skips_a_hidden_item();
    puts("ui_feed_model tests passed");
    return 0;
}
