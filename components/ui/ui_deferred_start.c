#include "ui_deferred_start.h"

void ui_deferred_start_init(ui_deferred_start_t *state)
{
    if (state == NULL) return;
    state->pending = false;
    state->station_index = 0;
    state->due_ms = 0;
}

void ui_deferred_start_schedule(ui_deferred_start_t *state, size_t station_index,
                                uint32_t now_ms, uint32_t delay_ms)
{
    if (state == NULL) return;
    state->pending = true;
    state->station_index = station_index;
    state->due_ms = now_ms + delay_ms;
}

bool ui_deferred_start_is_pending(const ui_deferred_start_t *state)
{
    return state != NULL && state->pending;
}

bool ui_deferred_start_take_if_due(ui_deferred_start_t *state, uint32_t now_ms,
                                   size_t *station_index)
{
    if (state == NULL || station_index == NULL || !ui_deferred_start_is_pending(state) ||
        (int32_t)(now_ms - state->due_ms) < 0) {
        return false;
    }
    *station_index = state->station_index;
    state->pending = false;
    return true;
}

void ui_deferred_start_cancel(ui_deferred_start_t *state)
{
    if (state == NULL) return;
    state->pending = false;
}
