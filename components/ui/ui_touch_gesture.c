#include "ui_touch_gesture.h"

#include <stddef.h>
#include <stdlib.h>

/* A finger coming down wanders: twelve presses aimed at one 20 px icon on the
 * bench's ST7796S landed over 44 px of height, though each one held still once
 * it was down. Below this it is still a touch. */
#define UI_TOUCH_DRAG_SLOP_PX 16
/* Held longer than this it was not a touch on a row. Left unanswered rather
 * than treated as one: a hand resting on the glass must not start a station. */
#define UI_TOUCH_TAP_MAX_MS 800U
/* The player screen's edges and its volume strip. A finger aimed at a mark lands
 * anywhere over 44 px, so a band much narrower would need aiming at. */
#define UI_TOUCH_PLAYER_EDGE_PX 36
/* Wider at the top, where nothing else wants an up-and-down: a pull started just
 * below the edge missed it on the bench, at y = 45 and 75 on the portrait panel. */
#define UI_TOUCH_PLAYER_TOP_PX 80
#define UI_TOUCH_PLAYER_VOLUME_W 70

/* What a flick is, and how the list rolls on from one.
 *
 * Not the speed at the lift: a finger leaving the glass rolls off its contact,
 * and the last points before it jumped by up to 85 px on the bench, so a row
 * brought carefully into place and let go read as a flick and rolled past. So
 * the touch has to be over quickly, which leaves the aiming alone however it
 * ends, and the newest point is never counted.
 *
 * Not the speed over the whole touch either: the first row it moves is a repaint
 * of 200 ms during which nothing is read, so the finger's distance was divided
 * by time it was not watched for and not one flick of nineteen on the bench's
 * list reached the lower figure. Only passes short enough to time it by count.
 *
 * How far it rolls is found by hand on the list of stations, where a row is
 * 35 px: 2 px/ms at 450 ms ran eight rows a frame, far too much. */
#define UI_TOUCH_FLICK_MAX_MS 500U
#define UI_TOUCH_FLICK_PASS_MAX_MS 60U
#define UI_TOUCH_COAST_MIN_PX_MS 0.4f
#define UI_TOUCH_COAST_MAX_PX_MS 1.0f
#define UI_TOUCH_COAST_STOP_PX_MS 0.03f
// The time the speed takes to fall by about two thirds.
#define UI_TOUCH_COAST_DECAY_MS 450.0f
// A stall in the task should not read as a list that rolled on through it.
#define UI_TOUCH_COAST_DT_MAX_MS 300U

void ui_touch_gesture_init(ui_touch_gesture_t *gesture)
{
    if (gesture == NULL) return;
    *gesture = (ui_touch_gesture_t){0};
}

static ui_touch_event_t coast(ui_touch_gesture_t *gesture, uint32_t now_ms)
{
    ui_touch_event_t event = {.kind = UI_TOUCH_EVENT_NONE};
    uint32_t elapsed = now_ms - gesture->last_ms;
    if (elapsed > UI_TOUCH_COAST_DT_MAX_MS) elapsed = UI_TOUCH_COAST_DT_MAX_MS;
    gesture->last_ms = now_ms;
    /* In this form because a pass here is a repaint, 200 ms and more, and the
     * plain v -= v * dt / decay lost most of the roll to that coarse a step: one
     * row a flick on the bench. Over any run of passes this one adds up to the
     * speed times the decay, however long they take. */
    gesture->velocity *= UI_TOUCH_COAST_DECAY_MS / (UI_TOUCH_COAST_DECAY_MS + (float)elapsed);
    gesture->coast_px += gesture->velocity * (float)elapsed;
    const int steps = (int)(gesture->coast_px / (float)gesture->step_px);
    if (steps != 0) {
        gesture->coast_px -= (float)(steps * gesture->step_px);
        event.kind = UI_TOUCH_EVENT_DRAG;
        event.steps = steps;
        event.coasting = true;
        event.axis = gesture->axis;
    }
    const float speed = gesture->velocity < 0.0f ? -gesture->velocity : gesture->velocity;
    if (speed < UI_TOUCH_COAST_STOP_PX_MS) ui_touch_gesture_init(gesture);
    return event;
}

