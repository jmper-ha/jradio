#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_input.h"

typedef struct {
    size_t count;
    size_t selected_index;
    size_t active_index;
    uint32_t last_activity_ms;
} station_list_state_t;

void station_list_init(station_list_state_t *state, size_t count, size_t initial_index,
                       size_t active_index);
bool station_list_handle_input(station_list_state_t *state, board_input_action_t action);
size_t station_list_selected_index(const station_list_state_t *state);
bool station_list_get_selection(const station_list_state_t *state,
                                size_t *selected_index);
size_t station_list_active_index(const station_list_state_t *state);
/* Station index shown on the topmost visible row. The cursor is pinned to the
 * middle row and the list scrolls under it, so this is negative near the start
 * of the catalogue and runs past the last entry near its end: those rows are
 * padding and have no station. Returns the row the cursor sits on via
 * *cursor_row. */
int station_list_window_top(const station_list_state_t *state, size_t visible_count,
                            size_t *cursor_row);
bool station_list_selection_requires_switch(const station_list_state_t *state);
/* Re-syncs the cached station count/active entry with a fresh player snapshot
 * and re-clamps the cursor if the list shrank. Returns true when something
 * changed and the screen needs a redraw. */
bool station_list_sync_counts(station_list_state_t *state, size_t count, size_t active_index);
/* Cursor position as a percentage, for the scroll bar under the list. The
 * edge cases are the point of having this apart from the caller: an empty list
 * has no position at all, and a list that fits on screen would divide by zero
 * on (count - 1). Both answer 100 - a bar that cannot move should look filled
 * rather than empty, which would read as "you are at the top of something
 * longer". */
uint8_t station_list_progress_percent(const station_list_state_t *state);

/* Where the cursor belongs when a listing is (re)opened. `active_index` is the
 * row that is playing, or `count` when nothing in this listing is. On USB the
 * first row is ".." rather than an entry, and landing there made every freshly
 * opened directory start with the cursor on the way out of it - so with
 * nothing playing the cursor goes to `first_entry_row`, which callers set to 0
 * when the list has no parent row. A directory holding only ".." has no entry
 * to prefer and falls back to 0. */
size_t station_list_initial_index(size_t count, size_t active_index,
                                  size_t first_entry_row);

void station_list_note_activity(station_list_state_t *state, uint32_t now_ms);
bool station_list_idle_timeout_elapsed(const station_list_state_t *state, uint32_t now_ms,
                                       uint32_t timeout_ms);
