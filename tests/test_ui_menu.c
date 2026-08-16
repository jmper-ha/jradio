#include <assert.h>
#include <stdio.h>

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
    assert(ui_menu_selected_index(&state) == UI_MENU_ITEM_COUNT - 1);
}

static void test_activate_maps_current_source(void)
{
    ui_menu_state_t state;

    ui_menu_init(&state);
    assert(ui_menu_activate(&state) == AUDIO_SOURCE_INTERNET_RADIO);
    for (int index = 0; index < UI_MENU_ITEM_COUNT - 1; ++index) {
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
}

static void test_settings_is_the_last_row_and_is_not_a_source(void)
{
    /* Settings shares AUDIO_SOURCE_NONE with the unimplemented sources, so the
     * only thing separating them is this predicate - without it, selecting
     * Settings would silently do nothing like selecting Bluetooth does. */
    ui_menu_state_t state;
    ui_menu_init(&state);
    assert(!ui_menu_selection_is_settings(&state));

    for (int index = 0; index < UI_MENU_ITEM_COUNT - 1; ++index) {
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

int main(void)
{
    test_encoder_navigation_wraps();
    test_activate_maps_current_source();
    test_select_source_moves_menu_cursor();
    test_settings_is_the_last_row_and_is_not_a_source();
    puts("ui_menu tests passed");
    return 0;
}
