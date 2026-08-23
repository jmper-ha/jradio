#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Marquee timing for text too wide for the box it sits in.
 *
 * Written here rather than left to LVGL because neither of its long modes is
 * what the device asks for. LV_LABEL_LONG_MODE_SCROLL always animates the
 * return trip and holds a hardcoded 300 ms at each end; SCROLL_CIRCULAR never
 * shows the text whole again, it just wraps the tail into the head. The
 * "left" mode below needs an instant return after a pause, which is neither.
 *
 * Pure and time-driven: everything is a function of milliseconds since the
 * text appeared, so a caller that misses a frame catches up rather than
 * drifting, and the whole thing is testable without a display.
 */

typedef enum {
    /* Left to the end, back to the start, pause, repeat. */
    UI_TEXT_SCROLL_BOUNCE = 0,
    /* Left to the end, hold, snap back whole, pause, repeat. */
    UI_TEXT_SCROLL_LEFT,
} ui_text_scroll_mode_t;

/* 40 px/s is LVGL's own default (LV_LABEL_DEF_SCROLL_SPEED), kept so the feel
 * does not change with this rewrite. The clamps are its as well: a very short
 * overhang would otherwise flick past, and a very long one would crawl. */
#define UI_TEXT_SCROLL_SPEED_PX_S 40
#define UI_TEXT_SCROLL_TRAVEL_MIN_MS 300U
#define UI_TEXT_SCROLL_TRAVEL_MAX_MS 10000U
/* At the far end, before turning back. LVGL's value, for the same reason. */
#define UI_TEXT_SCROLL_BOUNCE_HOLD_MS 300U
/* At the far end in "left" mode, before the text snaps back whole. Longer
 * than the bounce hold because this is the reader's only chance at the tail:
 * after it the line is gone rather than travelling back past the eye. */
#define UI_TEXT_SCROLL_LEFT_HOLD_MS 1000U
/* Showing the start again, before the next cycle. Long enough to read the
 * beginning, which is the part that identifies the line. */
#define UI_TEXT_SCROLL_REPEAT_MS 3000U

/* How far left the text is drawn, in pixels: 0 at rest, negative while
 * scrolled. Zero whenever the text fits, so a caller can apply it blindly. */
int ui_text_scroll_offset(ui_text_scroll_mode_t mode, int text_width, int view_width,
                          uint32_t elapsed_ms);

/* Length of one full cycle. Exposed for the tests, and because it is the
 * honest answer to "how long before it shows the start again". */
uint32_t ui_text_scroll_cycle_ms(ui_text_scroll_mode_t mode, int text_width, int view_width);
