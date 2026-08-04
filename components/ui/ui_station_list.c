#include "ui_station_list.h"

void station_list_init(station_list_state_t *state, size_t count)
{
    if (state == NULL) return;
    state->count = count;
    state->selected_index = 0;
}

bool station_list_handle_input(station_list_state_t *state, board_input_action_t action)
{
    if (state == NULL || state->count == 0) return false;
    if (action == BOARD_INPUT_ACTION_ENCODER_LEFT) {
        state->selected_index = state->selected_index == 0 ? state->count - 1 : state->selected_index - 1;
        return true;
    }
    if (action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
        state->selected_index = (state->selected_index + 1) % state->count;
        return true;
    }
    return action == BOARD_INPUT_ACTION_ENCODER_BUTTON;
}

size_t station_list_selected_index(const station_list_state_t *state)
{
    return state == NULL ? 0 : state->selected_index;
}
