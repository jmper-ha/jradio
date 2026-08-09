#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "board_input.h"

typedef struct {
    size_t count;
    size_t selected_index;
    size_t active_index;
} station_list_state_t;

void station_list_init(station_list_state_t *state, size_t count, size_t initial_index,
                       size_t active_index);
bool station_list_handle_input(station_list_state_t *state, board_input_action_t action);
size_t station_list_selected_index(const station_list_state_t *state);
size_t station_list_active_index(const station_list_state_t *state);
size_t station_list_window_start(const station_list_state_t *state, size_t visible_count);
bool station_list_selection_requires_switch(const station_list_state_t *state);
