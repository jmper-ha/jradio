#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool pending;
    size_t station_index;
    uint32_t due_ms;
} ui_deferred_start_t;

void ui_deferred_start_init(ui_deferred_start_t *state);
void ui_deferred_start_schedule(ui_deferred_start_t *state, size_t station_index,
                                uint32_t now_ms, uint32_t delay_ms);
bool ui_deferred_start_is_pending(const ui_deferred_start_t *state);
bool ui_deferred_start_take_if_due(ui_deferred_start_t *state, uint32_t now_ms,
                                   size_t *station_index);
void ui_deferred_start_cancel(ui_deferred_start_t *state);
