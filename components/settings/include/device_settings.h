#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DEVICE_LANGUAGE_RU = 0,
    DEVICE_LANGUAGE_EN,
} device_language_t;

typedef enum {
    DEVICE_HOME_SCREEN_TEXT = 0,
    DEVICE_HOME_SCREEN_FEED,
} device_home_screen_t;

typedef enum {
    DEVICE_LAST_SOURCE_NONE = 0,
    DEVICE_LAST_SOURCE_INTERNET_RADIO,
    DEVICE_LAST_SOURCE_USB,
} device_last_source_t;

#define DEVICE_SETTINGS_PATH "/littlefs/config/settings.csv"
#define DEVICE_SETTINGS_PATH_MAX 128
/* Long enough for a full path on the drive; the CSV value field caps at 256. */
#define DEVICE_LAST_USB_FILE_MAX 256
/* Loud enough to be obviously working, quiet enough that a fresh flash does
 * not startle anyone. */
#define DEVICE_VOLUME_DEFAULT 80

typedef struct {
    device_language_t language;
    device_home_screen_t home_screen;
    bool flip_vertical;
    bool flip_horizontal;
    /* Resume what was playing at power-off instead of opening the home
     * screen. What "what was playing" means is the two fields below: the
     * radio's own last-station URL is stored separately by station_resume. */
    bool autoplay;
    /* 0..100. Defaults to 80 rather than full: the first sound after a fresh
     * flash should not be at maximum. */
    unsigned char volume;
    device_last_source_t last_source;
    char last_usb_file[DEVICE_LAST_USB_FILE_MAX];
    char storage_path[DEVICE_SETTINGS_PATH_MAX];
} device_settings_t;

bool device_settings_init(device_settings_t *settings);
bool device_settings_init_at(device_settings_t *settings, const char *path);
bool device_settings_set_language(device_settings_t *settings, device_language_t language);
bool device_settings_set_home_screen(device_settings_t *settings,
                                     device_home_screen_t home_screen);
bool device_settings_set_flip_vertical(device_settings_t *settings, bool enabled);
bool device_settings_set_flip_horizontal(device_settings_t *settings, bool enabled);
bool device_settings_set_flip_vertical_value(device_settings_t *settings, int value);
bool device_settings_set_flip_horizontal_value(device_settings_t *settings, int value);
bool device_settings_set_autoplay(device_settings_t *settings, bool enabled);
/* Values above 100 are refused rather than clamped: a caller passing one has a
 * bug, and silently accepting it would hide it. */
bool device_settings_set_volume(device_settings_t *settings, unsigned char volume);
/* Recorded as playback starts, so a power cut still leaves the last choice
 * behind. Writing "none" clears the resume point. */
bool device_settings_set_last_source(device_settings_t *settings,
                                     device_last_source_t source);
bool device_settings_set_last_usb_file(device_settings_t *settings, const char *path);
bool device_settings_get(const device_settings_t *settings, device_settings_t *copy);
