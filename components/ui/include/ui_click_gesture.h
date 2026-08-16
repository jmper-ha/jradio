#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Tells a single click from a double click on one button.
 *
 * This lives in the UI rather than in board_input on purpose. Recognising a
 * double click means a single one cannot be acted on until the window for a
 * second press has passed, and that delay is only worth paying where double
 * click actually means something - the player screen. The menu, the lists and
 * the settings screen keep acting on the press immediately, which is what
 * makes them feel responsive.
 *
 * The double click is reported the instant the second press arrives; only the
 * single one waits. So the slower answer is the one whose action - play/pause
 * - is least sensitive to a fraction of a second.
 */

#define UI_CLICK_DOUBLE_WINDOW_MS 350U

typedef enum {
    UI_CLICK_NONE = 0,
    UI_CLICK_SINGLE,
    UI_CLICK_DOUBLE,
} ui_click_result_t;

typedef struct {
    bool pending;
    uint32_t first_ms;
} ui_click_gesture_t;

void ui_click_gesture_init(ui_click_gesture_t *gesture);

/* Feed every button press here. Answers DOUBLE at once when this press closes
 * a double click, otherwise NONE while the click is held pending. */
ui_click_result_t ui_click_gesture_press(ui_click_gesture_t *gesture, uint32_t now_ms);

/* Call from the poll loop. Answers SINGLE once the window has passed with no
 * second press. */
ui_click_result_t ui_click_gesture_poll(ui_click_gesture_t *gesture, uint32_t now_ms);

/* Drops a pending click. Needed whenever the screen changes under it, or the
 * click would fire on whatever replaced it. */
void ui_click_gesture_cancel(ui_click_gesture_t *gesture);

bool ui_click_gesture_is_pending(const ui_click_gesture_t *gesture);
