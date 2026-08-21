#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Counts the presses of one button into a single, double or triple click.
 *
 * This lives in the UI rather than in board_input on purpose. Recognising a
 * multiple click means a single one cannot be acted on until the window for a
 * further press has passed, and that delay is only worth paying where the
 * longer gestures actually mean something - the player screen. The menu, the
 * lists and the settings screen keep acting on the press immediately, which is
 * what makes them feel responsive.
 *
 * Whether a third press is worth waiting for is the caller's to say, per
 * gesture, and it costs something either way. Where triple is off, the double
 * is reported the instant the second press lands. Where it is on, the double
 * has to wait out one more window to prove it was not the start of a triple -
 * so it is only turned on where a triple click has a meaning, and the action
 * it delays is the least time-sensitive one on the screen.
 */

/* Longest gap between two presses of one gesture. Measured from the previous
 * press, not from the first, so a triple click has a whole window per gap
 * rather than having to fit three presses into one. */
#define UI_CLICK_WINDOW_MS 350U

typedef enum {
    UI_CLICK_NONE = 0,
    UI_CLICK_SINGLE,
    UI_CLICK_DOUBLE,
    UI_CLICK_TRIPLE,
} ui_click_result_t;

typedef struct {
    /* Presses seen in the window that is still open; 0 when nothing is
     * pending. */
    uint8_t count;
    /* Latched from the press that opened the window, so the answer cannot
     * change kind halfway through a gesture the user has already started. */
    bool allow_triple;
    uint32_t last_ms;
} ui_click_gesture_t;

void ui_click_gesture_init(ui_click_gesture_t *gesture);

/* Feed every button press here. Answers TRIPLE at once on the third press, and
 * DOUBLE at once on the second where `allow_triple` is false; otherwise NONE
 * while the gesture is still open. */
ui_click_result_t ui_click_gesture_press(ui_click_gesture_t *gesture, uint32_t now_ms,
                                         bool allow_triple);

/* Call from the poll loop. Answers whatever the gesture turned out to be once
 * the window has passed with no further press. */
ui_click_result_t ui_click_gesture_poll(ui_click_gesture_t *gesture, uint32_t now_ms);

/* Drops a pending click. Needed whenever the screen changes under it, or the
 * click would fire on whatever replaced it. */
void ui_click_gesture_cancel(ui_click_gesture_t *gesture);

bool ui_click_gesture_is_pending(const ui_click_gesture_t *gesture);
