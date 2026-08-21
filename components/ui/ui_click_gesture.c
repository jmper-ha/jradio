#include "ui_click_gesture.h"

#include <stddef.h>

void ui_click_gesture_init(ui_click_gesture_t *gesture)
{
    if (gesture == NULL) return;
    gesture->count = 0U;
    gesture->allow_triple = false;
    gesture->last_ms = 0U;
}

static bool window_expired(const ui_click_gesture_t *gesture, uint32_t now_ms)
{
    /* Unsigned difference, so this stays correct across the tick counter's
     * wrap - the UI clock is milliseconds in a uint32_t and wraps every
     * 49 days. */
    return (uint32_t)(now_ms - gesture->last_ms) >= UI_CLICK_WINDOW_MS;
}

static ui_click_result_t take(ui_click_gesture_t *gesture)
{
    const uint8_t count = gesture->count;
    gesture->count = 0U;
    switch (count) {
    case 1U: return UI_CLICK_SINGLE;
    case 2U: return UI_CLICK_DOUBLE;
    case 3U: return UI_CLICK_TRIPLE;
    default: return UI_CLICK_NONE;
    }
}

ui_click_result_t ui_click_gesture_press(ui_click_gesture_t *gesture, uint32_t now_ms,
                                         bool allow_triple)
{
    if (gesture == NULL) return UI_CLICK_NONE;
    if (gesture->count == 0U || window_expired(gesture, now_ms)) {
        /* Either the first press, or one so late that the previous gesture has
         * already been delivered. Start a fresh window rather than joining
         * presses the user meant as separate. */
        gesture->count = 1U;
        gesture->allow_triple = allow_triple;
        gesture->last_ms = now_ms;
        return UI_CLICK_NONE;
    }
    ++gesture->count;
    gesture->last_ms = now_ms;
    /* Answered without waiting once no further press could change the reading:
     * the third is the longest gesture there is, and the second is the longest
     * this window was opened for. */
    if (gesture->count >= 3U || !gesture->allow_triple) return take(gesture);
    return UI_CLICK_NONE;
}

ui_click_result_t ui_click_gesture_poll(ui_click_gesture_t *gesture, uint32_t now_ms)
{
    if (gesture == NULL || gesture->count == 0U || !window_expired(gesture, now_ms)) {
        return UI_CLICK_NONE;
    }
    return take(gesture);
}

void ui_click_gesture_cancel(ui_click_gesture_t *gesture)
{
    if (gesture != NULL) gesture->count = 0U;
}

bool ui_click_gesture_is_pending(const ui_click_gesture_t *gesture)
{
    return gesture != NULL && gesture->count > 0U;
}
