#include "device_settings.h"

#include "settings_csv.h"

#include <stdio.h>
#include <stdlib.h>
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
        .volume = DEVICE_VOLUME_DEFAULT,
        .yandex_music = true,
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
    if (read_value(path, "autoplay", value, sizeof(value))) {
        (void)parse_bool(value, &settings->autoplay);
    }
    if (read_value(path, "yandex_music", value, sizeof(value))) {
        (void)parse_bool(value, &settings->yandex_music);
    }
    if (read_value(path, "volume", value, sizeof(value))) {
        char *end = NULL;
        const long parsed = strtol(value, &end, 10);
        /* A corrupt line leaves the default rather than silencing the device
         * or blasting it. */
        if (end != NULL && *end == '\0' && parsed >= 0 && parsed <= 100) {
            settings->volume = (unsigned char)parsed;
        }
    }
    if (read_value(path, "last_source", value, sizeof(value))) {
        if (strcmp(value, "internet_radio") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_INTERNET_RADIO;
        } else if (strcmp(value, "usb") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_USB;
        } else if (strcmp(value, "sd") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_SD;
        }
    }
    /* Read into its own buffer: a path is far longer than the little `value`
     * the other keys share. */
    /* The key on disk is still "last_file": the field was renamed when the
     * SD card joined the USB drive, and settings.csv on devices in the field
     * was not. */
    (void)settings_csv_get(path, "last_usb_file", settings->last_file,
                           sizeof(settings->last_file));
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

bool device_settings_set_autoplay(device_settings_t *settings, bool enabled)
{
    if (!save_value(settings, "autoplay", enabled ? "1" : "0")) return false;
    settings->autoplay = enabled;
    return true;
}

bool device_settings_set_yandex_music(device_settings_t *settings, bool enabled)
{
    if (!save_value(settings, "yandex_music", enabled ? "1" : "0")) return false;
    settings->yandex_music = enabled;
    return true;
}

bool device_settings_set_volume(device_settings_t *settings, unsigned char volume)
{
    if (volume > 100U) return false;
    char text[8];
    snprintf(text, sizeof(text), "%u", (unsigned int)volume);
    if (!save_value(settings, "volume", text)) return false;
    settings->volume = volume;
    return true;
}

bool device_settings_set_last_source(device_settings_t *settings,
                                     device_last_source_t source)
{
    const char *text = source == DEVICE_LAST_SOURCE_INTERNET_RADIO ? "internet_radio"
                     : source == DEVICE_LAST_SOURCE_USB           ? "usb"
                     : source == DEVICE_LAST_SOURCE_SD            ? "sd"
                                                                   : "none";
    if (settings == NULL || source > DEVICE_LAST_SOURCE_SD) return false;
    /* Skip the write when nothing changed: this is called as playback starts,
     * and settings.csv lives on flash with a finite erase budget. */
    if (settings->last_source == source) return true;
    if (!save_value(settings, "last_source", text)) return false;
    settings->last_source = source;
    return true;
}

bool device_settings_set_last_file(device_settings_t *settings, const char *path)
{
    if (settings == NULL || path == NULL ||
        strlen(path) >= sizeof(settings->last_file)) return false;
    if (strcmp(settings->last_file, path) == 0) return true;
    if (!save_value(settings, "last_usb_file", path)) return false;
    memcpy(settings->last_file, path, strlen(path) + 1U);
    return true;
}

bool device_settings_get(const device_settings_t *settings, device_settings_t *copy)
{
    if (settings == NULL || copy == NULL) return false;
    *copy = *settings;
    return true;
}
