#include <assert.h>
#include <stdio.h>

#include "ui_station_list.h"

int main(void)
{
    station_list_state_t state;
    /* The cursor stops at both ends instead of wrapping, and reports "nothing
     * changed" so the screen is not redrawn for a no-op. */
    station_list_init(&state, 3, 0, 1);
    assert(station_list_selected_index(&state) == 0);
    assert(station_list_active_index(&state) == 1);
    assert(!station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT));
    assert(station_list_selected_index(&state) == 0);
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(station_list_selected_index(&state) == 1);
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(station_list_selected_index(&state) == 2);
    assert(!station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(station_list_selected_index(&state) == 2);
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

    /* The cursor is pinned to the middle row (row 3 of 7) and the window
     * slides under it, padding past both ends of the catalogue. */
    size_t cursor_row = 99U;
    station_list_init(&state, 12, 0, 0);
    assert(station_list_window_top(&state, 7, &cursor_row) == -3);
    assert(cursor_row == 3U);
    for (int index = 0; index < 8; ++index) {
        assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    }
    assert(station_list_selected_index(&state) == 8);
    assert(station_list_window_top(&state, 7, &cursor_row) == 5);
    assert(cursor_row == 3U);

    /* At the last entry the window runs past the end: rows 4..6 are padding. */
    station_list_init(&state, 12, 11, 11);
    assert(station_list_window_top(&state, 7, &cursor_row) == 8);

    /* The cursor row always lands on the selected entry: top + row == index. */
    station_list_init(&state, 12, 6, 0);
    assert(station_list_window_top(&state, 7, &cursor_row) + (int)cursor_row == 6);
    station_list_init(&state, 12, 0, 0);
    assert(station_list_window_top(&state, 7, &cursor_row) + (int)cursor_row == 0);

    /* An even row count puts the cursor just below centre, consistently. */
    station_list_init(&state, 12, 4, 0);
    assert(station_list_window_top(&state, 6, &cursor_row) == 1);
    assert(cursor_row == 3U);

    assert(station_list_window_top(&state, 0, &cursor_row) == 0);
    assert(station_list_window_top(NULL, 7, &cursor_row) == 0);
    assert(cursor_row == 3U);

    size_t selected_index = 99;
    station_list_init(&state, 0, 0, 0);
    assert(!station_list_get_selection(&state, &selected_index));
    assert(selected_index == 99);

    /* A playlist edited from the web UI shrinks the list under the cursor. */
    station_list_init(&state, 8, 7, 7);
    assert(station_list_selected_index(&state) == 7);
    assert(station_list_sync_counts(&state, 3, 1));
    assert(station_list_selected_index(&state) == 2);
    assert(station_list_active_index(&state) == 1);
    /* Re-clamped onto the new last entry, which is now also the end stop. */
    assert(!station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(station_list_selected_index(&state) == 2);
    assert(station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_LEFT));
    assert(station_list_selected_index(&state) == 1);

    /* No change reported when the snapshot still matches the cached counts. */
    assert(!station_list_sync_counts(&state, 3, 1));

    /* Growing the list keeps the cursor and re-clamps a vanished active entry. */
    station_list_init(&state, 3, 1, 1);
    assert(station_list_sync_counts(&state, 6, 9));
    assert(station_list_selected_index(&state) == 1);
    assert(station_list_active_index(&state) == 6);

    /* An emptied playlist parks the cursor at 0 and blocks navigation. */
    station_list_init(&state, 4, 3, 3);
    assert(station_list_sync_counts(&state, 0, 0));
    assert(station_list_selected_index(&state) == 0);
    assert(!station_list_handle_input(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT));
    assert(!station_list_get_selection(&state, &selected_index));

    station_list_init(&state, 3, 0, 1);
    station_list_note_activity(&state, 1000);
    assert(!station_list_idle_timeout_elapsed(&state, 1000 + 9999, 10000));
    assert(station_list_idle_timeout_elapsed(&state, 1000 + 10000, 10000));
    station_list_note_activity(&state, 1000 + 9999);
    assert(!station_list_idle_timeout_elapsed(&state, 1000 + 9999 + 9999, 10000));

    puts("ui_station_list tests passed");
    return 0;
}
