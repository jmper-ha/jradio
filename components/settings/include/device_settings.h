#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "device_language.h"
#include "device_timezone.h"

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

/* How the player screen shows what the input buffer holds: the reading as a
 * percentage, or a strip of bars that keeps the last few seconds of it. The
 * number answers "how much is held right now" and the graph answers "is it
 * holding steady", which is the question a dropout is an answer to. */
typedef enum {
    DEVICE_BUFFER_VIEW_TEXT = 0,
    DEVICE_BUFFER_VIEW_GRAPH,
} device_buffer_view_t;

/* Where the panel's weather comes from, if anywhere. Three services rather
 * than one because they fail differently: Open-Meteo needs no key and answers
 * over plain HTTP, wttr.in is a proxy that answers 503 under load, and
 * OpenWeatherMap wants an account key - which one is reachable from a given
 * network is not something the firmware can know. The key itself is not a
 * setting: it is a secret and lives in its own file, like the Yandex token. */
typedef enum {
    DEVICE_WEATHER_OFF = 0,
    DEVICE_WEATHER_OPEN_METEO,
    DEVICE_WEATHER_WTTR,
    DEVICE_WEATHER_OPENWEATHERMAP,
} device_weather_provider_t;

typedef enum {
    DEVICE_LAST_SOURCE_NONE = 0,
    DEVICE_LAST_SOURCE_INTERNET_RADIO,
    DEVICE_LAST_SOURCE_USB,
    DEVICE_LAST_SOURCE_SD,
    DEVICE_LAST_SOURCE_YANDEX,
    DEVICE_LAST_SOURCE_DLNA,
} device_last_source_t;

#define DEVICE_SETTINGS_PATH "/littlefs/config/settings.csv"
#define DEVICE_SETTINGS_PATH_MAX 128
/* Long enough for a full path on either volume; the CSV value field caps at
 * 256. The volume is part of the path, which is what tells a resume point on
 * the card from one on the drive. */
#define DEVICE_LAST_FILE_MAX 256
/* The Yandex station to come back to, as the three strings starting one takes.
 *
 * Not the row it sat on: the dashboard is fetched from the account and can
 * come back in a different order, or a station short - a remembered row would
 * then resume whatever had moved into it. Not the id alone either, because
 * the dashboard is what turns an id into a name and an idForFrom, and waiting
 * for that fetch before any sound is most of what autoplay is for.
 *
 * Sized here rather than from yandex_catalog.h: this layer stores strings and
 * knows nothing about Yandex. The sizes are checked against that header where
 * the two meet - see player_control.c. */
#define DEVICE_LAST_YANDEX_ID_MAX 48
#define DEVICE_LAST_YANDEX_NAME_MAX 96
#define DEVICE_LAST_YANDEX_FROM_MAX 48
/* The three in one settings.csv value, tab-separated. Under the file's own
 * 256-byte cap on a value, which is what makes one key possible at all. */
#define DEVICE_LAST_YANDEX_PACKED_MAX \
    (DEVICE_LAST_YANDEX_ID_MAX + DEVICE_LAST_YANDEX_NAME_MAX + DEVICE_LAST_YANDEX_FROM_MAX + 2)
/* Where on a media server to come back to: which server, which container, and
 * which track inside it.
 *
 * The server is its UUID, not the row it sat on - servers answer a search in
 * whatever order they happen to reply, so a row means nothing tomorrow. The
 * container and the track are the server's own object ids, which are opaque
 * and only ever handed back to it; they can go stale when a library is
 * re-scanned, and the resume falls back a step at a time when they do.
 *
 * Sized here rather than from the dlna headers, the way the Yandex fields are:
 * this layer stores strings and knows nothing about UPnP. The sizes are
 * checked against those headers where the two meet - see dlna_source.c. */
#define DEVICE_LAST_DLNA_SERVER_MAX 64
#define DEVICE_LAST_DLNA_ID_MAX 64
/* The three in one settings.csv value, tab-separated, under the file's own
 * 256-byte cap on a value. */
