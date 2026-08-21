#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Where the listener is dragging the playing file to.
 *
 * Scrubbing is a mode: a triple click opens it, the encoder moves a target
 * that is shown instead of the real position, and a press commits the target
 * and hands the knob back to the volume. The track keeps playing throughout -
 * only the readout follows the knob. That target is all this holds; the jump
 * itself is a command to player_control.
 *
 * The length is copied in when the mode opens rather than re-read per turn.
 * It is an estimate from the bitrate for everything but WAV, so it can move
 * under a variable-bitrate file, and a scale that changed while the user was
 * pointing at it would move the target they had already chosen.
 */

/* One click of the encoder, at the ends of the range this scales over. */
#define UI_SEEK_STEP_MIN_S 5U
#define UI_SEEK_STEP_MAX_S 60U

typedef struct {
    bool active;
    uint32_t total_seconds;
    uint32_t target_seconds;
} ui_seek_t;

void ui_seek_reset(ui_seek_t *seek);

/* Opens the mode at the position playing now. False - and nothing opened -
 * when the track has no length yet: without one there is no scale to move
 * along, and the bar the user would be aiming with is not on screen either. */
bool ui_seek_begin(ui_seek_t *seek, uint32_t elapsed_seconds, uint32_t total_seconds);

/* One encoder click, positive for forwards. True when the target actually
 * moved, so a turn against either end does not redraw the screen. */
bool ui_seek_move(ui_seek_t *seek, int direction);

bool ui_seek_is_active(const ui_seek_t *seek);
uint32_t ui_seek_target(const ui_seek_t *seek);
uint8_t ui_seek_percent(const ui_seek_t *seek);

/* How far one click moves, for a track of this length. */
uint32_t ui_seek_step_seconds(uint32_t total_seconds);
