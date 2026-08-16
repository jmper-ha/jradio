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
    // The list scrolls under a fixed cursor and deliberately does not wrap:
    // running off either end just stops, so the ends stay distinguishable.
    if (action == BOARD_INPUT_ACTION_ENCODER_LEFT) {
        if (state->selected_index == 0U) return false;
        --state->selected_index;
        return true;
    }
    if (action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
        if (state->selected_index + 1U >= state->count) return false;
        ++state->selected_index;
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

int station_list_window_top(const station_list_state_t *state, size_t visible_count,
                            size_t *cursor_row)
{
    const size_t middle = visible_count / 2U;
    if (cursor_row != NULL) {
        *cursor_row = middle;
    }
    if (state == NULL || visible_count == 0U) {
        return 0;
    }
    return (int)state->selected_index - (int)middle;
}

bool station_list_selection_requires_switch(const station_list_state_t *state)
{
    return state != NULL && state->selected_index != state->active_index;
}

uint8_t station_list_progress_percent(const station_list_state_t *state)
{
    if (state == NULL || state->count == 0U) return 100U;
    if (state->count == 1U) return 100U;
    const size_t last = state->count - 1U;
    const size_t index = state->selected_index > last ? last : state->selected_index;
    return (uint8_t)((index * 100U) / last);
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
