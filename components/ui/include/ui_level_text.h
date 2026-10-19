#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "audio_level.h"

/* The levelling's reading over the volume figure on the player: the
 * correction in whole decibels, an ellipsis while a new station is still
 * being measured, and the word for "no room" when the volume left none for a
 * lift - the one case where, unsaid, the switch looks as if it did nothing.
 * Nothing at all when it is off. The words come in from the caller, in the
 * language on screen. Pure, and tested on the host. */
bool ui_level_text(const audio_level_status_t *status, const char *db_unit,
                   const char *measuring, const char *capped, char *out, size_t size);
