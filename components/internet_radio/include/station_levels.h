#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What levelling learned per station, kept so a station starts at its own
 * level the next time (board's audio_level.h does the measuring).
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
 * Most recently played first, in memory and in the file; past the cap the
 * one played longest ago goes. Pure, and tested on the host. */

#define STATION_LEVELS_PATH "/littlefs/config/levels.csv"
#define STATION_LEVELS_FILE_NAME "levels.csv"
#define STATION_LEVELS_MAX 256U
/* A correction that moved less than this is not worth a write to flash. */
#define STATION_LEVELS_WRITE_STEP_DB 0.5f

typedef struct {
    uint32_t key;
    int16_t tenths_db;
} station_level_t;

typedef struct {
    station_level_t entries[STATION_LEVELS_MAX];
    size_t count;
} station_levels_t;

// FNV-1a.
uint32_t station_levels_key(const char *url);

/* A missing or unreadable file is an empty table; lines that do not parse
 * are skipped. */
void station_levels_load(station_levels_t *levels, const char *path);

bool station_levels_find(const station_levels_t *levels, uint32_t key, float *db);

/* Records `db` for `key` and moves it to the front. True when the table now
 * differs from the file by enough to be written: a new station, or one whose
 * correction moved by STATION_LEVELS_WRITE_STEP_DB or more. */
bool station_levels_note(station_levels_t *levels, uint32_t key, float db);

// Through a temporary file and a rename, so a reset mid-write loses nothing.
bool station_levels_save(const station_levels_t *levels, const char *path);
