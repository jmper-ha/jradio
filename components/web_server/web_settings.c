#include "web_settings.h"

#include <string.h>

#include "alarm_schedule.h"
#include "cJSON.h"
#include "device_timezone.h"
#include "web_json.h"

/* Long enough for the largest request this accepts - two members, the wider of
 * which is a host name - with room to notice one that is too long rather than
 * silently truncating it into something that parses. */
#define WEB_SETTINGS_BODY_MAX 192U

/* The named values of a choice field, in enum order. Four is the widest
 * choice on the page - the weather service - and the rest leave the tail
 * NULL. */
#define WEB_SETTINGS_CHOICE_MAX 4

typedef struct {
    const char *name;
    web_settings_field_t field;
    /* Empty for a switch, for a number and for text. */
    const char *choices[WEB_SETTINGS_CHOICE_MAX];
    bool number;
    bool text;
} field_descriptor_t;

/* The service names are the ones settings.csv stores, so a page and a card
 * written a year apart still mean the same thing by "wttr". In enum order. */
#define WEB_SETTINGS_WEATHER_NAMES "off", "open_meteo", "wttr", "openweathermap"
static const char *const k_weather_names[] = {WEB_SETTINGS_WEATHER_NAMES};
#define WEB_SETTINGS_SCREENSAVER_NAMES "off", "dim", "blank", "clock"
static const char *const k_screensaver_names[] = {WEB_SETTINGS_SCREENSAVER_NAMES};

static const field_descriptor_t k_fields[] = {
    {"language", WEB_SETTINGS_FIELD_LANGUAGE, {"ru", "en"}, false, false},
    {"home_screen", WEB_SETTINGS_FIELD_HOME_SCREEN, {"text", "feed"}, false, false},
    {"scroll", WEB_SETTINGS_FIELD_SCROLL, {"bounce", "left"}, false, false},
    {"buffer_view", WEB_SETTINGS_FIELD_BUFFER_VIEW, {"text", "graph"}, false, false},
    {"autoplay", WEB_SETTINGS_FIELD_AUTOPLAY, {NULL}, false, false},
    {"yandex_music", WEB_SETTINGS_FIELD_YANDEX_MUSIC, {NULL}, false, false},
    {"dlna", WEB_SETTINGS_FIELD_DLNA, {NULL}, false, false},
    {"bt_output", WEB_SETTINGS_FIELD_BT_OUTPUT, {NULL}, false, false},
    {"flip_vertical", WEB_SETTINGS_FIELD_FLIP_VERTICAL, {NULL}, false, false},
    {"flip_horizontal", WEB_SETTINGS_FIELD_FLIP_HORIZONTAL, {NULL}, false, false},
    {"invert_colors", WEB_SETTINGS_FIELD_INVERT_COLORS, {NULL}, false, false},
    {"brightness", WEB_SETTINGS_FIELD_BRIGHTNESS, {NULL}, true, false},
    {"volume", WEB_SETTINGS_FIELD_VOLUME, {NULL}, true, false},
    {"timezone", WEB_SETTINGS_FIELD_TIMEZONE, {NULL}, false, true},
    {"ntp_server", WEB_SETTINGS_FIELD_NTP_SERVER, {NULL}, false, true},
    {"device_name", WEB_SETTINGS_FIELD_DEVICE_NAME, {NULL}, false, true},
    {"weather", WEB_SETTINGS_FIELD_WEATHER, {WEB_SETTINGS_WEATHER_NAMES}, false, false},
    {"weather_latitude", WEB_SETTINGS_FIELD_WEATHER_LATITUDE, {NULL}, false, true},
    {"weather_longitude", WEB_SETTINGS_FIELD_WEATHER_LONGITUDE, {NULL}, false, true},
    {"openweathermap_key", WEB_SETTINGS_FIELD_OPENWEATHERMAP_KEY, {NULL}, false, true},
    {"screensaver", WEB_SETTINGS_FIELD_SCREENSAVER, {WEB_SETTINGS_SCREENSAVER_NAMES}, false, false},
    {"screensaver_seconds", WEB_SETTINGS_FIELD_SCREENSAVER_SECONDS, {NULL}, true, false},
    {"screensaver_brightness", WEB_SETTINGS_FIELD_SCREENSAVER_BRIGHTNESS, {NULL}, true, false},
    {"alarm_enabled", WEB_SETTINGS_FIELD_ALARM_ENABLED, {NULL}, false, false},
    {"alarm_time", WEB_SETTINGS_FIELD_ALARM_TIME, {NULL}, false, true},
    {"alarm_days", WEB_SETTINGS_FIELD_ALARM_DAYS, {NULL}, true, false},
    {"alarm_station", WEB_SETTINGS_FIELD_ALARM_STATION, {NULL}, true, false},
    {"alarm_volume", WEB_SETTINGS_FIELD_ALARM_VOLUME, {NULL}, true, false},
};