ui_touch_event_t ui_touch_gesture_step(ui_touch_gesture_t *gesture, bool down, int x, int y,
                                       uint32_t now_ms, ui_touch_axis_t axis, int step_px)
{
    ui_touch_event_t event = {.kind = UI_TOUCH_EVENT_NONE};
    if (gesture == NULL) return event;
    if (!down) {
        if (gesture->coasting) return coast(gesture, now_ms);
        if (gesture->down && !gesture->ignored && !gesture->dragging &&
            !gesture->stopped_coast &&
            (uint32_t)(now_ms - gesture->start_ms) <= UI_TOUCH_TAP_MAX_MS) {
            event.kind = UI_TOUCH_EVENT_TAP;
            event.x = gesture->start_x;
            event.y = gesture->start_y;
        }
        const uint32_t held_ms = now_ms - gesture->start_ms;
        const bool flicked = gesture->down && gesture->dragging && !gesture->ignored &&
                             step_px > 0 && gesture->seen_ms > 0U &&
                             held_ms <= UI_TOUCH_FLICK_MAX_MS;
        const float seen =
            flicked ? (float)gesture->seen_px / (float)gesture->seen_ms : 0.0f;
        const float speed = seen < 0.0f ? -seen : seen;
        const ui_touch_axis_t flick_axis = gesture->axis;
        ui_touch_gesture_init(gesture);
        if (flicked && speed >= UI_TOUCH_COAST_MIN_PX_MS) {
            gesture->coasting = true;
            gesture->velocity = seen > UI_TOUCH_COAST_MAX_PX_MS    ? UI_TOUCH_COAST_MAX_PX_MS
                                : seen < -UI_TOUCH_COAST_MAX_PX_MS ? -UI_TOUCH_COAST_MAX_PX_MS
                                                                   : seen;
            gesture->last_ms = now_ms;
            gesture->step_px = step_px;
            gesture->axis = flick_axis;
        }
        return event;
    }
    if (!gesture->down) {
        const bool stopped = gesture->coasting;
        ui_touch_gesture_init(gesture);
        gesture->down = true;
        gesture->stopped_coast = stopped;
        gesture->start_x = x;
        gesture->start_y = y;
        gesture->start_ms = now_ms;
        gesture->axis = axis == UI_TOUCH_AXIS_HORIZONTAL ? axis : UI_TOUCH_AXIS_VERTICAL;
        gesture->last_along = gesture->axis == UI_TOUCH_AXIS_HORIZONTAL ? x : y;
        gesture->last_ms = now_ms;
        event.kind = UI_TOUCH_EVENT_PRESSED;
        return event;
    }
    if (gesture->ignored) return event;
    if (axis != UI_TOUCH_AXIS_FREE) gesture->axis = axis;
    const int along = gesture->axis == UI_TOUCH_AXIS_HORIZONTAL ? x : y;
    const uint32_t elapsed = now_ms - gesture->last_ms;
    if (elapsed > 0U) {
        gesture->seen_px += gesture->pending_px;
        gesture->seen_ms += gesture->pending_ms;
        const bool timed = elapsed <= UI_TOUCH_FLICK_PASS_MAX_MS;
        gesture->pending_px = timed ? along - gesture->last_along : 0;
        gesture->pending_ms = timed ? elapsed : 0U;
        gesture->last_along = along;
        gesture->last_ms = now_ms;
    }
    if (!gesture->dragging) {
        if (abs(x - gesture->start_x) < UI_TOUCH_DRAG_SLOP_PX &&
            abs(y - gesture->start_y) < UI_TOUCH_DRAG_SLOP_PX) {
            if (!gesture->stopped_coast &&
                (uint32_t)(now_ms - gesture->start_ms) >= UI_TOUCH_TAP_MAX_MS) {
                gesture->ignored = true;
                event.kind = UI_TOUCH_EVENT_LONG;
            }
            return event;
        }
        if (axis == UI_TOUCH_AXIS_FREE &&
            abs(x - gesture->start_x) > abs(y - gesture->start_y)) {
            gesture->axis = UI_TOUCH_AXIS_HORIZONTAL;
        }
        // From here, or the slop itself would count as the first bit of a row.
        gesture->dragging = true;
        gesture->anchor = gesture->axis == UI_TOUCH_AXIS_HORIZONTAL ? x : y;
        return event;
    }
    if (step_px <= 0) return event;
    const int steps = (along - gesture->anchor) / step_px;
    if (steps != 0) {
        gesture->anchor += steps * step_px;
        event.kind = UI_TOUCH_EVENT_DRAG;
        event.steps = steps;
        event.axis = gesture->axis;
    }
    return event;
}

