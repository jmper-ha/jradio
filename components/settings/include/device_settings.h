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

#define DEVICE_SETTINGS_PATH "/littlefs/config/settings.csv"
#define DEVICE_SETTINGS_PATH_MAX 128

typedef struct {
    device_language_t language;
    device_home_screen_t home_screen;
    bool flip_vertical;
    bool flip_horizontal;
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
bool device_settings_get(const device_settings_t *settings, device_settings_t *copy);