#define DEVICE_LAST_DLNA_PACKED_MAX \
    (DEVICE_LAST_DLNA_SERVER_MAX + 2 * DEVICE_LAST_DLNA_ID_MAX + 2)
/* What to write above the listing when it is opened again. Its own key rather
 * than a fourth field, because the three above already fill most of a value
 * and a title is 128 bytes on its own. Nothing breaks if a power cut leaves it
 * behind the others: a heading is what the screen says, not where the music
 * comes from. */
#define DEVICE_LAST_DLNA_TITLE_MAX 128

/* A host name, not a URL: SNTP takes one and resolves it itself. Long enough
 * for the longest pool name anybody uses, and refused rather than truncated
 * past that - half a host name resolves to nothing at all. */
#define DEVICE_NTP_SERVER_MAX 64
/* What the device used before the setting existed: it resolves to whatever is
 * close, needs no account, and is what every appliance points at. */
#define DEVICE_NTP_SERVER_DEFAULT "pool.ntp.org"

/* A coordinate as the user typed it - "55.75", "-0.1278" - validated on the
 * way in and stored as text, since the only thing ever done with it is to put
 * it in a URL. Room for a sign, three digits, a point and six decimals. */
#define DEVICE_COORDINATE_MAX 16
/* Where the weather is asked for before anyone has said: the same city the
 * default time zone is on, so the two defaults agree with each other. The
 * device cannot find itself - this network's exit resolves to Amsterdam - and
 * a wrong city with the right time is easier to notice than the reverse. */
#define DEVICE_WEATHER_LATITUDE_DEFAULT "55.75"
#define DEVICE_WEATHER_LONGITUDE_DEFAULT "37.62"

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
    device_buffer_view_t buffer_view;
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
    /* Whether the media server appears on the home screen. Same rules as the
     * switch above, and stored the same way: the build decides whether the row
     * can exist at all, this only decides whether it does. */
    bool dlna;
    /* 0..100. Defaults to 80 rather than full: the first sound after a fresh
     * flash should not be at maximum. */
    unsigned char volume;
    /* Backlight, as a percentage of PWM duty. Stored unvalidated against the
     * UI's 10..90 window: this layer only refuses what the hardware cannot do,
     * so a value written by hand still reaches the panel. */
    unsigned char brightness;
    /* The time zone as an id out of device_timezone.h, not a POSIX string:
     * every summer-time rule carries commas, and settings.csv splits a line on
     * the first one. */
    char timezone[DEVICE_TIMEZONE_ID_MAX];
    char ntp_server[DEVICE_NTP_SERVER_MAX];
    device_weather_provider_t weather_provider;
    char weather_latitude[DEVICE_COORDINATE_MAX];
    char weather_longitude[DEVICE_COORDINATE_MAX];
    device_last_source_t last_source;
    char last_file[DEVICE_LAST_FILE_MAX];
    char last_yandex_id[DEVICE_LAST_YANDEX_ID_MAX];
    char last_yandex_name[DEVICE_LAST_YANDEX_NAME_MAX];
    char last_yandex_from[DEVICE_LAST_YANDEX_FROM_MAX];
    char last_dlna_server[DEVICE_LAST_DLNA_SERVER_MAX];
    char last_dlna_container[DEVICE_LAST_DLNA_ID_MAX];
    char last_dlna_track[DEVICE_LAST_DLNA_ID_MAX];
    char last_dlna_title[DEVICE_LAST_DLNA_TITLE_MAX];
    char storage_path[DEVICE_SETTINGS_PATH_MAX];
} device_settings_t;

bool device_settings_init(device_settings_t *settings);
bool device_settings_init_at(device_settings_t *settings, const char *path);
bool device_settings_set_language(device_settings_t *settings, device_language_t language);
bool device_settings_set_home_screen(device_settings_t *settings,
                                     device_home_screen_t home_screen);
bool device_settings_set_scroll(device_settings_t *settings, device_scroll_t scroll);
bool device_settings_set_buffer_view(device_settings_t *settings,
                                     device_buffer_view_t buffer_view);
