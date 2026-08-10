#include "ui_station_list.h"

void station_list_init(station_list_state_t *state, size_t count, size_t initial_index,
                       size_t active_index)
{
    if (state == NULL) return;
    state->count = count;
    state->selected_index = count > 0U && initial_index < count ? initial_index : 0U;
    state->active_index = active_index < count ? active_index : count;
    state->last_activity_ms = 0U;
}

bool station_list_sync_counts(station_list_state_t *state, size_t count, size_t active_index)
{
    if (state == NULL) return false;
    const size_t clamped_active = active_index < count ? active_index : count;
    if (state->count == count && state->active_index == clamped_active) {
        return false;
    }
    state->count = count;
    state->active_index = clamped_active;
    if (state->selected_index >= count) {
        // The list shrank under the cursor (the playlist was edited from the
        // web UI while this screen was open). Without re-clamping, the cursor
        // would keep wrapping over indices that no longer exist: those rows
        // render blank and pressing the encoder is silently rejected.
        state->selected_index = count > 0U ? count - 1U : 0U;
    }
    return true;
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

bool station_list_get_selection(const station_list_state_t *state,
                                size_t *selected_index)
{
    if (state == NULL || selected_index == NULL || state->count == 0U ||
        state->selected_index >= state->count) {
        return false;
    }
    *selected_index = state->selected_index;
    return true;
}

size_t station_list_active_index(const station_list_state_t *state)
{
    return state == NULL ? 0 : state->active_index;
}

size_t station_list_window_start(const station_list_state_t *state, size_t visible_count)
{
    if (state == NULL || state->count == 0U || visible_count == 0U ||
        state->count <= visible_count || state->selected_index < visible_count) {
        return 0U;
    }
    const size_t last_start = state->count - visible_count;
    const size_t selected_start = state->selected_index - visible_count + 1U;
    return selected_start < last_start ? selected_start : last_start;
}

bool station_list_selection_requires_switch(const station_list_state_t *state)
{
    return state != NULL && state->selected_index != state->active_index;
}

void station_list_note_activity(station_list_state_t *state, uint32_t now_ms)
{
    if (state == NULL) return;
    state->last_activity_ms = now_ms;
}

bool station_list_idle_timeout_elapsed(const station_list_state_t *state, uint32_t now_ms,
                                       uint32_t timeout_ms)
{
    if (state == NULL) return false;
    return (uint32_t)(now_ms - state->last_activity_ms) >= timeout_ms;
}