/* A provider the page has no name for - a newer card - reads as off, which is
 * what the device does with it too. */
static const char *weather_name(uint8_t provider)
{
    const size_t count = sizeof(k_weather_names) / sizeof(k_weather_names[0]);
    return provider < count ? k_weather_names[provider] : k_weather_names[0];
}

static const char *screensaver_name(uint8_t mode)
{
    const size_t count = sizeof(k_screensaver_names) / sizeof(k_screensaver_names[0]);
    return mode < count ? k_screensaver_names[mode] : k_screensaver_names[0];
}

static const field_descriptor_t *descriptor_by_name(const char *name)
{
    for (size_t index = 0U; index < sizeof(k_fields) / sizeof(k_fields[0]); ++index) {
        if (strcmp(k_fields[index].name, name) == 0) return &k_fields[index];
    }
    return NULL;
}

static bool parse_value(const field_descriptor_t *descriptor, const cJSON *value,
                        web_settings_change_t *change)
{
    int *const result = &change->value;
    if (descriptor->text) {
        if (!cJSON_IsString(value) || value->valuestring == NULL) return false;
        if (strlen(value->valuestring) >= sizeof(change->text)) return false;
        /* Whether the text means anything is the setter's question: a zone id
         * is checked against the firmware's table and a host name against what
         * a host name may contain, and neither belongs here. */
        strcpy(change->text, value->valuestring);
        return true;
    }
    if (descriptor->choices[0] != NULL) {
        if (!cJSON_IsString(value) || value->valuestring == NULL) return false;
        for (int index = 0; index < WEB_SETTINGS_CHOICE_MAX; ++index) {
            if (descriptor->choices[index] == NULL) break;
            if (strcmp(value->valuestring, descriptor->choices[index]) == 0) {
                *result = index;
                return true;
            }
        }
        return false;
    }
    if (!descriptor->number) {
        if (!cJSON_IsBool(value)) return false;
        *result = cJSON_IsTrue(value) ? 1 : 0;
        return true;
    }
    if (!cJSON_IsNumber(value)) return false;
    const double raw = value->valuedouble;
    /* Whole percentages only. A fractional slider position would be stored as
     * an integer anyway, and rounding one behind the user's back is how a page
     * ends up showing a value the device never took. */
    if (raw != (double)(int)raw) return false;
    const int number = (int)raw;
    if (descriptor->field == WEB_SETTINGS_FIELD_BRIGHTNESS) {
        if (number < WEB_SETTINGS_BRIGHTNESS_MIN ||
            number > WEB_SETTINGS_BRIGHTNESS_MAX) {
            return false;
        }
    } else if (descriptor->field == WEB_SETTINGS_FIELD_SCREENSAVER_BRIGHTNESS) {
        if (number < WEB_SETTINGS_IDLE_BRIGHTNESS_MIN ||
            number > WEB_SETTINGS_IDLE_BRIGHTNESS_MAX) {
            return false;
        }
    } else if (descriptor->field == WEB_SETTINGS_FIELD_SCREENSAVER_SECONDS) {
        /* Off the device's own list or nothing: a page offering 45 would be
         * offering a value the knob could never land on again. */
        if (number <= 0 || !device_settings_screensaver_seconds_valid((unsigned int)number)) {
            return false;
        }
    } else if (descriptor->field == WEB_SETTINGS_FIELD_ALARM_DAYS) {
        /* Never empty, which is the state the whole feature is arranged to
         * make unreachable, and never a bit the week does not have. */
        if (number <= 0 || number > (int)ALARM_DAYS_ALL) return false;
    } else if (descriptor->field == WEB_SETTINGS_FIELD_ALARM_STATION) {
        if (number < 0 || number > DEVICE_ALARM_STATION_MAX) return false;
    } else if (number < 0 || number > 100) {
        return false;
    }
    *result = number;
    return true;
}

