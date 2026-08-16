#include "device_settings.h"

#include "settings_csv.h"

#include <stdio.h>
#include <string.h>

static bool read_value(const char *path, const char *key, char *value, size_t value_size)
{
    return settings_csv_get(path, key, value, value_size);
}

static bool save_value(device_settings_t *settings, const char *key, const char *value)
{
    if (settings == NULL || settings->storage_path[0] == '\0') return false;
    return settings_csv_set(settings->storage_path, key, value);
}

static bool parse_bool(const char *value, bool *result)
{
    if (strcmp(value, "0") == 0) {
        *result = false;
        return true;
    }
    if (strcmp(value, "1") == 0) {
        *result = true;
        return true;
    }
    return false;
}

bool device_settings_init_at(device_settings_t *settings, const char *path)
{
    if (settings == NULL || path == NULL || path[0] == '\0' ||
        strlen(path) >= sizeof(settings->storage_path)) return false;
    *settings = (device_settings_t){
        .language = DEVICE_LANGUAGE_RU,
        .home_screen = DEVICE_HOME_SCREEN_TEXT,
    };
    memcpy(settings->storage_path, path, strlen(path) + 1U);

    char value[32];
    if (read_value(path, "language", value, sizeof(value))) {
        if (strcmp(value, "en") == 0) settings->language = DEVICE_LANGUAGE_EN;
        else if (strcmp(value, "ru") != 0) settings->language = DEVICE_LANGUAGE_RU;
    }
    if (read_value(path, "home_screen", value, sizeof(value))) {
        if (strcmp(value, "feed") == 0) settings->home_screen = DEVICE_HOME_SCREEN_FEED;
        else if (strcmp(value, "text") != 0) settings->home_screen = DEVICE_HOME_SCREEN_TEXT;
    }
    if (read_value(path, "display_flip_vertical", value, sizeof(value))) {
        (void)parse_bool(value, &settings->flip_vertical);
    }
    if (read_value(path, "display_flip_horizontal", value, sizeof(value))) {
        (void)parse_bool(value, &settings->flip_horizontal);
    }
    return true;
}

bool device_settings_init(device_settings_t *settings)
{
    return device_settings_init_at(settings, DEVICE_SETTINGS_PATH);
}

bool device_settings_set_language(device_settings_t *settings, device_language_t language)
{
    if (settings == NULL || language > DEVICE_LANGUAGE_EN) return false;
    if (!save_value(settings, "language", language == DEVICE_LANGUAGE_EN ? "en" : "ru")) return false;
    settings->language = language;
    return true;
}

bool device_settings_set_home_screen(device_settings_t *settings,
                                     device_home_screen_t home_screen)
{
    if (settings == NULL || home_screen > DEVICE_HOME_SCREEN_FEED) return false;
    if (!save_value(settings, "home_screen",
                    home_screen == DEVICE_HOME_SCREEN_FEED ? "feed" : "text")) return false;
    settings->home_screen = home_screen;
    return true;
}

bool device_settings_set_flip_vertical(device_settings_t *settings, bool enabled)
{
    if (!save_value(settings, "display_flip_vertical", enabled ? "1" : "0")) return false;
    settings->flip_vertical = enabled;
    return true;
}

bool device_settings_set_flip_horizontal(device_settings_t *settings, bool enabled)
{
    if (!save_value(settings, "display_flip_horizontal", enabled ? "1" : "0")) return false;
    settings->flip_horizontal = enabled;
    return true;
}

bool device_settings_set_flip_vertical_value(device_settings_t *settings, int value)
{
    return value == 0 || value == 1 ? device_settings_set_flip_vertical(settings, value != 0) : false;
}

bool device_settings_set_flip_horizontal_value(device_settings_t *settings, int value)
{
    return value == 0 || value == 1 ? device_settings_set_flip_horizontal(settings, value != 0) : false;
}

bool device_settings_get(const device_settings_t *settings, device_settings_t *copy)
{
    if (settings == NULL || copy == NULL) return false;
    *copy = *settings;
    return true;
}
