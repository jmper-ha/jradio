#include "station_levels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t station_levels_key(const char *url)
{
    uint32_t hash = 2166136261U;
    if (url == NULL) return hash;
    for (const unsigned char *at = (const unsigned char *)url; *at != '\0'; ++at) {
        hash ^= *at;
        hash *= 16777619U;
    }
    return hash;
}

static int16_t to_tenths(float db)
{
    const long tenths = lroundf(db * 10.0f);
    if (tenths > 999) return 999;
    if (tenths < -999) return -999;
    return (int16_t)tenths;
}

static bool parse_line(const char *line, station_level_t *entry)
{
    char *end = NULL;
    const unsigned long key = strtoul(line, &end, 16);
    if (end == line || *end != ',' || end - line != 8) return false;
    const char *number = end + 1;
    long tenths = strtol(number, &end, 10);
    if (end == number) {
        tenths = STATION_LEVELS_UNMEASURED;
    } else if (tenths > 999 || tenths < -999) {
        return false;
    }
    long nudge = 0;
    if (*end == ',') {
        const char *field = end + 1;
        nudge = strtol(field, &end, 10);
        if (end == field || nudge > STATION_LEVELS_NUDGE_MAX || nudge < -STATION_LEVELS_NUDGE_MAX) {
            return false;
        }
    }
    if (*end != '\0' && *end != '\n' && *end != '\r') return false;
    // An empty line of ours would say nothing; it is no line of ours.
    if (tenths == STATION_LEVELS_UNMEASURED && nudge == 0) return false;
    entry->key = (uint32_t)key;
    entry->tenths_db = (int16_t)tenths;
    entry->nudge_db = (int8_t)nudge;
    return true;
}

static size_t index_of(const station_levels_t *levels, uint32_t key)
{
    for (size_t index = 0U; index < levels->count; ++index) {
        if (levels->entries[index].key == key) return index;
    }
    return levels->count;
}

void station_levels_load(station_levels_t *levels, const char *path)
{
    if (levels == NULL) return;
    levels->count = 0U;
    FILE *file = path != NULL ? fopen(path, "r") : NULL;
    if (file == NULL) return;
    char line[32];
    while (levels->count < STATION_LEVELS_MAX && fgets(line, sizeof(line), file) != NULL) {
        station_level_t entry;
        // A line longer than the buffer is no line of ours; skip its rest.
        if (strchr(line, '\n') == NULL && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {
            }
            continue;
        }
        if (!parse_line(line, &entry)) continue;
        // Written once each; a second is a hand edit, and the first wins.
        if (index_of(levels, entry.key) != levels->count) continue;
        levels->entries[levels->count++] = entry;
    }
    fclose(file);
}

bool station_levels_find(const station_levels_t *levels, uint32_t key, float *db)
{
    if (levels == NULL) return false;
    const size_t index = index_of(levels, key);
    if (index == levels->count) return false;
    if (levels->entries[index].tenths_db == STATION_LEVELS_UNMEASURED) return false;
    if (db != NULL) *db = (float)levels->entries[index].tenths_db / 10.0f;
    return true;
}

int station_levels_nudge(const station_levels_t *levels, uint32_t key)
{
    if (levels == NULL) return 0;
    const size_t index = index_of(levels, key);
    return index == levels->count ? 0 : levels->entries[index].nudge_db;
}

// `entry` into the front, in place of the one at `index` or as a new one.
static void put_first(station_levels_t *levels, size_t index, const station_level_t *entry)
{
    if (index == levels->count) {
        // Full: the last one, played longest ago, makes room.
        if (levels->count < STATION_LEVELS_MAX) levels->count++;
        index = levels->count - 1U;
    }
    memmove(&levels->entries[1], &levels->entries[0], index * sizeof(levels->entries[0]));
    levels->entries[0] = *entry;
}

bool station_levels_set_nudge(station_levels_t *levels, uint32_t key, int nudge_db)
{
    if (levels == NULL) return false;
    if (nudge_db > STATION_LEVELS_NUDGE_MAX) nudge_db = STATION_LEVELS_NUDGE_MAX;
    if (nudge_db < -STATION_LEVELS_NUDGE_MAX) nudge_db = -STATION_LEVELS_NUDGE_MAX;
    const size_t index = index_of(levels, key);
    station_level_t entry = {.key = key, .tenths_db = STATION_LEVELS_UNMEASURED};
    if (index != levels->count) {
        entry = levels->entries[index];
    } else if (nudge_db == 0) {
        return false;
    }
    const bool changed = entry.nudge_db != nudge_db;
    entry.nudge_db = (int8_t)nudge_db;
    put_first(levels, index, &entry);
    return changed;
}

bool station_levels_note(station_levels_t *levels, uint32_t key, float db)
{
    if (levels == NULL || !isfinite(db)) return false;
    station_level_t entry = {.key = key, .tenths_db = to_tenths(db)};
    const size_t index = index_of(levels, key);
    bool changed = true;
    if (index != levels->count) entry.nudge_db = levels->entries[index].nudge_db;
    // Nudged but never measured: the first measurement is news.
    if (index != levels->count && levels->entries[index].tenths_db != STATION_LEVELS_UNMEASURED) {
        const int moved = abs((int)levels->entries[index].tenths_db - (int)entry.tenths_db);
        changed = moved >= (int)lroundf(STATION_LEVELS_WRITE_STEP_DB * 10.0f);
        /* Too small to write is too small to keep as well: the stored value
         * stays the one the next start uses, and the table stays what the
         * file says. Only its place moves. */
        if (!changed) entry = levels->entries[index];
    }
    put_first(levels, index, &entry);
    return changed;
}

bool station_levels_save(const station_levels_t *levels, const char *path)
{
    if (levels == NULL || path == NULL) return false;
    char temporary[96];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) {
        return false;
    }
    FILE *file = fopen(temporary, "w");
    if (file == NULL) return false;
    bool ok = true;
    for (size_t index = 0U; index < levels->count && ok; ++index) {
        const station_level_t *entry = &levels->entries[index];
        ok = fprintf(file, "%08lx,", (unsigned long)entry->key) > 0;
        if (ok && entry->tenths_db != STATION_LEVELS_UNMEASURED) {
            ok = fprintf(file, "%d", (int)entry->tenths_db) > 0;
        }
        if (ok && entry->nudge_db != 0) ok = fprintf(file, ",%d", (int)entry->nudge_db) > 0;
        ok = ok && fputc('\n', file) != EOF;
    }
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        remove(temporary);
        return false;
    }
    // LittleFS's rename replaces the target; a FAT-style one would refuse.
    if (rename(temporary, path) != 0) {
        remove(path);
        if (rename(temporary, path) != 0) {
            remove(temporary);
            return false;
        }
    }
    return true;
}
