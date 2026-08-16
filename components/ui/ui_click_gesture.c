#include "ui_click_gesture.h"

#include <stddef.h>

void ui_click_gesture_init(ui_click_gesture_t *gesture)
{
    if (gesture == NULL) return;
    gesture->pending = false;
    gesture->first_ms = 0U;
}

static bool window_expired(const ui_click_gesture_t *gesture, uint32_t now_ms)
{
    /* Unsigned difference, so this stays correct across the tick counter's
     * wrap - the UI clock is milliseconds in a uint32_t and wraps every
     * 49 days. */
    return (uint32_t)(now_ms - gesture->first_ms) >= UI_CLICK_DOUBLE_WINDOW_MS;
}

ui_click_result_t ui_click_gesture_press(ui_click_gesture_t *gesture, uint32_t now_ms)
{
    if (gesture == NULL) return UI_CLICK_NONE;
    if (gesture->pending && !window_expired(gesture, now_ms)) {
        gesture->pending = false;
        return UI_CLICK_DOUBLE;
    }
    /* Either the first press, or one so late that the previous click has
     * already been delivered as a single. Start a fresh window rather than
     * pairing presses that the user meant as separate. */
    gesture->pending = true;
    gesture->first_ms = now_ms;
    return UI_CLICK_NONE;
}

ui_click_result_t ui_click_gesture_poll(ui_click_gesture_t *gesture, uint32_t now_ms)
{
    if (gesture == NULL || !gesture->pending || !window_expired(gesture, now_ms)) {
        return UI_CLICK_NONE;
    }
    gesture->pending = false;
    return UI_CLICK_SINGLE;
}

void ui_click_gesture_cancel(ui_click_gesture_t *gesture)
{
    if (gesture != NULL) gesture->pending = false;
}

bool ui_click_gesture_is_pending(const ui_click_gesture_t *gesture)
{
    return gesture != NULL && gesture->pending;
}
