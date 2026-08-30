#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_features.h"
#include "ui_menu.h"

static void test_encoder_navigation_wraps(void)
{
    ui_menu_state_t state;

    ui_menu_init(&state);
    assert(ui_menu_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    /* The second row on screen, not item 1: which item that is depends on what
     * this board carries. */
    assert(ui_menu_selected_index(&state) == (uint8_t)ui_menu_visible_item_at(1U, true));
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
#if BOARD_HAS_USB
    assert(ui_menu_select_source(&state, AUDIO_SOURCE_USB));
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_USB);
#else
    /* No row for a part that is not on the board, so the cursor has nowhere to
     * go - which is also what stops the web from starting it. */
    assert(!ui_menu_select_source(&state, AUDIO_SOURCE_USB));
#endif
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

static void test_only_the_parts_this_build_has_get_a_row(void)
{
    /* The device must not offer what it cannot do. A source whose part is not
     * wired in board_options.h, or whose feature is FEATURE_OFF, has no row -
     * not a greyed one, none - because the press would land on nothing. */
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_INTERNET_RADIO, true));
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_SETTINGS, true));
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_USB_FILES, true) == (bool)BOARD_HAS_USB);
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_SD_CARD, true) == (bool)BOARD_HAS_SD_CARD);
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_BLUETOOTH, true) == (bool)BOARD_HAS_BLUETOOTH);
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_FM_RADIO, true) == (bool)BOARD_HAS_FM_RADIO);
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_DLNA, true) == (bool)BOARD_HAS_DLNA);
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, true) ==
           (bool)BOARD_HAS_YANDEX_MUSIC);
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_COUNT, true));

    /* Two always, plus whatever this build has. */
    const uint8_t expected = (uint8_t)(2 + BOARD_HAS_USB + BOARD_HAS_SD_CARD +
                                       BOARD_HAS_BLUETOOTH + BOARD_HAS_FM_RADIO +
                                       BOARD_HAS_DLNA + BOARD_HAS_YANDEX_MUSIC);
    assert(ui_menu_visible_count(true) == expected);

    /* Walking the rows only ever lands on items this build has, which is what
     * makes the row count above more than arithmetic. */
    for (uint8_t row = 0U; row < expected; ++row) {
        assert(ui_menu_item_is_visible(ui_menu_visible_item_at(row, true), true));
        assert(ui_menu_visible_position(ui_menu_visible_item_at(row, true), true) == row);
    }
}

static void test_a_two_row_home_screen_is_not_worth_showing(void)
{
    /* Internet radio and Settings are always there, so two rows means the
     * screen offers no choice at all - it costs a press to reach either of the
     * only two places there are. Such a build has no home screen and the long
     * press swaps between them directly.
     *
     * Both answers are checked from whichever build this happens to be: the
     * host cannot compile both boards at once, and a rule tested on one side
     * only is half a rule. */
    assert(!ui_menu_home_screen_needed(2U));
    assert(ui_menu_home_screen_needed(3U));
    assert(ui_menu_home_screen_needed(UI_MENU_ITEM_COUNT));
    /* Fewer than two cannot happen, and must not read as "show it" if it
     * somehow did. */
    assert(!ui_menu_home_screen_needed(0U));
    assert(!ui_menu_home_screen_needed(1U));
}

static void test_hiding_yandex_takes_its_row_out_of_the_run(void)
{
#if BOARD_HAS_YANDEX_MUSIC
    ui_menu_state_t state;
    ui_menu_init(&state);
    assert(ui_menu_yandex_visible(&state));
    const uint8_t shown = ui_menu_visible_count(true);
    /* The row Yandex Music occupies in this build, rather than its place in
     * the enum: the items before it are only there if the board carries
     * them. */
    const uint8_t row = ui_menu_visible_position(UI_MENU_ITEM_YANDEX_MUSIC, true);
    const ui_menu_item_t above = ui_menu_item_step(UI_MENU_ITEM_YANDEX_MUSIC, -1, true);

    ui_menu_set_yandex_visible(&state, false);
    assert(!ui_menu_yandex_visible(&state));
    assert(ui_menu_visible_count(false) == shown - 1U);
    assert(!ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, false));

    /* Settings moves up into the row, and nothing above it moves. The rows are
     * what the screen draws, so this is the whole of "the item disappeared". */
    assert(ui_menu_visible_item_at(row, false) == UI_MENU_ITEM_SETTINGS);
    assert(ui_menu_visible_position(UI_MENU_ITEM_SETTINGS, false) == row);
    assert(ui_menu_visible_position(above, false) == row - 1U);

    /* The encoder steps over the gap rather than into it, in both directions. */
    (void)ui_menu_select_source(&state, AUDIO_SOURCE_INTERNET_RADIO);
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
#endif
}

/* Visible and startable are different questions. A source that needs the
   network keeps its row when there is none - moving every row below it would
   be a worse answer than a dim one - but it cannot be started. */
static void test_network_sources_need_a_network(void)
{
    assert(!ui_menu_item_is_enabled(UI_MENU_ITEM_INTERNET_RADIO, true, false));
    assert(ui_menu_item_is_visible(UI_MENU_ITEM_INTERNET_RADIO, true));
    assert(ui_menu_item_is_enabled(UI_MENU_ITEM_INTERNET_RADIO, true, true));

    /* Settings is how the network gets set up again, so it is never the row
       that goes dim. */
    assert(ui_menu_item_is_enabled(UI_MENU_ITEM_SETTINGS, true, false));

    if (ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, true)) {
        assert(!ui_menu_item_is_enabled(UI_MENU_ITEM_YANDEX_MUSIC, true, false));
        assert(ui_menu_item_is_enabled(UI_MENU_ITEM_YANDEX_MUSIC, true, true));
        /* Switched off in Settings beats every other answer: there is no row. */
        assert(!ui_menu_item_is_enabled(UI_MENU_ITEM_YANDEX_MUSIC, false, true));
    }
    if (ui_menu_item_is_visible(UI_MENU_ITEM_USB_FILES, true)) {
        assert(ui_menu_item_is_enabled(UI_MENU_ITEM_USB_FILES, true, false));
    }
    if (ui_menu_item_is_visible(UI_MENU_ITEM_SD_CARD, true)) {
        assert(ui_menu_item_is_enabled(UI_MENU_ITEM_SD_CARD, true, false));
    }
}

int main(void)
{
    test_encoder_navigation_wraps();
    test_activate_maps_current_source();
    test_select_source_moves_menu_cursor();
    test_settings_is_the_last_row_and_is_not_a_source();
    test_only_the_parts_this_build_has_get_a_row();
    test_a_two_row_home_screen_is_not_worth_showing();
    test_hiding_yandex_takes_its_row_out_of_the_run();
    test_network_sources_need_a_network();
    puts("ui_menu tests passed");
    return 0;
}
