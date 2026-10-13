#include "yandex_session.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The rule yandex_feedback.c follows for the same identifiers: they are the
 * rotor's own and alphanumeric with a little punctuation, so anything else is
 * refused rather than escaped into a request the server reads differently. */
static bool yandex_session_safe(const char *text)
{
    if (text == NULL || text[0] == '\0') return false;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const char character = *cursor;
        const bool alnum = (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z');
        if (!alnum && strchr(":-_.", character) == NULL) return false;
    }
    return true;
}

static size_t yandex_session_done(int written, char *out, size_t out_size)
{
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

size_t yandex_session_new_body(const char *station, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    if (!yandex_session_safe(station)) return 0U;
    return yandex_session_done(
        snprintf(out, out_size, "{\"seeds\":[\"%s\"],\"includeTracksInResponse\":true}", station),
        out, out_size);
}

size_t yandex_session_tracks_path(const char *session_id, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    if (!yandex_session_safe(session_id)) return 0U;
    return yandex_session_done(snprintf(out, out_size, "/rotor/session/%s/tracks", session_id),
                               out, out_size);
}

size_t yandex_session_tracks_body(const yandex_track_batch_t *batch, char *out, size_t out_size)
{
    if (batch == NULL || out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    size_t length = 0U;
    int written = snprintf(out, out_size, "{\"queue\":[");
    if (written < 0 || (size_t)written >= out_size) return yandex_session_done(-1, out, out_size);
    length = (size_t)written;
    bool first = true;
    for (uint8_t i = 0U; i < batch->count && i < YANDEX_TRACK_BATCH_MAX; ++i) {
        if (!yandex_session_safe(batch->tracks[i].id)) continue;
        written = snprintf(out + length, out_size - length, "%s\"%s\"", first ? "" : ",",
                           batch->tracks[i].id);
        if (written < 0 || (size_t)written >= out_size - length) {
            return yandex_session_done(-1, out, out_size);
        }
        length += (size_t)written;
        first = false;
    }
    written = snprintf(out + length, out_size - length, "]}");
    if (written < 0 || (size_t)written >= out_size - length) {
        return yandex_session_done(-1, out, out_size);
    }
    return length + (size_t)written;
}
