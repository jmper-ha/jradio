#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_settings.h"
#include "web_json.h"

/* The device settings, as the browser sees them.
 *
 * The on-device settings screen and this speak to the same settings.csv
 * through the same device_settings setters, so neither is the owner: the UI
 * task holds a copy it read at startup and has to be told when the other one
 * writes - see device_settings_take_changed().
 *
 * Parsing and serialising live here rather than in web_server.c so the host
 * tests can reach them; the handler is left with the file read, the write and
 * the notification. */

/* Mirrors UI_SETTINGS_BRIGHTNESS_MIN/MAX in ui_settings_model.h, deliberately
 * duplicated rather than pulled in: the reason for the window is the panel -
 * unreadable below about ten, and zero looks like a dead device - so the web
 * slider has to stop where the knob does, but this component has no business
 * depending on the on-device screen. */
#define WEB_SETTINGS_BRIGHTNESS_MIN 10
#define WEB_SETTINGS_BRIGHTNESS_MAX 90

typedef enum {
    WEB_SETTINGS_FIELD_LANGUAGE = 0,
    WEB_SETTINGS_FIELD_HOME_SCREEN,
    WEB_SETTINGS_FIELD_SCROLL,
    WEB_SETTINGS_FIELD_BUFFER_VIEW,
    WEB_SETTINGS_FIELD_AUTOPLAY,
    WEB_SETTINGS_FIELD_YANDEX_MUSIC,
    WEB_SETTINGS_FIELD_FLIP_VERTICAL,
    WEB_SETTINGS_FIELD_FLIP_HORIZONTAL,
    WEB_SETTINGS_FIELD_BRIGHTNESS,
    WEB_SETTINGS_FIELD_VOLUME,
} web_settings_field_t;

typedef struct {
    web_settings_field_t field;
    /* The enum ordinal for a choice, 0 or 1 for a switch, a percentage for
     * the two numbers. Validated at parse time, so an applier never has to
     * range-check again. */
    int value;
} web_settings_change_t;

/* One field per request, the way one press changes one row on the device.
 * A request naming several would have to report which of them failed to
 * reach the card, and there is no shape for that answer here. */
bool web_settings_parse(const char *body, size_t length,
                        web_settings_change_t *change);

/* Writes the change into `settings` and through it into settings.csv. False
 * when the card refused the write, which is what the handler reports as a
 * failure rather than a bad request. */
bool web_settings_apply(device_settings_t *settings,
                        const web_settings_change_t *change);

/* Everything the browser is shown, and nothing else.
 *
 * Small on purpose: the WebSocket broadcaster keeps one of these per queued
 * frame and compares it against the last one sent on every pass, and a whole
 * device_settings_t carries a 256-byte resume path that the web never sees.
 *
 * `home_screen_available` and `yandex_available` say whether this build has
 * the rows at all, so the page can hide what the device screen hides instead
 * of offering a switch that changes nothing. */
typedef struct {
    uint8_t language;
    uint8_t home_screen;
    uint8_t scroll;
    uint8_t buffer_view;
    uint8_t volume;
    uint8_t brightness;
    bool autoplay;
    bool yandex_music;
    bool flip_vertical;
    bool flip_horizontal;
    bool home_screen_available;
    bool yandex_available;
} web_settings_view_t;

void web_settings_make_view(web_settings_view_t *view,
                            const device_settings_t *settings,
                            bool home_screen_available, bool yandex_available);
bool web_settings_view_equal(const web_settings_view_t *left,
                             const web_settings_view_t *right);

/* The settings object, braces included. Shared by the REST document and the
 * WebSocket section so the two cannot drift into describing the same values
 * differently. */
void web_settings_write(web_json_writer_t *writer,
                        const web_settings_view_t *view);

/* The same object as a standalone document. Returns the length written, or 0
 * when the buffer was too small - in which case nothing usable is left in
 * it. */
size_t web_settings_serialize(char *output, size_t output_size,
                              const web_settings_view_t *view);