bool web_settings_parse(const char *body, size_t length,
                        web_settings_change_t *change)
{
    if (body == NULL || change == NULL || length == 0U ||
        length >= WEB_SETTINGS_BODY_MAX ||
        memchr(body, '\0', length) != NULL) {
        return false;
    }
    char raw[WEB_SETTINGS_BODY_MAX];
    memcpy(raw, body, length);
    raw[length] = '\0';

    cJSON *root = cJSON_Parse(raw);
    if (root == NULL) return false;
    bool ok = false;
    if (cJSON_IsObject(root)) {
        const cJSON *field = cJSON_GetObjectItemCaseSensitive(root, "field");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
        /* Exactly the two members, so a request carrying a third - a stale
         * field name from an older page, say - is refused instead of having
         * the part this build understands applied. */
        if (cJSON_GetArraySize(root) == 2 && cJSON_IsString(field) &&
            field->valuestring != NULL && value != NULL) {
            const field_descriptor_t *descriptor = descriptor_by_name(field->valuestring);
            if (descriptor != NULL) {
                *change = (web_settings_change_t){.field = descriptor->field};
                ok = parse_value(descriptor, value, change);
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool web_settings_apply(device_settings_t *settings,
                        const web_settings_change_t *change)
{
    if (settings == NULL || change == NULL) return false;
    switch (change->field) {
    case WEB_SETTINGS_FIELD_LANGUAGE:
        return device_settings_set_language(settings, (device_language_t)change->value);
    case WEB_SETTINGS_FIELD_HOME_SCREEN:
        return device_settings_set_home_screen(settings,
                                               (device_home_screen_t)change->value);
    case WEB_SETTINGS_FIELD_SCROLL:
        return device_settings_set_scroll(settings, (device_scroll_t)change->value);
    case WEB_SETTINGS_FIELD_BUFFER_VIEW:
        return device_settings_set_buffer_view(settings,
                                               (device_buffer_view_t)change->value);
    case WEB_SETTINGS_FIELD_AUTOPLAY:
        return device_settings_set_autoplay(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_YANDEX_MUSIC:
        return device_settings_set_yandex_music(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_DLNA:
        return device_settings_set_dlna(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_BT_OUTPUT:
        return device_settings_set_bt_output(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_FLIP_VERTICAL:
        return device_settings_set_flip_vertical(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_FLIP_HORIZONTAL:
        return device_settings_set_flip_horizontal(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_INVERT_COLORS:
        return device_settings_set_invert_colors(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_BRIGHTNESS:
        return device_settings_set_brightness(settings, (unsigned char)change->value);
    case WEB_SETTINGS_FIELD_VOLUME:
        return device_settings_set_volume(settings, (unsigned char)change->value);
    case WEB_SETTINGS_FIELD_TIMEZONE:
        return device_settings_set_timezone(settings, change->text);
    case WEB_SETTINGS_FIELD_NTP_SERVER:
        return device_settings_set_ntp_server(settings, change->text);
    case WEB_SETTINGS_FIELD_DEVICE_NAME:
        return device_settings_set_device_name(settings, change->text);
    case WEB_SETTINGS_FIELD_WEATHER:
        return device_settings_set_weather_provider(settings,
                                                    (device_weather_provider_t)change->value);
    case WEB_SETTINGS_FIELD_WEATHER_LATITUDE:
        return device_settings_set_weather_latitude(settings, change->text);
    case WEB_SETTINGS_FIELD_WEATHER_LONGITUDE:
        return device_settings_set_weather_longitude(settings, change->text);
    case WEB_SETTINGS_FIELD_SCREENSAVER:
        return device_settings_set_screensaver(settings, (device_screensaver_t)change->value);
    case WEB_SETTINGS_FIELD_SCREENSAVER_SECONDS:
        return device_settings_set_screensaver_seconds(settings, (unsigned int)change->value);
    case WEB_SETTINGS_FIELD_SCREENSAVER_BRIGHTNESS:
        return device_settings_set_screensaver_brightness(settings,
                                                          (unsigned char)change->value);
    case WEB_SETTINGS_FIELD_ALARM_ENABLED:
        return device_settings_set_alarm_enabled(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_ALARM_TIME: {
        uint8_t hour = 0U;
        uint8_t minute = 0U;
        if (!alarm_time_parse(change->text, &hour, &minute)) return false;
        return device_settings_set_alarm_time(settings, hour, minute);
    }
    case WEB_SETTINGS_FIELD_ALARM_DAYS:
        return device_settings_set_alarm_days(settings, (unsigned int)change->value);
    case WEB_SETTINGS_FIELD_ALARM_STATION:
        return device_settings_set_alarm_station(settings, (unsigned int)change->value);
    case WEB_SETTINGS_FIELD_ALARM_VOLUME:
        return device_settings_set_alarm_volume(settings, (unsigned char)change->value);
    case WEB_SETTINGS_FIELD_OPENWEATHERMAP_KEY:
        /* Not the card's: the handler routes it to the key file. */
        return false;
    default:
        return false;
    }
}

void web_settings_make_view(web_settings_view_t *view,
                            const device_settings_t *settings,
                            bool home_screen_available, bool yandex_available,
                            bool dlna_available, bool bt_available)
{
    if (view == NULL) return;
    if (settings == NULL) {
        *view = (web_settings_view_t){0};
        return;
    }
    *view = (web_settings_view_t){
        .language = (uint8_t)settings->language,
        .home_screen = (uint8_t)settings->home_screen,
        .scroll = (uint8_t)settings->scroll,
        .buffer_view = (uint8_t)settings->buffer_view,
        .volume = settings->volume,
        .brightness = settings->brightness,
        .autoplay = settings->autoplay,
        .yandex_music = settings->yandex_music,
        .dlna = settings->dlna,
        .bt_output = settings->bt_output,
        .flip_vertical = settings->flip_vertical,
        .flip_horizontal = settings->flip_horizontal,
        .invert_colors = settings->invert_colors,
        .timezone = (uint8_t)device_timezone_index_of(settings->timezone),
        .weather = (uint8_t)settings->weather_provider,
        .screensaver = (uint8_t)settings->screensaver,
        .screensaver_seconds = settings->screensaver_seconds,
        .screensaver_brightness = settings->screensaver_brightness,
        .alarm = settings->alarm,
        .home_screen_available = home_screen_available,
        .yandex_available = yandex_available,
        .dlna_available = dlna_available,
        .bt_available = bt_available,
        /* Nothing armed until the caller says otherwise: zero would be the
         * first function. */
        .remote_available = false,
        .remote_learning = -1,
        .remote_revision = 0U,
    };
}

bool web_settings_view_equal(const web_settings_view_t *left,
                             const web_settings_view_t *right)
{
    if (left == right) return true;
    if (left == NULL || right == NULL) return false;
    /* Field by field rather than memcmp: every member happens to be one byte
     * today, and a field added later that is not would make a padding byte
     * decide whether the browser gets told. */
    return left->language == right->language &&
           left->home_screen == right->home_screen &&
           left->scroll == right->scroll &&
           left->buffer_view == right->buffer_view && left->volume == right->volume &&
           left->brightness == right->brightness &&
           left->autoplay == right->autoplay &&
           left->yandex_music == right->yandex_music &&
           left->dlna == right->dlna && left->bt_output == right->bt_output &&
           left->flip_vertical == right->flip_vertical &&
           left->flip_horizontal == right->flip_horizontal &&
           left->invert_colors == right->invert_colors &&
           left->timezone == right->timezone && left->weather == right->weather &&
           left->screensaver == right->screensaver &&
           left->screensaver_seconds == right->screensaver_seconds &&
           left->screensaver_brightness == right->screensaver_brightness &&
           left->alarm.enabled == right->alarm.enabled &&
           left->alarm.hour == right->alarm.hour &&
           left->alarm.minute == right->alarm.minute &&
           left->alarm.days == right->alarm.days &&
           left->alarm.station == right->alarm.station &&
           left->alarm.volume == right->alarm.volume &&
           left->home_screen_available == right->home_screen_available &&
           left->yandex_available == right->yandex_available &&
           left->dlna_available == right->dlna_available &&
           left->bt_available == right->bt_available &&
           left->remote_available == right->remote_available &&
           left->remote_learning == right->remote_learning &&
           left->remote_revision == right->remote_revision;
}

/* The id of the zone a view carries, or the default's when the card names one
 * this build does not have - the browser has to show a row that exists in the
 * list beside it, and "nothing selected" is not a time zone. */
static const char *view_timezone_id(const web_settings_view_t *view)
{
    const device_timezone_t *zone = device_timezone_at(view->timezone);
    return zone != NULL ? zone->id : DEVICE_TIMEZONE_DEFAULT_ID;
}

static void write_body(web_json_writer_t *writer, const web_settings_view_t *view)
{
    web_json_literal(writer, "\"language\":");
    web_json_string(writer, view->language == DEVICE_LANGUAGE_EN ? "en" : "ru");
    web_json_literal(writer, ",\"home_screen\":");
    web_json_string(writer,
                    view->home_screen == DEVICE_HOME_SCREEN_FEED ? "feed" : "text");
    web_json_literal(writer, ",\"scroll\":");
    web_json_string(writer, view->scroll == DEVICE_SCROLL_LEFT ? "left" : "bounce");
    web_json_literal(writer, ",\"buffer_view\":");
    web_json_string(writer,
                    view->buffer_view == DEVICE_BUFFER_VIEW_GRAPH ? "graph" : "text");
    web_json_literal(writer, ",\"autoplay\":");
    web_json_literal(writer, view->autoplay ? "true" : "false");
    web_json_literal(writer, ",\"yandex_music\":");
    web_json_literal(writer, view->yandex_music ? "true" : "false");
    web_json_literal(writer, ",\"dlna\":");
    web_json_literal(writer, view->dlna ? "true" : "false");
    web_json_literal(writer, ",\"bt_output\":");
    web_json_literal(writer, view->bt_output ? "true" : "false");
    web_json_literal(writer, ",\"flip_vertical\":");
    web_json_literal(writer, view->flip_vertical ? "true" : "false");
    web_json_literal(writer, ",\"flip_horizontal\":");
    web_json_literal(writer, view->flip_horizontal ? "true" : "false");
    web_json_literal(writer, ",\"invert_colors\":");
    web_json_literal(writer, view->invert_colors ? "true" : "false");
    web_json_literal(writer, ",\"brightness\":");
    web_json_format(writer, "%u", (unsigned)view->brightness);
    web_json_literal(writer, ",\"volume\":");
    web_json_format(writer, "%u", (unsigned)view->volume);
    web_json_literal(writer, ",\"timezone\":");
    web_json_string(writer, view_timezone_id(view));
    web_json_literal(writer, ",\"weather\":");
    web_json_string(writer, weather_name(view->weather));
    web_json_literal(writer, ",\"screensaver\":");
    web_json_string(writer, screensaver_name(view->screensaver));
    web_json_literal(writer, ",\"screensaver_seconds\":");
    web_json_format(writer, "%u", (unsigned)view->screensaver_seconds);
    web_json_literal(writer, ",\"screensaver_brightness\":");
    web_json_format(writer, "%u", (unsigned)view->screensaver_brightness);
    /* Five flat members under the names the page posts back, the way every
     * other setting here reads: the page's field table is then one line per
     * row and needs no special case, and the player's indicator reads the two
     * it cares about straight out of the frame. */
    web_json_literal(writer, ",\"alarm_enabled\":");
    web_json_literal(writer, view->alarm.enabled ? "true" : "false");
    web_json_literal(writer, ",\"alarm_time\":");
    char alarm_time[6];
    alarm_time_format(alarm_time, sizeof(alarm_time), view->alarm.hour, view->alarm.minute);
    web_json_string(writer, alarm_time);
    web_json_literal(writer, ",\"alarm_days\":");
    web_json_format(writer, "%u", (unsigned)view->alarm.days);
    web_json_literal(writer, ",\"alarm_station\":");
    web_json_format(writer, "%u", (unsigned)view->alarm.station);
    web_json_literal(writer, ",\"alarm_volume\":");
    web_json_format(writer, "%u", (unsigned)view->alarm.volume);
    /* What this build has, not what it is set to: a switch for a source the
     * firmware was compiled without would change a value nothing reads. */
    web_json_literal(writer, ",\"available\":{\"home_screen\":");
    web_json_literal(writer, view->home_screen_available ? "true" : "false");
    web_json_literal(writer, ",\"yandex_music\":");
    web_json_literal(writer, view->yandex_available ? "true" : "false");
    web_json_literal(writer, ",\"dlna\":");
    web_json_literal(writer, view->dlna_available ? "true" : "false");
    web_json_literal(writer, ",\"bt_output\":");
    web_json_literal(writer, view->bt_available ? "true" : "false");
    web_json_literal(writer, ",\"remote\":");
    web_json_literal(writer, view->remote_available ? "true" : "false");
    web_json_literal(writer, "},\"remote_learning\":");
    if (view->remote_learning < 0) {
        web_json_literal(writer, "null");
    } else {
        web_json_format(writer, "%d", view->remote_learning);
    }
    web_json_literal(writer, ",\"remote_revision\":");
    web_json_format(writer, "%u", (unsigned)view->remote_revision);
    web_json_literal(writer, ",\"brightness_min\":");
    web_json_format(writer, "%d", WEB_SETTINGS_BRIGHTNESS_MIN);
    web_json_literal(writer, ",\"brightness_max\":");
    web_json_format(writer, "%d", WEB_SETTINGS_BRIGHTNESS_MAX);
    web_json_literal(writer, ",\"idle_brightness_min\":");
    web_json_format(writer, "%d", WEB_SETTINGS_IDLE_BRIGHTNESS_MIN);
    web_json_literal(writer, ",\"idle_brightness_max\":");
    web_json_format(writer, "%d", WEB_SETTINGS_IDLE_BRIGHTNESS_MAX);
    /* The waits the device offers, so the page's list is the device's and a
     * step added in the firmware is one line here and none on the page. */
    web_json_literal(writer, ",\"screensaver_seconds_choices\":[");
    for (size_t index = 0; index < DEVICE_SCREENSAVER_SECONDS_CHOICES; ++index) {
        if (index > 0) web_json_literal(writer, ",");
        web_json_format(writer, "%u", (unsigned)device_screensaver_seconds_choices[index]);
    }
    web_json_literal(writer, "]");
}

void web_settings_write(web_json_writer_t *writer,
                        const web_settings_view_t *view)
{
    if (writer == NULL) return;
    if (view == NULL) {
        web_json_invalidate(writer);
        return;
    }
    web_json_literal(writer, "{");
    write_body(writer, view);
    web_json_literal(writer, "}");
}

size_t web_settings_serialize(char *output, size_t output_size,
                              const web_settings_view_t *view,
                              const web_settings_document_t *document)
{
    web_json_writer_t writer;
    web_json_init(&writer, output, output_size, output_size);
    if (view == NULL || document == NULL) return 0U;
    /* Out of the document being written rather than off the card: the zone
     * names have to be in the same language as the labels beside them. */
    const device_language_t language = (device_language_t)view->language;
    web_json_literal(&writer, "{");
    write_body(&writer, view);
    /* Only in the document, not in the live diff: a time server and a pair of
     * coordinates are typed once in a device's life, and the zone list never
     * changes at all. All would otherwise be compared on every pass and kept
     * per queued frame. */
    web_json_literal(&writer, ",\"ntp_server\":");
    web_json_string(&writer, document->ntp_server == NULL ? "" : document->ntp_server);
    web_json_literal(&writer, ",\"device_name\":");
    web_json_string(&writer, document->device_name == NULL ? "" : document->device_name);
    web_json_literal(&writer, ",\"device_name_default\":");
    web_json_string(&writer, document->device_name_default == NULL ? "" : document->device_name_default);
    web_json_literal(&writer, ",\"weather_latitude\":");
    web_json_string(&writer,
                    document->weather_latitude == NULL ? "" : document->weather_latitude);
    web_json_literal(&writer, ",\"weather_longitude\":");
    web_json_string(&writer,
                    document->weather_longitude == NULL ? "" : document->weather_longitude);
    /* Not a setting either, and for a different reason: it is not written to
     * the card at all. It travels with the settings because the page that
     * sets it is this one. */
    web_json_literal(&writer, ",\"sleep\":{\"minutes\":");
    web_json_format(&writer, "%u", (unsigned)document->sleep_minutes);
    web_json_literal(&writer, "}");
    /* Whether, never what: the key is a secret and the page has no need of
     * it beyond knowing there is one. */
    web_json_literal(&writer, ",\"openweathermap_key_set\":");
    web_json_literal(&writer, document->openweathermap_key_set ? "true" : "false");
    web_json_literal(&writer, ",\"weather_state\":");
    web_json_string(&writer, document->weather_state == NULL ? "off" : document->weather_state);
    web_json_literal(&writer, ",\"weather_http_status\":");
    web_json_format(&writer, "%d", document->weather_http_status);
    if (document->weather_valid) {
        web_json_literal(&writer, ",\"weather_report\":{\"temperature\":");
        web_json_format(&writer, "%d", document->weather_temperature);
        web_json_literal(&writer, ",\"icon\":");
        web_json_string(&writer, document->weather_icon == NULL ? "none" : document->weather_icon);
        web_json_literal(&writer, "}");
    } else {
        web_json_literal(&writer, ",\"weather_report\":null");
    }
    web_json_literal(&writer, ",\"timezones\":[");
    for (size_t index = 0U; index < device_timezone_count(); ++index) {
        const device_timezone_t *const zone = device_timezone_at(index);
        if (index > 0U) web_json_literal(&writer, ",");
        web_json_literal(&writer, "{\"id\":");
        web_json_string(&writer, zone->id);
        web_json_literal(&writer, ",\"label\":");
        web_json_string(&writer, device_timezone_label(zone, language));
        web_json_literal(&writer, "}");
    }
    web_json_literal(&writer, "]}");
    if (!web_json_valid(&writer)) {
        web_json_truncate(&writer);
        return 0U;
    }
    return web_json_length(&writer);
}
