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

int main(void)
{
    test_encoder_navigation_wraps();
    test_activate_maps_current_source();
    test_select_source_moves_menu_cursor();
    puts("ui_menu tests passed");
    return 0;
}
