#include "ui_station_dial.h"

#include <stddef.h>
#include <stdio.h>

void ui_station_dial_init(ui_station_dial_t *dial)
{
    if (dial == NULL) return;
    dial->first = 0U;
    dial->pending = false;
    dial->since_ms = 0U;
}

static unsigned settle(ui_station_dial_t *dial, unsigned number, unsigned station_count)
{
    dial->pending = false;
    dial->first = 0U;
    if (number == 0U || number > station_count || number > UI_DIAL_MAX) return 0U;
    return number;
}

unsigned ui_station_dial_press(ui_station_dial_t *dial, unsigned digit, unsigned station_count,
                               uint32_t now_ms)
{
    if (dial == NULL || digit > 9U) return 0U;
    if (dial->pending) {
        return settle(dial, (unsigned)dial->first * 10U + digit, station_count);
    }
    /* A leading zero is nothing - there is no station 0 and no "07". */
    if (digit == 0U) return 0U;
    /* A first digit that cannot start any station number in the list is the
     * whole number already: with twelve stations "3" is station 3 and a second
     * digit could only make it 30-something. */
    if (digit * 10U > station_count) {
        dial->pending = false;
        return settle(dial, digit, station_count);
    }
    dial->first = (uint8_t)digit;
    dial->pending = true;
    dial->since_ms = now_ms;
    return 0U;
}

unsigned ui_station_dial_poll(ui_station_dial_t *dial, unsigned station_count, uint32_t now_ms)
{
    if (dial == NULL || !dial->pending) return 0U;
    if ((uint32_t)(now_ms - dial->since_ms) < UI_DIAL_WAIT_MS) return 0U;
    return settle(dial, dial->first, station_count);
}

void ui_station_dial_text(const ui_station_dial_t *dial, char *out, unsigned size)
{
    if (out == NULL || size == 0U) return;
    if (dial == NULL || !dial->pending) {
        out[0] = '\0';
        return;
    }
    snprintf(out, size, "%u_", (unsigned)dial->first);
}
