#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A few small values kept in NVS instead of settings.csv: the ones that
 * change while a hand is on a control.
 *
 * A settings.csv write is a read-modify-write of the whole file on LittleFS -
 * 80 to 720 ms measured - with the flash erasing under it; the cache is off
 * for that, the sound stalls and the erase current is heard as a click. An
 * NVS write appends one entry to a page that is already erased: milliseconds,
 * and an erase only when a page fills. yoRadio, flashed by a great many
 * people without complaints of this kind, saves its volume this way.
 *
 * Only on the device; on the host every call answers false and the caller
 * keeps using settings.csv, which is what the host tests exercise. */
bool settings_nvs_get_u8(const char *key, uint8_t *value);
bool settings_nvs_set_u8(const char *key, uint8_t value);
/* Gone, so settings.csv speaks for the key again - after a restore put a
 * value there that must not be overruled by an older one here. */
void settings_nvs_erase(const char *key);
