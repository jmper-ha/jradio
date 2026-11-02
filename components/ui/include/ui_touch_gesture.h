#pragma once

#include <stdbool.h>
#include <stdint.h>

/* What a finger on the glass means, in the two words the lists and the home
 * screens need: it touched something, or it dragged some number of rows. Pure,
 * given the point and the time on every pass; ui.c turns the answer into the
 * same actions the encoder sends, so no screen has a second way of being
 * worked. */

typedef enum {
    UI_TOUCH_AXIS_VERTICAL = 0,
    UI_TOUCH_AXIS_HORIZONTAL,
    // For a screen where each way means something: the finger's first move picks.
    UI_TOUCH_AXIS_FREE,
} ui_touch_axis_t;

typedef enum {
    UI_TOUCH_EVENT_NONE = 0,
    UI_TOUCH_EVENT_PRESSED,
    UI_TOUCH_EVENT_TAP,
    UI_TOUCH_EVENT_DRAG,
} ui_touch_event_kind_t;

typedef struct {
    ui_touch_event_kind_t kind;
    // TAP: where the finger came down, not where it left.
    int x;
    int y;
    // DRAG: whole steps since the last answer, positive along the axis.
    int steps;
    // DRAG: the finger has left and this is the list rolling on.
    bool coasting;
    // DRAG: which way those steps run.
    ui_touch_axis_t axis;
} ui_touch_event_t;

typedef struct {
    bool down;
    bool dragging;
    bool ignored;
    // This press stopped a rolling list, and is not a touch on what is under it.
    bool stopped_coast;
    bool coasting;
    int start_x;
    int start_y;
    int anchor;
    int last_along;
    // What the touch has covered in passes short enough to time it by.
    int seen_px;
    uint32_t seen_ms;
    // The newest such pass, not counted until another follows it: see the .c.
    int pending_px;
    uint32_t pending_ms;
    uint32_t start_ms;
    uint32_t last_ms;
    // Along the axis, in pixels a millisecond, while coasting.
    float velocity;
    float coast_px;
    int step_px;
    // The way this touch runs: the screen's, or what UI_TOUCH_AXIS_FREE settled on.
    ui_touch_axis_t axis;
} ui_touch_gesture_t;

void ui_touch_gesture_init(ui_touch_gesture_t *gesture);

/* One pass. `axis` and `step_px` belong to the screen under the finger. */
ui_touch_event_t ui_touch_gesture_step(ui_touch_gesture_t *gesture, bool down, int x, int y,
                                       uint32_t now_ms, ui_touch_axis_t axis, int step_px);

/* Which way this touch runs, for a screen whose step differs by direction: under
 * UI_TOUCH_AXIS_FREE it is vertical until the finger has moved enough to say. */
ui_touch_axis_t ui_touch_gesture_axis(const ui_touch_gesture_t *gesture);

/* The rest of this press means nothing: it only woke the screen. */
void ui_touch_gesture_ignore(ui_touch_gesture_t *gesture);

/* Stops a rolling list where it is. For a screen where rolling on means
 * nothing good: a number being edited, a menu with nothing to scroll. */
void ui_touch_gesture_stop_coast(ui_touch_gesture_t *gesture);

/* The row under `y`, counted from `first_y`, or -1 off either end. Rows are
 * hit by their pitch rather than their height, so the gap between two of them
 * belongs to one of them and a finger never lands on nothing. */
int ui_touch_row_at(int y, int first_y, int pitch, int rows);
