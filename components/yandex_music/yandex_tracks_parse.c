#include <string.h>

#include "yandex_json_reader.h"
#include "yandex_track.h"

/* Joins performers into one credit line. Unlike an id, this is display text:
 * when the line will not fit, the artists that do are kept and the rest are
 * dropped, because a track credited to the first two of three names is still
 * worth playing. */
static bool yandex_parse_artists(json_reader_t *reader, char *output, size_t capacity)
{
    if (!json_accept(reader, '[')) return false;
    output[0] = '\0';
    size_t length = 0U;
    json_skip_whitespace(reader);
    if (json_accept(reader, ']')) return true;

    while (true) {
        json_reader_t artist = *reader;
        char name[YANDEX_TRACK_ARTIST_MAX + 1U];
        if (json_enter_object_key(&artist, "name") &&
            json_read_string_clipped(&artist, name, sizeof(name)) && name[0] != '\0') {
            const size_t separator = (length > 0U) ? 2U : 0U;
            const size_t needed = separator + strlen(name);
            if (length + needed < capacity) {
                if (separator > 0U) {
                    memcpy(output + length, ", ", separator);
                    length += separator;
                }
                strcpy(output + length, name);
                length += strlen(name);
            }
        }
        if (!json_skip_value(reader)) return false;
        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        return json_accept(reader, ']');
    }
}

/* Reads one "track" object. The walk is by structure rather than by key name
 * on purpose: a batch of five carries 35 "id" fields and 24 "name" fields,
 * and all but five of each belong to nested albums, artists and labels. */
static bool yandex_parse_track(json_reader_t *reader, yandex_track_t *track)
{
    *track = (yandex_track_t){0};
    bool available = true;

    if (!json_accept(reader, '{')) return false;
    json_skip_whitespace(reader);
    if (json_accept(reader, '}')) return false;
    while (true) {
        char key[32];
        if (!json_read_string(reader, key, sizeof(key))) key[0] = '\0';
        if (!json_accept(reader, ':')) return false;

        if (strcmp(key, "id") == 0) {
            if (!json_read_string(reader, track->id, sizeof(track->id))) return false;
        } else if (strcmp(key, "title") == 0) {
            if (!json_read_string_clipped(reader, track->title, sizeof(track->title))) {
                return false;
            }
        } else if (strcmp(key, "version") == 0) {
            if (!json_read_string_clipped(reader, track->version, sizeof(track->version))) {
                return false;
            }
        } else if (strcmp(key, "coverUri") == 0) {
            /* Display only, and the track is perfectly playable without it, so
             * a cover that will not fit is dropped rather than refused. */
            if (!json_read_string_clipped(reader, track->cover, sizeof(track->cover))) {
                return false;
            }
        } else if (strcmp(key, "durationMs") == 0) {
            if (!json_read_uint(reader, &track->duration_ms)) return false;
        } else if (strcmp(key, "available") == 0) {
            if (!json_read_bool(reader, &available)) return false;
        } else if (strcmp(key, "artists") == 0) {
            if (!yandex_parse_artists(reader, track->artist, sizeof(track->artist))) {
                return false;
            }
        } else if (!json_skip_value(reader)) {
            return false;
        }

        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        if (!json_accept(reader, '}')) return false;
        break;
    }

    return available && track->id[0] != '\0';
}

bool yandex_tracks_parse_batch(const char *json, yandex_track_batch_t *batch)
{
    if (json == NULL || batch == NULL) return false;
    *batch = (yandex_track_batch_t){0};

    json_reader_t reader = {.cursor = json};
    json_reader_t probe = reader;
    if (json_enter_object_key(&probe, "result")) {
        reader = probe;
    }
    if (!json_enter_object_key(&reader, "sequence")) return false;
    if (!json_accept(&reader, '[')) return false;
    json_skip_whitespace(&reader);
    if (json_accept(&reader, ']')) return false;

    while (true) {
        json_reader_t entry = reader;
        yandex_track_t track;
        bool liked = false;
        json_reader_t like = reader;
        if (json_enter_object_key(&like, "liked")) {
            (void)json_read_bool(&like, &liked);
        }
        if (json_enter_object_key(&entry, "track") && yandex_parse_track(&entry, &track) &&
            batch->count < YANDEX_TRACK_BATCH_MAX) {
            track.liked = liked;
            batch->tracks[batch->count++] = track;
        }
        if (!json_skip_value(&reader)) return batch->count > 0U;
        json_skip_whitespace(&reader);
        if (json_accept(&reader, ',')) continue;
        break;
    }
    return batch->count > 0U;
}
