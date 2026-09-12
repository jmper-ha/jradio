#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_language.h"
#include "device_settings.h"

/* The screensaver: what the panel does once nobody has touched it for a
 * while, decided here without LVGL so it can be tested against a clock the
 * test controls.
 *
 * The screen has three ways to rest - dimmed, dark, or dark with the time
 * drifting across it - and one wait shared by all three. Every input wakes it;
 * in the two modes that hide the screen the waking press goes no further,
 * because nobody can see what they are about to press. In the dimmed mode the
 * screen is still readable and the press means what it always means.
 *
 * The drift is a slow bounce, one pixel a step, rather than a jump to a new
 * spot every so often: a jump reads as a glitch, and a block that never holds
 * still is what keeps a panel from burning the digits in. It is deterministic
 * - start in the middle, head down and right, reflect off the edges - so a
 * test can say where the block is after a given number of steps. */

/* The drift itself is not decided here: the panel scrolls the block along
 * its long axis in hardware, and LVGL's animation engine walks the offset.
 * What is decided here is the speed, and where across the other axis the
 * block sits - a different place each time the clock comes up, so that no
 * one row of the glass carries the digits every night. */
#define UI_SCREENSAVER_SPEED_PX_PER_S 8
/* How long after a waking press the controls stay swallowed. A turn of the
 * knob is several detents, and the loop drains a whole burst in one pass:
 * without this the first detent woke the panel and the rest changed the
 * volume before anyone could see it. */
#define UI_SCREENSAVER_WAKE_GRACE_MS 400U

typedef struct {
    uint32_t last_input_ms;
    bool active;
    /* Where the block was put when the clock came up, and the generator
     * that chose it. */
    int x;
    int y;
    uint32_t seed;
    bool have_view;
    /* Set by a waking press that was swallowed; the grace runs from it. */
    bool swallowing;
    uint32_t woke_ms;
} ui_screensaver_t;

typedef struct {
    bool active;
    /* What the backlight should be right now - the idle level while resting
     * in the modes that keep the panel lit, zero when it is dark, the normal
     * level otherwise. The caller applies it only when it differs from what
     * the panel already has. */
    uint8_t backlight;
    /* Whether the black cover is over the screen, and whether the clock block
     * is drawn on it. */
    bool cover;
    bool clock;
    int x;
    int y;
} ui_screensaver_view_t;

void ui_screensaver_init(ui_screensaver_t *saver, uint32_t now_ms);

/* Something happened at the controls. True when the input is to be swallowed:
 * the saver was resting with the screen hidden and this press only wakes it,
 * or it follows such a press within the grace above. */
bool ui_screensaver_wake(ui_screensaver_t *saver, device_screensaver_t mode, uint32_t now_ms);

/* One pass of the UI loop. `area_*` is the panel and `block_*` the clock
 * block, so the block can be placed when the clock comes up - somewhere it
 * fits, and somewhere else the next time; `x`/`y` in the view are that
 * corner, and only that.
 * Returns true when the view differs from the one the previous pass produced
 * - the caller has nothing to do with LVGL otherwise. */
bool ui_screensaver_step(ui_screensaver_t *saver, const device_settings_t *settings,
                         uint32_t now_ms, int area_w, int area_h, int block_w, int block_h,
                         ui_screensaver_view_t *view);

/* How long one leg of the drift takes at the speed above: from `from` to
 * `to` along one axis, in milliseconds, never less than a frame. */
uint32_t ui_screensaver_leg_ms(int from, int to);

/* The three lines of the clock block. */

/* "12:05", or "--:--" until the clock has been set. Two digits either side of
 * the colon always, so the seven-segment face stays the same width. */
void ui_screensaver_time_text(char *out, size_t out_size, bool have_time, int hour, int minute);

/* "11 сентября, четверг" / "Thursday, 11 September". `month` is 1..12 and
 * `weekday` counts from Sunday as 0, the way struct tm does. Empty while the
 * clock is unset. */
void ui_screensaver_date_text(char *out, size_t out_size, device_language_t language,
                              bool have_date, int day, int month, int weekday);

/* One line for what is playing: "performer - track" when both are known, the
 * track alone otherwise, and the heading - the station, the album - when the
 * stream has said nothing about the track yet. Empty when nothing is known. */
void ui_screensaver_track_text(char *out, size_t out_size, const char *heading,
                               const char *artist, const char *title);