ui_touch_axis_t ui_touch_gesture_axis(const ui_touch_gesture_t *gesture)
{
    return gesture != NULL ? gesture->axis : UI_TOUCH_AXIS_VERTICAL;
}

void ui_touch_gesture_ignore(ui_touch_gesture_t *gesture)
{
    if (gesture != NULL) gesture->ignored = true;
}

void ui_touch_gesture_stop_coast(ui_touch_gesture_t *gesture)
{
    if (gesture != NULL && gesture->coasting) ui_touch_gesture_init(gesture);
}

ui_touch_player_drag_t ui_touch_player_drag(int start_x, int start_y, ui_touch_axis_t axis,
                                            int width, int height)
{
    if (axis == UI_TOUCH_AXIS_HORIZONTAL) return UI_TOUCH_PLAYER_SKIP;
    if (start_y < UI_TOUCH_PLAYER_TOP_PX) return UI_TOUCH_PLAYER_QUICK_PANEL;
    if (start_y >= height - UI_TOUCH_PLAYER_EDGE_PX) return UI_TOUCH_PLAYER_ACTION_STRIP;
    return start_x >= width - UI_TOUCH_PLAYER_VOLUME_W ? UI_TOUCH_PLAYER_VOLUME
                                                       : UI_TOUCH_PLAYER_NOTHING;
}

bool ui_touch_player_tap_toggles(int x, int y, int width, int height)
{
    return x >= width / 4 && x < width - width / 4 && y >= height / 4 &&
           y < height - height / 4;
}

ui_touch_axis_t ui_touch_player_axis(int start_y, int height)
{
    return start_y < UI_TOUCH_PLAYER_EDGE_PX || start_y >= height - UI_TOUCH_PLAYER_EDGE_PX
               ? UI_TOUCH_AXIS_VERTICAL
               : UI_TOUCH_AXIS_FREE;
}

int ui_touch_scrollbar_item(int y, int track_y, int track_h, int count)
{
    if (track_h <= 0 || count <= 0 || y < track_y) return 0;
    const int item = (y - track_y) * count / track_h;
    return item < count ? item : count - 1;
}

unsigned ui_touch_strip_items(bool has_list, bool can_seek, bool can_tune, bool can_rate,
                              ui_touch_strip_item_t items[UI_TOUCH_STRIP_MAX_ITEMS])
{
    unsigned count = 0U;
    if (has_list) items[count++] = UI_TOUCH_STRIP_LIST;
    if (can_seek) items[count++] = UI_TOUCH_STRIP_SEEK;
    if (can_tune) items[count++] = UI_TOUCH_STRIP_TUNE;
    if (can_rate) {
        items[count++] = UI_TOUCH_STRIP_LIKE;
        items[count++] = UI_TOUCH_STRIP_DISLIKE;
    }
    // There always is a way out, and it stands last: furthest from a slip.
    if (count < UI_TOUCH_STRIP_MAX_ITEMS) items[count++] = UI_TOUCH_STRIP_EXIT;
    return count;
}

int ui_touch_strip_cell(int x, int width, unsigned count)
{
    if (width <= 0 || count == 0U || x < 0 || x >= width) return -1;
    return x * (int)count / width;
}

int ui_touch_row_at(int y, int first_y, int pitch, int rows)
{
    if (pitch <= 0 || y < first_y) return -1;
    const int row = (y - first_y) / pitch;
    return row < rows ? row : -1;
}
