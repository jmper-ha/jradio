#include <assert.h>
#include <stdio.h>

#include "ui_station_list.h"

int main(void)
{
    station_list_state_t state;
    station_list_init(&state, 3, 0, 1);
    assert(station_list_selected_index(&state) == 0);
    assert(station_list_active_index(&state) == 1);
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT));
    assert(station_list_selected_index(&state) == 2);
    assert(station_list_active_index(&state) == 1);
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(station_list_selected_index(&state) == 0);
    assert(station_list_active_index(&state) == 1);
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_BUTTON));

    station_list_init(&state, 5, 3, 1);
    assert(station_list_selected_index(&state) == 3);
    assert(station_list_active_index(&state) == 1);

    station_list_init(&state, 3, 1, 1);
    assert(!station_list_selection_requires_switch(&state));
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(station_list_selection_requires_switch(&state));

    station_list_init(&state, 3, 5, 9);
    assert(station_list_selected_index(&state) == 0);
    assert(station_list_active_index(&state) == 3);

    station_list_init(&state, 12, 0, 0);
    assert(station_list_window_start(&state, 7) == 0);
    for (int index = 0; index < 8; ++index) {
        assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    }
    assert(station_list_selected_index(&state) == 8);
    assert(station_list_window_start(&state, 7) == 2);

    station_list_init(&state, 12, 11, 11);
    assert(station_list_window_start(&state, 7) == 5);
    assert(station_list_window_start(&state, 0) == 0);

    puts("ui_station_list tests passed");
    return 0;
}
