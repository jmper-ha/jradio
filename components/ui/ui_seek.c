#include "ui_seek.h"

#include <stddef.h>

uint32_t ui_seek_step_seconds(uint32_t total_seconds)
{
    /* A fixed step cannot serve both ends of what is on a drive: five seconds
     * is right for a song and is 720 clicks across a concert recording. So the
     * step is a sixtieth of the track, held between five seconds and a minute
     * - a three-minute song crosses in 36 clicks, an hour-long file in 60, and
     * neither asks the user to sit and grind the knob. */
    const uint32_t proportional = total_seconds / 60U;
    if (proportional < UI_SEEK_STEP_MIN_S) return UI_SEEK_STEP_MIN_S;
    if (proportional > UI_SEEK_STEP_MAX_S) return UI_SEEK_STEP_MAX_S;
    return proportional;
}

void ui_seek_reset(ui_seek_t *seek)
{
    if (seek == NULL) return;
    seek->active = false;
    seek->total_seconds = 0U;
    seek->target_seconds = 0U;
}

bool ui_seek_begin(ui_seek_t *seek, uint32_t elapsed_seconds, uint32_t total_seconds)
{
    if (seek == NULL || total_seconds == 0U) return false;
    seek->active = true;
    seek->total_seconds = total_seconds;
    /* A variable-bitrate file routinely outlives its estimate, so the position
     * it starts from can already be past the end of the scale. */
    seek->target_seconds =
        elapsed_seconds > total_seconds ? total_seconds : elapsed_seconds;
    return true;
}

bool ui_seek_move(ui_seek_t *seek, int direction)
{
    if (seek == NULL || !seek->active || direction == 0) return false;
    const uint32_t step = ui_seek_step_seconds(seek->total_seconds);
    const uint32_t before = seek->target_seconds;
    if (direction > 0) {
        // Stops at both ends rather than wrapping, like every other list and
        // knob here: a scrub that ran off the end and reappeared at the start
        // would be a jump the user did not ask for.
        seek->target_seconds = seek->total_seconds - before < step
                                   ? seek->total_seconds
                                   : before + step;
    } else {
        seek->target_seconds = before < step ? 0U : before - step;
    }
    return seek->target_seconds != before;
}

bool ui_seek_is_active(const ui_seek_t *seek)
{
    return seek != NULL && seek->active;
}

uint32_t ui_seek_target(const ui_seek_t *seek)
{
    return seek == NULL ? 0U : seek->target_seconds;
}

uint8_t ui_seek_percent(const ui_seek_t *seek)
{
    if (seek == NULL || seek->total_seconds == 0U) return 0U;
    if (seek->target_seconds >= seek->total_seconds) return 100U;
    return (uint8_t)(((uint64_t)seek->target_seconds * 100U) / seek->total_seconds);
}
