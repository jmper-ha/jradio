#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SETTINGS_CSV_KEY_MAX_LEN 64
#define SETTINGS_CSV_VALUE_MAX_LEN 256

/* Settings are stored as one key,value pair per line. */
bool settings_csv_get(const char *path, const char *key, char *value, size_t value_size);
bool settings_csv_set(const char *path, const char *key, const char *value);
