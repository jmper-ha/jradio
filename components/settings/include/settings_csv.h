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
bool settings_csv_set(const char *path, const char *key, const char *value);
