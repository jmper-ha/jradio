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
    WEB_SETTINGS_FIELD_DLNA,
    WEB_SETTINGS_FIELD_FLIP_VERTICAL,
    WEB_SETTINGS_FIELD_FLIP_HORIZONTAL,
    WEB_SETTINGS_FIELD_BRIGHTNESS,
    WEB_SETTINGS_FIELD_VOLUME,
    WEB_SETTINGS_FIELD_TIMEZONE,
    WEB_SETTINGS_FIELD_NTP_SERVER,
    WEB_SETTINGS_FIELD_WEATHER,
    WEB_SETTINGS_FIELD_WEATHER_LATITUDE,
    WEB_SETTINGS_FIELD_WEATHER_LONGITUDE,
    /* Not a device setting at all - a secret that goes to its own file - but
     * it arrives on the same page in the same shape, so it is parsed here and
     * routed by the handler: web_settings_apply() refuses it. */
    WEB_SETTINGS_FIELD_OPENWEATHERMAP_KEY,
} web_settings_field_t;

/* Room for the longest text a request may carry - a host name or a key, both
 * sixty-four - with a byte to notice one that is longer rather than storing
 * half of it. */
#define WEB_SETTINGS_TEXT_MAX 65

typedef struct {
    web_settings_field_t field;
    /* The enum ordinal for a choice, 0 or 1 for a switch, a percentage for
     * the two numbers. Validated at parse time, so an applier never has to
     * range-check again. */
    int value;
    /* The text fields - the zone id, the time server, the two coordinates and
     * the key - and empty for every other. Text rather than another ordinal
     * because none is a closed set the page and the device could agree on by
     * position: the zone list grows in the firmware, and a host name is
     * whatever the user has. */
    char text[WEB_SETTINGS_TEXT_MAX];
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
 * The `*_available` flags say whether this build has the rows at all, so the
 * page can hide what the device screen hides instead of offering a switch that
 * changes nothing. */
typedef struct {
    uint8_t language;
    uint8_t home_screen;
    uint8_t scroll;
    uint8_t buffer_view;
    uint8_t volume;
    uint8_t brightness;
    bool autoplay;
    bool yandex_music;
    bool dlna;
    bool flip_vertical;
    bool flip_horizontal;
    /* The zone as its row in device_timezone.h rather than its id: this view
     * is kept per queued WebSocket frame and compared on every pass, and a
     * string here would cost every frame the whole id. Past the end means the
     * card names a zone this build does not have. */
    uint8_t timezone;
    /* The weather service, as device_weather_provider_t. The coordinates and
     * the key stay out of the live diff with the time server: typed once. */
    uint8_t weather;
    bool home_screen_available;
    bool yandex_available;
    bool dlna_available;
} web_settings_view_t;

void web_settings_make_view(web_settings_view_t *view,
                            const device_settings_t *settings,
                            bool home_screen_available, bool yandex_available,
                            bool dlna_available);
bool web_settings_view_equal(const web_settings_view_t *left,
                             const web_settings_view_t *right);

/* The settings object, braces included. Shared by the REST document and the
 * WebSocket section so the two cannot drift into describing the same values
 * differently. */
void web_settings_write(web_json_writer_t *writer,
                        const web_settings_view_t *view);

/* The part of the document that is typed once and never diffed: the time
 * server, the weather's coordinates, and whether a key is on file - never the
 * key itself. */
typedef struct {
    const char *ntp_server;
    const char *weather_latitude;
    const char *weather_longitude;
    bool openweathermap_key_set;
    /* What the weather task last said, as the page shows it beside the
     * picker: "off", "no_key", "waiting", "ok", "failed", plus the HTTP status
     * of a failure and the reading, when there is one. */
    const char *weather_state;
    int weather_http_status;
    bool weather_valid;
    int weather_temperature;
    const char *weather_icon;
} web_settings_document_t;

/* The same object as a standalone document, plus what is not worth a place in
 * the live diff: the fields above, which change once in a device's life, and
 * the list of zones to choose from, which does not change at all. The list is
 * sent rather than written into the page so that adding a zone is one line of
 * firmware and the two cannot describe different sets.
 *
 * Returns the length written, or 0 when the buffer was too small - in which
 * case nothing usable is left in it. */
size_t web_settings_serialize(char *output, size_t output_size,
                              const web_settings_view_t *view,
                              const web_settings_document_t *document);
