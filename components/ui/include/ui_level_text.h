#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "audio_level.h"

/* The levelling's reading over the volume figure on the player: the
 * correction in whole decibels, an ellipsis while a new station is still
 * being measured, and the word for "no room" when the volume left none for a
 * lift - the one case where, unsaid, the switch looks as if it did nothing.
 * The listener's nudge is in the figure; with the levelling off the figure
 * is the nudge, and there is nothing at all without one. The words come in from the caller, in the
 * language on screen. Pure, and tested on the host. */
bool ui_level_text(const audio_level_status_t *status, const char *db_unit,
                   const char *measuring, const char *capped, char *out, size_t size);

/* Whether the reading is still on its way - a station being measured, or its
 * correction moving to where the estimate says. The figure is drawn in the
 * station name's colour then, and in its own once the level has settled
 * (remembered, measured and stable, or from the track's own data), so the
 * colour says whether the number is the station's yet. */
bool ui_level_settling(const audio_level_status_t *status);
