#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SETTINGS_CSV_KEY_MAX_LEN 64
#define SETTINGS_CSV_VALUE_MAX_LEN 256

/* Settings are stored as one key,value pair per line.
 *
 * `/littlefs/config/settings.csv` has more than one writer: the player_control
 * task stores the last station there, and the ui task stores the device
 * settings. A set() is a read-modify-write through a temp file whose name is
 * derived from the target, so two of them running at once share that temp file
 * and interleave - losing one update, or leaving a half-written file that
 * costs both the settings and the last station. The accessors below are
 * therefore serialised against each other.
 *
 * Call settings_csv_init() once from app_main before any task that touches
 * settings is started. On the host build it does nothing, the tests being
 * single-threaded. */
void settings_csv_init(void);

bool settings_csv_get(const char *path, const char *key, char *value, size_t value_size);

/* The whole file read once, for a caller that wants many keys out of it.
 *
 * get() opens the file and reads it from the top for every key, and on
 * LittleFS that is several milliseconds each: device_settings_init() asks for
 * some fifty keys and took half a second - long enough, with the web server's
 * priority above the screen's, to freeze the VU meter whenever the settings
 * page polled a handler that reads the settings.
 *
 * A file that does not exist loads as empty, so every key reads as missing,
 * the way get() answers. One past SETTINGS_CSV_SNAPSHOT_MAX, or with no
 * memory to hold it, falls back to get() per key: slow, but never a device
 * that has quietly lost its settings. Lines are cut and parsed exactly as
 * get() cuts and parses them. */
#define SETTINGS_CSV_SNAPSHOT_MAX (32U * 1024U)

typedef struct {
    char *text;
    const char *path;
} settings_csv_snapshot_t;

void settings_csv_snapshot_load(settings_csv_snapshot_t *snapshot, const char *path);
bool settings_csv_snapshot_get(const settings_csv_snapshot_t *snapshot, const char *key,
                               char *value, size_t value_size);
void settings_csv_snapshot_free(settings_csv_snapshot_t *snapshot);
bool settings_csv_set(const char *path, const char *key, const char *value);

/* set(), written in the background. For a value that changes while a hand is
 * on a control - the volume, the brightness - and must not hold the caller
 * for the 80 to 720 ms a write takes on LittleFS. Until it is written, get()
 * and the snapshot answer with it, so nobody reads the old value back. A
 * later value for the same key replaces one not yet written. Short values
 * only; anything that does not fit the queue is written at once, as set().
 *
 * On the host nothing writes in the background: the value waits until
 * settings_csv_flush_pending(), which the device's writer task also runs. */
bool settings_csv_set_later(const char *path, const char *key, const char *value);
size_t settings_csv_flush_pending(void);
