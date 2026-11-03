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
    // Held still long enough: what holding the encoder down means, on each screen.
    UI_TOUCH_EVENT_LONG,
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

/* What a drag on the player screen is, by where it started and which way it
 * went. The top and bottom edges win over the rest: a panel has to be reachable
 * in the corner as well, and the volume strip runs along the right side. */
typedef enum {
    UI_TOUCH_PLAYER_NOTHING = 0,
    UI_TOUCH_PLAYER_SKIP,
    UI_TOUCH_PLAYER_VOLUME,
    UI_TOUCH_PLAYER_QUICK_PANEL,
    UI_TOUCH_PLAYER_ACTION_STRIP,
} ui_touch_player_drag_t;

ui_touch_player_drag_t ui_touch_player_drag(int start_x, int start_y, ui_touch_axis_t axis,
                                            int width, int height);

/* The way to read a touch on the player. From an edge it can only be a pull: the
 * glass reports y = 0 there until the finger is well on it, so what moved first
 * was x, and seven pulls of nine read as a swipe sideways on the bench. */
ui_touch_axis_t ui_touch_player_axis(int start_y, int height);

/* Whether a tap at this point is the pause: only in the middle of the screen, so
 * a hand reaching for an edge or the volume does not stop the music. */
bool ui_touch_player_tap_toggles(int x, int y, int width, int height);

/* The item a point on a scrollbar stands for, of `count`. Off either end it is
 * the end: a finger working a bar does not stay on 4 px of it. */
int ui_touch_scrollbar_item(int y, int track_y, int track_h, int count);

/* The action strip under the player, which gathers what a finger has no gesture
 * for. Only what this source can do at this moment, so nothing on it is a press
 * that answers with nothing. */
typedef enum {
    UI_TOUCH_STRIP_LIST = 0,
    UI_TOUCH_STRIP_SEEK,
    UI_TOUCH_STRIP_TUNE,
    UI_TOUCH_STRIP_LIKE,
    UI_TOUCH_STRIP_DISLIKE,
    UI_TOUCH_STRIP_EXIT,
} ui_touch_strip_item_t;

#define UI_TOUCH_STRIP_MAX_ITEMS 5U

// Fills `items`, in the order they stand on the strip, and says how many.
unsigned ui_touch_strip_items(bool has_list, bool can_seek, bool can_tune, bool can_rate,
                              ui_touch_strip_item_t items[UI_TOUCH_STRIP_MAX_ITEMS]);

// Which of `count` equal cells across `width` is under `x`, or -1 outside them.
int ui_touch_strip_cell(int x, int width, unsigned count);

/* The row under `y`, counted from `first_y`, or -1 off either end. Rows are
 * hit by their pitch rather than their height, so the gap between two of them
 * belongs to one of them and a finger never lands on nothing. */
int ui_touch_row_at(int y, int first_y, int pitch, int rows);
