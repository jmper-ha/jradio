#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Dialling a station's number on a remote's digit keys.
 *
 * Stations are numbered from 1 as the screen shows them, up to 99. One digit
 * could be the whole number or the first of two, so a single digit waits
 * UI_DIAL_WAIT_MS for a second before it is taken as it is; a second digit
 * completes the number at once. A zero on its own means nothing and is
 * dropped, and a number past the end of the list is dropped too - the caller
 * says how long the list is. Pure, and tested on the host. */

#define UI_DIAL_WAIT_MS 1500U
#define UI_DIAL_MAX 99U

typedef struct {
    uint8_t first;     /* the digit waiting for a second, 0 when none */
    bool pending;
    uint32_t since_ms;
} ui_station_dial_t;

void ui_station_dial_init(ui_station_dial_t *dial);

/* A digit pressed. Returns the station number when the press completes one,
 * 0 while more is awaited or the digit was dropped. */
unsigned ui_station_dial_press(ui_station_dial_t *dial, unsigned digit, unsigned station_count,
                               uint32_t now_ms);
/* The wait running out. Returns the number the single digit stood for, 0 when
 * nothing was pending or it has not been long enough. */
unsigned ui_station_dial_poll(ui_station_dial_t *dial, unsigned station_count, uint32_t now_ms);
/* What is being dialled, for the screen: "2_" while a second digit is awaited,
 * "" otherwise. */
void ui_station_dial_text(const ui_station_dial_t *dial, char *out, unsigned size);
