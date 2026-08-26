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

/* How a line too long for its box is animated. The names are the UI's, not
 * LVGL's: neither maps to one of its long modes. */
typedef enum {
    DEVICE_SCROLL_BOUNCE = 0,
    DEVICE_SCROLL_LEFT,
} device_scroll_t;

typedef enum {
    DEVICE_LAST_SOURCE_NONE = 0,
    DEVICE_LAST_SOURCE_INTERNET_RADIO,
    DEVICE_LAST_SOURCE_USB,
    DEVICE_LAST_SOURCE_SD,
} device_last_source_t;

#define DEVICE_SETTINGS_PATH "/littlefs/config/settings.csv"
#define DEVICE_SETTINGS_PATH_MAX 128
/* Long enough for a full path on either volume; the CSV value field caps at
 * 256. The volume is part of the path, which is what tells a resume point on
 * the card from one on the drive. */
#define DEVICE_LAST_FILE_MAX 256
/* Loud enough to be obviously working, quiet enough that a fresh flash does
 * not startle anyone. */
#define DEVICE_VOLUME_DEFAULT 80
/* Backlight percentage. The range is the UI's - see ui_settings_model.h - and
 * this is what the panel came up at before the setting existed. */
#define DEVICE_BRIGHTNESS_DEFAULT 50

typedef struct {
    device_language_t language;
    device_home_screen_t home_screen;
    device_scroll_t scroll;
    bool flip_vertical;
    bool flip_horizontal;
    /* Resume what was playing at power-off instead of opening the home
     * screen. What "what was playing" means is the two fields below: the
     * radio's own last-station URL is stored separately by station_resume. */
    bool autoplay;
    /* Whether Yandex Music appears on the home screen. Stored whatever the
     * firmware was built with - this layer does not know about build options,
     * and a card moved into a build that has the feature should find the
     * choice the user made the last time it did. Defaults to on, so a device
     * that has the feature shows it without anyone going looking. */
    bool yandex_music;
    /* 0..100. Defaults to 80 rather than full: the first sound after a fresh
     * flash should not be at maximum. */
    unsigned char volume;
    /* Backlight, as a percentage of PWM duty. Stored unvalidated against the
     * UI's 10..90 window: this layer only refuses what the hardware cannot do,
     * so a value written by hand still reaches the panel. */
    unsigned char brightness;
    device_last_source_t last_source;
    char last_file[DEVICE_LAST_FILE_MAX];
    char storage_path[DEVICE_SETTINGS_PATH_MAX];
} device_settings_t;

bool device_settings_init(device_settings_t *settings);
bool device_settings_init_at(device_settings_t *settings, const char *path);
bool device_settings_set_language(device_settings_t *settings, device_language_t language);
bool device_settings_set_home_screen(device_settings_t *settings,
                                     device_home_screen_t home_screen);
bool device_settings_set_scroll(device_settings_t *settings, device_scroll_t scroll);
bool device_settings_set_flip_vertical(device_settings_t *settings, bool enabled);
bool device_settings_set_flip_horizontal(device_settings_t *settings, bool enabled);
bool device_settings_set_flip_vertical_value(device_settings_t *settings, int value);
bool device_settings_set_flip_horizontal_value(device_settings_t *settings, int value);
bool device_settings_set_autoplay(device_settings_t *settings, bool enabled);
bool device_settings_set_yandex_music(device_settings_t *settings, bool enabled);
/* Values above 100 are refused rather than clamped: a caller passing one has a
 * bug, and silently accepting it would hide it. */
bool device_settings_set_volume(device_settings_t *settings, unsigned char volume);
bool device_settings_set_brightness(device_settings_t *settings, unsigned char brightness);
/* Recorded as playback starts, so a power cut still leaves the last choice
 * behind. Writing "none" clears the resume point. */
bool device_settings_set_last_source(device_settings_t *settings,
                                     device_last_source_t source);
bool device_settings_set_last_file(device_settings_t *settings, const char *path);
bool device_settings_get(const device_settings_t *settings, device_settings_t *copy);

/* Says that settings.csv was written by someone other than the UI task - today
 * the web interface, which writes through the same setters above but into a
 * copy of its own.
 *
 * The UI holds the settings it read at startup, so without this a change made
 * in the browser would stay invisible on the panel until the settings screen
 * was next opened. Re-reading is only half of it: brightness, the display
 * flips, the volume and the Yandex row have to be applied to the hardware and
 * the menus, which is work only the UI task may do.
 *
 * Taking the flag clears it, so a change is acted on once. */
void device_settings_mark_changed(void);
bool device_settings_take_changed(void);

/* The other direction: what the UI task currently holds, for anyone who needs
 * to read it without touching the card.
 *
 * settings.csv is eleven separate reads - one per key - so a task that polls
 * cannot go to the file. The knob and the buttons change these values without
 * anything else being told, and the web interface has to show what the device
 * actually has, so the owner publishes and everyone else reads this copy.
 *
 * The read is serialised against the publish on the device and is a plain copy
 * on the host, where the tests are single-threaded. False until the first
 * publish, which is what tells a reader that the UI has not started yet. */
void device_settings_publish(const device_settings_t *settings);
bool device_settings_read_published(device_settings_t *copy);
