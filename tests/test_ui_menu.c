#include <assert.h>
#include <stdio.h>

#include "board_features.h"
#include "ui_menu.h"

static void test_encoder_navigation_wraps(void)
{
    ui_menu_state_t state;

    ui_menu_init(&state);
    assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(ui_menu_selected_index(&state) == 1);
    assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT));
    assert(ui_menu_selected_index(&state) == 0);
    assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT));
    /* Settings, not UI_MENU_ITEM_COUNT - 1: it is the last row on screen even
     * in a build where the item above it does not exist. */
    assert(ui_menu_selected_index(&state) == UI_MENU_ITEM_SETTINGS);
}

static void test_activate_maps_current_source(void)
{
    ui_menu_state_t state;

    ui_menu_init(&state);
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_INTERNET_RADIO);
    /* Rows on screen rather than items in the enum: a built-out item is not
     * one of the steps. */
    for (int index = 0; index < (int)ui_menu_visible_count(true) - 1; ++index) {
        assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    }
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_NONE);
}

static void test_select_source_moves_menu_cursor(void)
{
    ui_menu_state_t state;

    ui_menu_init(&state);
    assert(ui_menu_select_source(&state, AUDIO_SOURCE_USB));
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_USB);
    assert(!ui_menu_select_source(&state, AUDIO_SOURCE_NONE));

#if BOARD_HAS_YANDEX_MUSIC
    /* Yandex Music is a real source now, so leaving the player has a row to
     * come back to - it used to map to AUDIO_SOURCE_NONE, which no row
     * matches. */
    assert(ui_menu_select_source(&state, AUDIO_SOURCE_YANDEX));
    assert(ui_menu_selected_index(&state) == UI_MENU_ITEM_YANDEX_MUSIC);
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_YANDEX);
#endif
}

static void test_settings_is_the_last_row_and_is_not_a_source(void)
{
    /* Settings shares AUDIO_SOURCE_NONE with the unimplemented sources, so the
     * only thing separating them is this predicate - without it, selecting
     * Settings would silently do nothing like selecting Bluetooth does. */
    ui_menu_state_t state;
    ui_menu_init(&state);
    assert(!ui_menu_selection_is_settings(&state));

    for (int index = 0; index < (int)ui_menu_visible_count(true) - 1; ++index) {
        (void)ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT);
    }
    assert(ui_menu_selected_index(&state) == UI_MENU_ITEM_SETTINGS);
    assert(ui_menu_selection_is_settings(&state));
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_NONE);
    assert(ui_menu_item_label(UI_MENU_ITEM_SETTINGS)[0] != '\0');

    /* One step back leaves a source row, so the sources stay contiguous. */
    (void)ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT);
    assert(!ui_menu_selection_is_settings(&state));

    assert(!ui_menu_selection_is_settings(NULL));
}

static void test_hiding_yandex_takes_its_row_out_of_the_run(void)
{
#if BOARD_HAS_YANDEX_MUSIC
    ui_menu_state_t state;
    ui_menu_init(&state);
    assert(ui_menu_yandex_visible(&state));
    assert(ui_menu_visible_count(true) == UI_MENU_ITEM_COUNT);

    ui_menu_set_yandex_visible(&state, false);
    assert(!ui_menu_yandex_visible(&state));
    assert(ui_menu_visible_count(false) == UI_MENU_ITEM_COUNT - 1U);
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, false));

    /* Settings moves up a row, and nothing above it does. The rows are what
     * the screen draws, so this is the whole of "the item disappeared". */
    assert(ui_menu_visible_item_at(UI_MENU_ITEM_YANDEX_MUSIC, false) == UI_MENU_ITEM_SETTINGS);
    assert(ui_menu_visible_position(UI_MENU_ITEM_SETTINGS, false) ==
           (uint8_t)UI_MENU_ITEM_YANDEX_MUSIC);
    assert(ui_menu_visible_position(UI_MENU_ITEM_DLNA, false) == (uint8_t)UI_MENU_ITEM_DLNA);

    /* The encoder steps over the gap rather than into it, in both directions. */
    (void)ui_menu_select_source(&state, AUDIO_SOURCE_DLNA);
    for (int index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
        assert(ui_menu_selected_index(&state) != UI_MENU_ITEM_YANDEX_MUSIC);
    }
    for (int index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT));
        assert(ui_menu_selected_index(&state) != UI_MENU_ITEM_YANDEX_MUSIC);
    }
    /* And a source with no row cannot draw the cursor onto one. */
    assert(!ui_menu_select_source(&state, AUDIO_SOURCE_YANDEX));

    /* The cursor cannot be left standing on a row that has just gone. */
    ui_menu_init(&state);
    assert(ui_menu_select_source(&state, AUDIO_SOURCE_YANDEX));
    ui_menu_set_yandex_visible(&state, false);
    assert(ui_menu_selected_index(&state) == UI_MENU_ITEM_INTERNET_RADIO);
#else
    /* Built out: the switch cannot bring it back. */
    ui_menu_state_t state;
    ui_menu_init(&state);
    ui_menu_set_yandex_visible(&state, true);
    assert(!ui_menu_yandex_visible(&state));
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, true));
    assert(ui_menu_visible_count(true) == UI_MENU_ITEM_COUNT - 1U);
#endif
}

int main(void)
{
    test_encoder_navigation_wraps();
    test_activate_maps_current_source();
    test_select_source_moves_menu_cursor();
    test_settings_is_the_last_row_and_is_not_a_source();
    test_hiding_yandex_takes_its_row_out_of_the_run();
    puts("ui_menu tests passed");
    return 0;
}
