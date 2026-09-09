#include "device_settings.h"

#include "settings_csv.h"
#include "device_timezone.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include <stdatomic.h>
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

/* One tab-separated field out of `text` into `out`, returning where the next
 * one starts. A field that is missing altogether leaves `out` empty, which is
 * what a station with no idForFrom looks like. */
static const char *unpack_field(const char *text, char *out, size_t out_size)
{
    out[0] = '\0';
    if (text == NULL) return NULL;
    const char *tab = strchr(text, '\t');
    const size_t length = tab == NULL ? strlen(text) : (size_t)(tab - text);
    if (length < out_size) {
        memcpy(out, text, length);
        out[length] = '\0';
    }
    return tab == NULL ? NULL : tab + 1;
}

/* Copies `value`, turning into spaces everything that would break the file it
 * is about to be written into: settings.csv splits a line on its first comma,
 * and the tab is what separates the fields inside this one value. A station
 * name is the account's own text - "Хиты 90-х, лучшее" is an ordinary name. */
static void pack_field(char *out, size_t out_size, const char *value)
{
    size_t index = 0U;
    for (; value[index] != '\0' && index + 1U < out_size; ++index) {
        const char c = value[index];
        out[index] = (c == ',' || c == '\t' || c == '\r' || c == '\n') ? ' ' : c;
    }
    out[index] = '\0';
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
        .brightness = DEVICE_BRIGHTNESS_DEFAULT,
        .yandex_music = true,
        .dlna = true,
    };
    memcpy(settings->storage_path, path, strlen(path) + 1U);
    memcpy(settings->timezone, DEVICE_TIMEZONE_DEFAULT_ID,
           sizeof(DEVICE_TIMEZONE_DEFAULT_ID));
    memcpy(settings->ntp_server, DEVICE_NTP_SERVER_DEFAULT,
           sizeof(DEVICE_NTP_SERVER_DEFAULT));

    char value[32];
    if (read_value(path, "language", value, sizeof(value))) {
        if (strcmp(value, "en") == 0) settings->language = DEVICE_LANGUAGE_EN;
        else if (strcmp(value, "ru") != 0) settings->language = DEVICE_LANGUAGE_RU;
    }
    if (read_value(path, "home_screen", value, sizeof(value))) {
        if (strcmp(value, "feed") == 0) settings->home_screen = DEVICE_HOME_SCREEN_FEED;
        else if (strcmp(value, "text") != 0) settings->home_screen = DEVICE_HOME_SCREEN_TEXT;
    }
    if (read_value(path, "scroll", value, sizeof(value))) {
        if (strcmp(value, "left") == 0) settings->scroll = DEVICE_SCROLL_LEFT;
        else if (strcmp(value, "bounce") != 0) settings->scroll = DEVICE_SCROLL_BOUNCE;
    }
    if (read_value(path, "buffer_view", value, sizeof(value))) {
        if (strcmp(value, "graph") == 0) settings->buffer_view = DEVICE_BUFFER_VIEW_GRAPH;
        else if (strcmp(value, "text") != 0) settings->buffer_view = DEVICE_BUFFER_VIEW_TEXT;
    }
    /* A zone this build does not have leaves the default standing rather than
     * an empty string: an unset TZ is UTC, and a clock three hours out with no
     * explanation is worse than one that ignored a line in a file. */
    char zone[DEVICE_TIMEZONE_ID_MAX];
    if (settings_csv_get(path, "timezone", zone, sizeof(zone)) &&
        device_timezone_find(zone) != NULL) {
        memcpy(settings->timezone, zone, strlen(zone) + 1U);
    }
    char server[DEVICE_NTP_SERVER_MAX];
    if (settings_csv_get(path, "ntp_server", server, sizeof(server)) && server[0] != '\0') {
        memcpy(settings->ntp_server, server, strlen(server) + 1U);
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
    if (read_value(path, "dlna", value, sizeof(value))) {
        (void)parse_bool(value, &settings->dlna);
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
    if (read_value(path, "brightness", value, sizeof(value))) {
        char *end = NULL;
        const long parsed = strtol(value, &end, 10);
        /* A corrupt line leaves the default rather than blacking out the
         * screen - which would also hide the settings screen that fixes it. */
        if (end != NULL && *end == '\0' && parsed > 0 && parsed <= 100) {
            settings->brightness = (unsigned char)parsed;
        }
    }
    if (read_value(path, "last_source", value, sizeof(value))) {
        if (strcmp(value, "internet_radio") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_INTERNET_RADIO;
        } else if (strcmp(value, "usb") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_USB;
        } else if (strcmp(value, "sd") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_SD;
        } else if (strcmp(value, "yandex") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_YANDEX;
        } else if (strcmp(value, "dlna") == 0) {
            settings->last_source = DEVICE_LAST_SOURCE_DLNA;
        }
    }
    /* Read into its own buffer: a path is far longer than the little `value`
     * the other keys share. */
    /* The key on disk is still "last_file": the field was renamed when the
     * SD card joined the USB drive, and settings.csv on devices in the field
     * was not. */
    (void)settings_csv_get(path, "last_usb_file", settings->last_file,
                           sizeof(settings->last_file));
    /* One key holding the three, tab-separated - the shape stations.csv uses
     * for the same reason. Three keys would have needed three writes for one
     * identity, and two of them can legitimately be empty, which is a value
     * settings.csv refuses. */
    char yandex[DEVICE_LAST_YANDEX_PACKED_MAX];
    if (settings_csv_get(path, "last_yandex", yandex, sizeof(yandex))) {
        const char *cursor = yandex;
        cursor = unpack_field(cursor, settings->last_yandex_id,
                              sizeof(settings->last_yandex_id));
        cursor = unpack_field(cursor, settings->last_yandex_name,
                              sizeof(settings->last_yandex_name));
        (void)unpack_field(cursor, settings->last_yandex_from,
                           sizeof(settings->last_yandex_from));
    }
    /* The same shape, and for the same reason: one place on one server, so one
     * value. The heading beside it is its own key - see the header. */
    char dlna[DEVICE_LAST_DLNA_PACKED_MAX];
    if (settings_csv_get(path, "last_dlna", dlna, sizeof(dlna))) {
        const char *cursor = dlna;
        cursor = unpack_field(cursor, settings->last_dlna_server,
                              sizeof(settings->last_dlna_server));
        cursor = unpack_field(cursor, settings->last_dlna_container,
                              sizeof(settings->last_dlna_container));
        (void)unpack_field(cursor, settings->last_dlna_track,
                           sizeof(settings->last_dlna_track));
    }
    (void)settings_csv_get(path, "last_dlna_title", settings->last_dlna_title,
                           sizeof(settings->last_dlna_title));
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

bool device_settings_set_scroll(device_settings_t *settings, device_scroll_t scroll)
{
    if (settings == NULL || scroll > DEVICE_SCROLL_LEFT) return false;
    if (!save_value(settings, "scroll",
                    scroll == DEVICE_SCROLL_LEFT ? "left" : "bounce")) return false;
    settings->scroll = scroll;
    return true;
}

bool device_settings_set_buffer_view(device_settings_t *settings,
                                     device_buffer_view_t buffer_view)
{
    if (settings == NULL || buffer_view > DEVICE_BUFFER_VIEW_GRAPH) return false;
    if (!save_value(settings, "buffer_view",
                    buffer_view == DEVICE_BUFFER_VIEW_GRAPH ? "graph" : "text")) {
        return false;
    }
    settings->buffer_view = buffer_view;
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

bool device_settings_set_dlna(device_settings_t *settings, bool enabled)
{
    if (!save_value(settings, "dlna", enabled ? "1" : "0")) return false;
    settings->dlna = enabled;
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

bool device_settings_set_brightness(device_settings_t *settings, unsigned char brightness)
{
    /* Zero is refused along with over-100: a backlight at 0 is a dark panel,
     * and nothing on a dark panel can turn it back up. */
    if (brightness == 0U || brightness > 100U) return false;
    char text[8];
    snprintf(text, sizeof(text), "%u", (unsigned int)brightness);
    if (!save_value(settings, "brightness", text)) return false;
    settings->brightness = brightness;
    return true;
}

bool device_settings_set_timezone(device_settings_t *settings, const char *id)
{
    if (settings == NULL || device_timezone_find(id) == NULL) return false;
    if (strcmp(settings->timezone, id) == 0) return true;
    if (!save_value(settings, "timezone", id)) return false;
    memcpy(settings->timezone, id, strlen(id) + 1U);
    return true;
}

bool device_settings_set_ntp_server(device_settings_t *settings, const char *host)
{
    if (settings == NULL) return false;
    /* An empty field on the page means "whatever the device came with", not
     * "no time server": the clock is the one setting with no way to say it is
     * wrong from the device itself. */
    const char *value = host == NULL || host[0] == '\0' ? DEVICE_NTP_SERVER_DEFAULT : host;
    if (strlen(value) >= sizeof(settings->ntp_server)) return false;
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        const unsigned char character = (unsigned char)*cursor;
        /* Printable ASCII without a space or a comma. A space is not part of
         * any host name and would only be a typo; a comma would cut the
         * settings.csv line in two. */
        if (character <= ' ' || character >= 0x7FU || character == ',') return false;
    }
    if (strcmp(settings->ntp_server, value) == 0) return true;
    if (!save_value(settings, "ntp_server", value)) return false;
    memcpy(settings->ntp_server, value, strlen(value) + 1U);
    return true;
}

bool device_settings_set_last_source(device_settings_t *settings,
                                     device_last_source_t source)
{
    const char *text = source == DEVICE_LAST_SOURCE_INTERNET_RADIO ? "internet_radio"
                     : source == DEVICE_LAST_SOURCE_USB           ? "usb"
                     : source == DEVICE_LAST_SOURCE_SD            ? "sd"
                     : source == DEVICE_LAST_SOURCE_YANDEX        ? "yandex"
                     : source == DEVICE_LAST_SOURCE_DLNA          ? "dlna"
                                                                   : "none";
    if (settings == NULL || source > DEVICE_LAST_SOURCE_DLNA) return false;
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

bool device_settings_set_last_yandex(device_settings_t *settings, const char *id,
                                     const char *name, const char *from)
{
    if (settings == NULL) return false;
    const char *station = id == NULL ? "" : id;
    const char *title = name == NULL ? "" : name;
    const char *origin = from == NULL ? "" : from;
    // Refused rather than truncated: half an id names no station.
    if (strlen(station) >= sizeof(settings->last_yandex_id) ||
        strlen(title) >= sizeof(settings->last_yandex_name) ||
        strlen(origin) >= sizeof(settings->last_yandex_from)) {
        return false;
    }
    char packed_id[DEVICE_LAST_YANDEX_ID_MAX];
    char packed_name[DEVICE_LAST_YANDEX_NAME_MAX];
    char packed_from[DEVICE_LAST_YANDEX_FROM_MAX];
    pack_field(packed_id, sizeof(packed_id), station);
    pack_field(packed_name, sizeof(packed_name), title);
    pack_field(packed_from, sizeof(packed_from), origin);
    // Same reason last_source skips a write: this is called as playback
    // starts, over and over, onto flash with a finite erase budget.
    if (strcmp(settings->last_yandex_id, packed_id) == 0 &&
        strcmp(settings->last_yandex_name, packed_name) == 0 &&
        strcmp(settings->last_yandex_from, packed_from) == 0) {
        return true;
    }
    /* One write for the three, so there is no moment where an id names a
     * station and the name beside it belongs to the previous one. An id that
     * is empty clears the point, and the value is still not - the two tabs
     * remain, which is what settings.csv needs. */
    char packed[DEVICE_LAST_YANDEX_PACKED_MAX];
    const int written = snprintf(packed, sizeof(packed), "%s\t%s\t%s", packed_id, packed_name,
                                 packed_from);
    if (written < 0 || (size_t)written >= sizeof(packed)) return false;
    if (!save_value(settings, "last_yandex", packed)) return false;
    memcpy(settings->last_yandex_id, packed_id, strlen(packed_id) + 1U);
    memcpy(settings->last_yandex_name, packed_name, strlen(packed_name) + 1U);
    memcpy(settings->last_yandex_from, packed_from, strlen(packed_from) + 1U);
    return true;
}

bool device_settings_set_last_dlna(device_settings_t *settings, const char *server,
                                   const char *container, const char *track,
                                   const char *title)
{
    if (settings == NULL) return false;
    const char *udn = server == NULL ? "" : server;
    const char *parent = container == NULL ? "" : container;
    const char *item = track == NULL ? "" : track;
    const char *heading = title == NULL ? "" : title;
    // Refused rather than truncated: half an object id names nothing, and the
    // server would answer "no such object" to it.
    if (strlen(udn) >= sizeof(settings->last_dlna_server) ||
        strlen(parent) >= sizeof(settings->last_dlna_container) ||
        strlen(item) >= sizeof(settings->last_dlna_track) ||
        strlen(heading) >= sizeof(settings->last_dlna_title)) {
        return false;
    }
    char packed_server[DEVICE_LAST_DLNA_SERVER_MAX];
    char packed_container[DEVICE_LAST_DLNA_ID_MAX];
    char packed_track[DEVICE_LAST_DLNA_ID_MAX];
    char packed_title[DEVICE_LAST_DLNA_TITLE_MAX];
    pack_field(packed_server, sizeof(packed_server), udn);
    pack_field(packed_container, sizeof(packed_container), parent);
    pack_field(packed_track, sizeof(packed_track), item);
    pack_field(packed_title, sizeof(packed_title), heading);

    // Same reason last_source skips a write: this is called as each track
    // starts, onto flash with a finite erase budget.
    const bool place_unchanged = strcmp(settings->last_dlna_server, packed_server) == 0 &&
                                 strcmp(settings->last_dlna_container, packed_container) == 0 &&
                                 strcmp(settings->last_dlna_track, packed_track) == 0;
    if (!place_unchanged) {
        /* One write for the three, so no power cut can leave a track id beside
         * the previous container's. An empty server clears the point, and the
         * value is still not empty - the two tabs remain, which is what
         * settings.csv needs. */
        char packed[DEVICE_LAST_DLNA_PACKED_MAX];
        const int written = snprintf(packed, sizeof(packed), "%s\t%s\t%s", packed_server,
                                     packed_container, packed_track);
        if (written < 0 || (size_t)written >= sizeof(packed)) return false;
        if (!save_value(settings, "last_dlna", packed)) return false;
        memcpy(settings->last_dlna_server, packed_server, strlen(packed_server) + 1U);
        memcpy(settings->last_dlna_container, packed_container, strlen(packed_container) + 1U);
        memcpy(settings->last_dlna_track, packed_track, strlen(packed_track) + 1U);
    }
    /* An empty heading is not written: settings.csv has no way to store an
     * empty value, and this is the one field where that costs nothing. A
     * container always has a title - the server's own name stands in at the
     * root - so the empty case is a server that announced no name at all, and
     * what stays behind then is the previous heading over the right
     * container. */
    if (packed_title[0] != '\0' && strcmp(settings->last_dlna_title, packed_title) != 0) {
        if (!save_value(settings, "last_dlna_title", packed_title)) return false;
        memcpy(settings->last_dlna_title, packed_title, strlen(packed_title) + 1U);
    }
    return true;
}

bool device_settings_get(const device_settings_t *settings, device_settings_t *copy)
{
    if (settings == NULL || copy == NULL) return false;
    *copy = *settings;
    return true;
}

/* Atomic rather than a plain flag: the web server's task sets it while the UI
 * task is reading, and an exchange is what keeps a change made in that window
 * from being cleared without ever being acted on. */
static atomic_bool s_changed;

void device_settings_mark_changed(void)
{
    atomic_store(&s_changed, true);
}

bool device_settings_take_changed(void)
{
    return atomic_exchange(&s_changed, false);
}

/* The published copy, and the lock that keeps a reader from seeing half of it.
 *
 * A spinlock rather than a mutex: the copy is a few hundred bytes and both
 * sides are only ever copying, so the section is over in about the time taking
 * a mutex would cost. It is the same thing the WebSocket broadcaster does with
 * the player snapshot it publishes. */
#ifdef ESP_PLATFORM
static portMUX_TYPE s_publish_lock = portMUX_INITIALIZER_UNLOCKED;
#define PUBLISH_LOCK() taskENTER_CRITICAL(&s_publish_lock)
#define PUBLISH_UNLOCK() taskEXIT_CRITICAL(&s_publish_lock)
#else
/* The host tests are single-threaded, as they are for settings_csv. */
#define PUBLISH_LOCK() ((void)0)
#define PUBLISH_UNLOCK() ((void)0)
#endif

static device_settings_t s_published;
static bool s_have_published;

void device_settings_publish(const device_settings_t *settings)
{
    if (settings == NULL) return;
    PUBLISH_LOCK();
    s_published = *settings;
    s_have_published = true;
    PUBLISH_UNLOCK();
}

bool device_settings_read_published(device_settings_t *copy)
{
    if (copy == NULL) return false;
    PUBLISH_LOCK();
    const bool published = s_have_published;
    if (published) *copy = s_published;
    PUBLISH_UNLOCK();
    return published;
}

device_language_t device_settings_published_language(void)
{
    PUBLISH_LOCK();
    const device_language_t language = s_have_published ? s_published.language
                                                        : DEVICE_LANGUAGE_RU;
    PUBLISH_UNLOCK();
    return language;
}

bool device_settings_published_switches(bool *yandex_music, bool *dlna)
{
    PUBLISH_LOCK();
    const bool published = s_have_published;
    if (published) {
        if (yandex_music != NULL) *yandex_music = s_published.yandex_music;
        if (dlna != NULL) *dlna = s_published.dlna;
    }
    PUBLISH_UNLOCK();
    return published;
}
