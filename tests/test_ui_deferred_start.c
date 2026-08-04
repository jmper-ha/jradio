#include <assert.h>
#include <stdio.h>

#include "ui_deferred_start.h"

int main(void)
{
    ui_deferred_start_t state;
    size_t station_index = 0;

    ui_deferred_start_init(&state);
    assert(!ui_deferred_start_is_pending(&state));
    ui_deferred_start_schedule(&state, 3, 1000, 100);
    assert(ui_deferred_start_is_pending(&state));
    assert(!ui_deferred_start_take_if_due(&state, 1099, &station_index));
    assert(ui_deferred_start_take_if_due(&state, 1100, &station_index));
    assert(station_index == 3);
    assert(!ui_deferred_start_is_pending(&state));
    assert(!ui_deferred_start_take_if_due(&state, 1100, &station_index));

    ui_deferred_start_schedule(&state, 1, 2000, 100);
    ui_deferred_start_cancel(&state);
    assert(!ui_deferred_start_is_pending(&state));
    assert(!ui_deferred_start_take_if_due(&state, 2100, &station_index));

    puts("ui_deferred_start tests passed");
    return 0;
}
