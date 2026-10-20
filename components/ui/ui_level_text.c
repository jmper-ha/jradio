#include "ui_level_text.h"

#include <math.h>
#include <stdio.h>

bool ui_level_text(const audio_level_status_t *status, const char *db_unit,
                   const char *measuring, const char *capped, char *out, size_t size)
{
    if (out == NULL || size == 0U) return false;
    out[0] = '\0';
    if (status == NULL) return false;
    // With the levelling off, the nudge alone - and nothing at all without one.
    if (status->phase == AUDIO_LEVEL_OFF && status->nudge_db == 0) return false;
    if (status->phase == AUDIO_LEVEL_MEASURING) {
        snprintf(out, size, "%s", measuring);
        return true;
    }
    if (status->capped && status->wanted_db + (float)status->nudge_db > 0.0f) {
        snprintf(out, size, "%s", capped);
        return true;
    }
    const long db = lroundf(status->gain_db + (float)status->nudge_db);
    /* An en dash for the minus: a hyphen is too short to read as a sign at
     * this size, and the true minus (U+2212) is not in the 14 px face - it
     * drew as a box on the 480 px panel. */
    if (db < 0) {
        snprintf(out, size, "\xE2\x80\x93%ld %s", -db, db_unit);
    } else if (db > 0) {
        snprintf(out, size, "+%ld %s", db, db_unit);
    } else {
        snprintf(out, size, "0 %s", db_unit);
    }
    return true;
}
