#pragma once

/* Which language the interface speaks, on its own so that both the settings
 * and the tables they point at can name it.
 *
 * It used to live in device_settings.h, which is also where the time-zone
 * table's size comes from - so the moment a zone needed a label per language,
 * the two headers included each other. A type this small is cheaper to move
 * than to work around. */

typedef enum {
    DEVICE_LANGUAGE_RU = 0,
    DEVICE_LANGUAGE_EN,
} device_language_t;
