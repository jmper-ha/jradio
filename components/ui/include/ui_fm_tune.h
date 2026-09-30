#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tuning the FM receiver by hand.
 *
 * A mode, like scrubbing a file: a triple click opens it, the knob moves the
 * frequency in 100 kHz steps, a press keeps where it is and hands the knob
 * back to the volume, and any other key puts the tuner back where the mode
 * began - a mode with only one way out is a trap, and a way out that loses
 * the station you had is not one either. It closes by itself, keeping the
 * frequency, once the knob has been left alone for a while.
 *
 * The tuner follows as the knob turns, so what is heard is where the digits
 * are. A fast turn would post a command per click and fill the player's
 * queue, so the frequency goes out at most every UI_FM_TUNE_SEND_MS, and
 * always the latest one. Pure: the clock is passed in. */

#define UI_FM_TUNE_STEP_KHZ 100U
#define UI_FM_TUNE_MIN_KHZ 87000U
#define UI_FM_TUNE_MAX_KHZ 108000U
#define UI_FM_TUNE_SEND_MS 80U
#define UI_FM_TUNE_IDLE_MS 10000U

typedef struct {
    bool active;
    uint32_t start_khz;
    uint32_t khz;
    uint32_t sent_khz;
    uint32_t sent_ms;
    uint32_t touched_ms;
} ui_fm_tune_t;

void ui_fm_tune_reset(ui_fm_tune_t *tune);

/* Opens the mode on the frequency playing. False with none to start from. */
bool ui_fm_tune_begin(ui_fm_tune_t *tune, uint32_t khz, uint32_t now_ms);

/* One click of the knob, positive upwards. True when the frequency moved -
 * not at the band's ends. */
bool ui_fm_tune_move(ui_fm_tune_t *tune, int direction, uint32_t now_ms);

/* Where the tuner went by itself - a seek asked for from inside the mode -
 * so the digits and the next click start from there. Counts as activity. */
void ui_fm_tune_follow(ui_fm_tune_t *tune, uint32_t khz, uint32_t now_ms);

/* The frequency to send now, if one is waiting and the last went out long
 * enough ago; it is then counted as sent. */
bool ui_fm_tune_due(ui_fm_tune_t *tune, uint32_t now_ms, uint32_t *khz);

/* Closes the mode. Keeping answers the frequency reached; cancelling the one
 * it began on. Either is what the tuner must be told last. */
uint32_t ui_fm_tune_keep(ui_fm_tune_t *tune);
uint32_t ui_fm_tune_cancel(ui_fm_tune_t *tune);

/* Left alone long enough to close by itself. */
bool ui_fm_tune_idle(const ui_fm_tune_t *tune, uint32_t now_ms);

/* Whether choosing FM opens the mode by itself: with no presets there is no
 * list to step along and nothing for the keys to switch between, so tuning by
 * hand is the only way to use the receiver - left to be found by a triple
 * click, a newcomer hears noise on the band's edge and a list with no rows. */
bool ui_fm_tune_opens_on_entry(size_t preset_count);

bool ui_fm_tune_is_active(const ui_fm_tune_t *tune);
uint32_t ui_fm_tune_khz(const ui_fm_tune_t *tune);