bool device_settings_set_flip_vertical(device_settings_t *settings, bool enabled);
bool device_settings_set_flip_horizontal(device_settings_t *settings, bool enabled);
bool device_settings_set_flip_vertical_value(device_settings_t *settings, int value);
bool device_settings_set_flip_horizontal_value(device_settings_t *settings, int value);
bool device_settings_set_autoplay(device_settings_t *settings, bool enabled);
bool device_settings_set_yandex_music(device_settings_t *settings, bool enabled);
bool device_settings_set_dlna(device_settings_t *settings, bool enabled);
/* Values above 100 are refused rather than clamped: a caller passing one has a
 * bug, and silently accepting it would hide it. */
bool device_settings_set_volume(device_settings_t *settings, unsigned char volume);
bool device_settings_set_brightness(device_settings_t *settings, unsigned char brightness);
/* An id this build knows, out of device_timezone.h; anything else is refused
 * rather than stored, since a zone the firmware cannot translate is a clock
 * that silently stays on the old one. */
bool device_settings_set_timezone(device_settings_t *settings, const char *id);
/* A host name for SNTP. Empty puts the default back - the field on the page
 * can be cleared, and a device with no time server at all is worse than one on
 * the pool. Spaces, commas and anything unprintable are refused: the first
 * would not resolve, the second would cut the settings line in two. */
bool device_settings_set_ntp_server(device_settings_t *settings, const char *host);
bool device_settings_set_weather_provider(device_settings_t *settings,
                                          device_weather_provider_t provider);
/* A latitude within 90 degrees of the equator, a longitude within 180 of
 * Greenwich, each as decimal text with up to six places. Refused rather than
 * clamped: a coordinate off the globe is a typo, and clamping one would ask
 * for the weather somewhere the user never named. */
bool device_settings_set_weather_latitude(device_settings_t *settings, const char *text);
bool device_settings_set_weather_longitude(device_settings_t *settings, const char *text);
/* The check the two setters apply, on its own so the page and the tests can
 * ask the same question: an optional sign, one to three digits, and at most
 * six decimals, within `limit` degrees either side of zero. */
bool device_settings_coordinate_valid(const char *text, int limit);
/* Recorded as playback starts, so a power cut still leaves the last choice
 * behind. Writing "none" clears the resume point. */
bool device_settings_set_last_source(device_settings_t *settings,
                                     device_last_source_t source);
bool device_settings_set_last_file(device_settings_t *settings, const char *path);
/* The three together, because they are one identity and are only ever written
 * as one - into one key, so that no power cut can leave an id beside the
 * previous station's name. An empty id clears the point, which is what
 * "nothing was played here" looks like. `name` and `from` may be empty - a
 * station still starts without them, it is only named less well.
 *
 * A comma or a tab anywhere in them becomes a space on the way in: the first
 * would cut the settings.csv line in two, the second is what separates the
 * fields inside the value. */
bool device_settings_set_last_yandex(device_settings_t *settings, const char *id,
                                     const char *name, const char *from);
/* Where on a media server playback was. The first three travel as one value
 * for the reason the Yandex three do - they are one place, and half of one is
 * not a place - while the title is written beside them, since it only decides
 * what the heading says. An empty server clears the point.
 *
 * A comma or a tab in any of them becomes a space on the way in: the first
 * would cut the settings.csv line in two, the second separates the fields. */
bool device_settings_set_last_dlna(device_settings_t *settings, const char *server,
                                   const char *container, const char *track,
                                   const char *title);
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

/* Just the two source switches out of that copy.
 *
 * Narrow on purpose: player_control asks on every snapshot, and a snapshot is
 * built on the UI task, on the web server's single worker and on the player's
 * own - none of which has most of a kilobyte of device_settings_t to spare on
 * the stack for two bits.
 *
 * False before the first publish, and the outputs are left alone: a caller
 * that has not been told yet should assume a source is there rather than take
 * it away for the first second after boot. */
bool device_settings_published_switches(bool *yandex_music, bool *dlna);

/* The language out of that same copy, for the components that put words on a
 * screen or into a browser without owning the settings - the controller's
 * error lines, the WebSocket's source names. Russian before the first publish,
 * which is what the device has always come up in. */
device_language_t device_settings_published_language(void);
