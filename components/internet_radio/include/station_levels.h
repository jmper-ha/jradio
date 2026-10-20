#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What levelling learned per station, kept so a station starts at its own
 * level the next time (board's audio_level.h does the measuring).
 *
 * What is kept is the station's loudness in LUFS, in tenths, not the
 * correction made from it: the correction depends on the target, and a
 * target changed - by ear on the bench, or in a later release - would
 * otherwise start every remembered station off by the difference. The
 * file's first line says so; one without it holds corrections from before
 * (2026-10-20) and is read as empty.
 *
 * Keyed by a 32-bit hash of the stream's address, not the address itself:
 * the file rides in the settings backup, whose members are capped at 8 KB,
 * and 256 addresses of a hundred-odd bytes would not fit - nor need escaping
 * for their commas and quotes. Two stations sharing a hash at 256 entries is
 * about one in a hundred thousand, and costs one of them the other's
 * correction. Not in the playlist: a station found by the search, or a list
 * brought from elsewhere, is levelled all the same, and the playlist page
 * stays the playlist.
 *
 * Beside it, the listener's own nudge for the station, in whole decibels -
 * a third field, written only when it is not 0. A station nudged before it
 * was ever measured has an empty second field.
 *
 * Most recently played first, in memory and in the file; past the cap the
 * one played longest ago goes. Pure, and tested on the host. */

#define STATION_LEVELS_PATH "/littlefs/config/levels.csv"
#define STATION_LEVELS_FILE_NAME "levels.csv"
#define STATION_LEVELS_FORMAT_LINE "#lufs"
#define STATION_LEVELS_MAX 256U
/* A loudness that moved less than this is not worth a write to flash. */
#define STATION_LEVELS_WRITE_STEP_DB 0.5f
/* What a nudge read from the file may be; the player holds it to its own,
 * narrower limit. */
#define STATION_LEVELS_NUDGE_MAX 20
// tenths_db of a station that has a nudge and no measurement yet.
// Otherwise it is the loudness, LUFS.
#define STATION_LEVELS_UNMEASURED INT16_MIN

typedef struct {
    uint32_t key;
    int16_t tenths_db;
    int8_t nudge_db;
} station_level_t;

typedef struct {
    station_level_t entries[STATION_LEVELS_MAX];
    size_t count;
} station_levels_t;

// FNV-1a.
uint32_t station_levels_key(const char *url);

/* A missing or unreadable file is an empty table, and so is one without the
 * format line; lines that do not parse are skipped. */
void station_levels_load(station_levels_t *levels, const char *path);

// The loudness, LUFS. False for a station never measured, nudged or not.
bool station_levels_find(const station_levels_t *levels, uint32_t key, float *lufs);

// The station's nudge; 0 for one that has none or is not in the table.
int station_levels_nudge(const station_levels_t *levels, uint32_t key);

/* Records the nudge for `key` and moves it to the front, keeping what was
 * measured. True when the table changed and wants writing; a nudge of 0 for
 * a station not in the table changes nothing. */
bool station_levels_set_nudge(station_levels_t *levels, uint32_t key, int nudge_db);

/* Records the loudness `lufs` for `key` and moves it to the front, keeping
 * its nudge. True when the table now differs from the file by enough to be
 * written: a new station, a first measurement, or a loudness that moved by
 * STATION_LEVELS_WRITE_STEP_DB or more. */
bool station_levels_note(station_levels_t *levels, uint32_t key, float lufs);

// Through a temporary file and a rename, so a reset mid-write loses nothing.
bool station_levels_save(const station_levels_t *levels, const char *path);
