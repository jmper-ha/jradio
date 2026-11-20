#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_settings.h"
#include "ui_screensaver.h"

/* A look at the time and the weather from the bed, without a screensaver
 * running: a remote key that puts the screensaver's clock up for a few
 * seconds, the weather on a second press, and the screen it started from on
 * a third (a user's request, 2026-11-20).
 *
 * It steps through the faces not already on the panel - over the clock
 * saver the first press is the weather - and leaves out the weather while
 * there is no report. Each press starts the wait again; the wait running out
 * is the same as the last press. It is not somebody at the controls: the
 * screensaver's own wait goes on, and over a dark panel what comes back is
 * the dark panel, lit meanwhile at the screensaver's level, not the full
 * one. Any other input ends it, and goes no further - it was pressed at a
 * face, not at the screen under it. Pure, and tested on the host. */

#define UI_PEEK_MS 5000U

typedef enum {
    UI_PEEK_NONE = 0,
    UI_PEEK_CLOCK,
    UI_PEEK_WEATHER,
} ui_peek_face_t;

typedef struct {
    ui_peek_face_t face;
    uint32_t until_ms;
} ui_peek_t;

void ui_peek_init(ui_peek_t *peek);

/* The key. `saver` is what the screensaver itself has up: its view, before
 * any peek is laid over it. */
void ui_peek_press(ui_peek_t *peek, const ui_screensaver_view_t *saver, bool have_weather,
                   uint32_t now_ms);

// The face up now, NONE once the wait has run out.
ui_peek_face_t ui_peek_face(ui_peek_t *peek, uint32_t now_ms);

// Another input. True when a face was up - the input only took it down.
bool ui_peek_cancel(ui_peek_t *peek);

/* Lays the face over the screensaver's view: the cover, the face on it, and
 * the light to see it by. The clock block, which drifts on the saver, starts
 * from the middle of the area. Nothing changes for NONE. */
void ui_peek_apply(ui_peek_face_t face, const device_settings_t *settings, int area_w,
                   int area_h, int block_w, int block_h, ui_screensaver_view_t *view);
