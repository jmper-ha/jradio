#include "web_settings.h"

#include <string.h>

#include "cJSON.h"
#include "web_json.h"

/* Long enough for the largest request this accepts - two short members - with
 * room to notice one that is too long rather than silently truncating it into
 * something that parses. */
#define WEB_SETTINGS_BODY_MAX 128U

typedef struct {
    const char *name;
    web_settings_field_t field;
    /* The two named values of a choice field, in enum order; NULL for a switch
     * and for a number. */
    const char *first;
    const char *second;
    bool number;
} field_descriptor_t;

static const field_descriptor_t k_fields[] = {
    {"language", WEB_SETTINGS_FIELD_LANGUAGE, "ru", "en", false},
    {"home_screen", WEB_SETTINGS_FIELD_HOME_SCREEN, "text", "feed", false},
    {"scroll", WEB_SETTINGS_FIELD_SCROLL, "bounce", "left", false},
    {"buffer_view", WEB_SETTINGS_FIELD_BUFFER_VIEW, "text", "graph", false},
    {"autoplay", WEB_SETTINGS_FIELD_AUTOPLAY, NULL, NULL, false},
    {"yandex_music", WEB_SETTINGS_FIELD_YANDEX_MUSIC, NULL, NULL, false},
    {"flip_vertical", WEB_SETTINGS_FIELD_FLIP_VERTICAL, NULL, NULL, false},
    {"flip_horizontal", WEB_SETTINGS_FIELD_FLIP_HORIZONTAL, NULL, NULL, false},
    {"brightness", WEB_SETTINGS_FIELD_BRIGHTNESS, NULL, NULL, true},
    {"volume", WEB_SETTINGS_FIELD_VOLUME, NULL, NULL, true},
};

static const field_descriptor_t *descriptor_by_name(const char *name)
{
    for (size_t index = 0U; index < sizeof(k_fields) / sizeof(k_fields[0]); ++index) {
        if (strcmp(k_fields[index].name, name) == 0) return &k_fields[index];
    }
    return NULL;
}

static bool parse_value(const field_descriptor_t *descriptor, const cJSON *value,
                        int *result)
{
    if (descriptor->first != NULL) {
        if (!cJSON_IsString(value) || value->valuestring == NULL) return false;
        if (strcmp(value->valuestring, descriptor->first) == 0) {
            *result = 0;
            return true;
        }
        if (strcmp(value->valuestring, descriptor->second) == 0) {
            *result = 1;
            return true;
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
            int parsed = 0;
            if (descriptor != NULL && parse_value(descriptor, value, &parsed)) {
                change->field = descriptor->field;
                change->value = parsed;
                ok = true;
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
    case WEB_SETTINGS_FIELD_FLIP_VERTICAL:
        return device_settings_set_flip_vertical(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_FLIP_HORIZONTAL:
        return device_settings_set_flip_horizontal(settings, change->value != 0);
    case WEB_SETTINGS_FIELD_BRIGHTNESS:
        return device_settings_set_brightness(settings, (unsigned char)change->value);
    case WEB_SETTINGS_FIELD_VOLUME:
        return device_settings_set_volume(settings, (unsigned char)change->value);
    default:
        return false;
    }
}

void web_settings_make_view(web_settings_view_t *view,
                            const device_settings_t *settings,
                            bool home_screen_available, bool yandex_available)
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
        .flip_vertical = settings->flip_vertical,
        .flip_horizontal = settings->flip_horizontal,
        .home_screen_available = home_screen_available,
        .yandex_available = yandex_available,
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
           left->flip_vertical == right->flip_vertical &&
           left->flip_horizontal == right->flip_horizontal &&
           left->home_screen_available == right->home_screen_available &&
           left->yandex_available == right->yandex_available;
}

void web_settings_write(web_json_writer_t *writer,
                        const web_settings_view_t *view)
{
    if (writer == NULL) return;
    if (view == NULL) {
        web_json_invalidate(writer);
        return;
    }
    web_json_literal(writer, "{\"language\":");
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
    web_json_literal(writer, ",\"flip_vertical\":");
    web_json_literal(writer, view->flip_vertical ? "true" : "false");
    web_json_literal(writer, ",\"flip_horizontal\":");
    web_json_literal(writer, view->flip_horizontal ? "true" : "false");
    web_json_literal(writer, ",\"brightness\":");
    web_json_format(writer, "%u", (unsigned)view->brightness);
    web_json_literal(writer, ",\"volume\":");
    web_json_format(writer, "%u", (unsigned)view->volume);
    /* What this build has, not what it is set to: a switch for a source the
     * firmware was compiled without would change a value nothing reads. */
    web_json_literal(writer, ",\"available\":{\"home_screen\":");
    web_json_literal(writer, view->home_screen_available ? "true" : "false");
    web_json_literal(writer, ",\"yandex_music\":");
    web_json_literal(writer, view->yandex_available ? "true" : "false");
    web_json_literal(writer, "},\"brightness_min\":");
    web_json_format(writer, "%d", WEB_SETTINGS_BRIGHTNESS_MIN);
    web_json_literal(writer, ",\"brightness_max\":");
    web_json_format(writer, "%d", WEB_SETTINGS_BRIGHTNESS_MAX);
    web_json_literal(writer, "}");
}

size_t web_settings_serialize(char *output, size_t output_size,
                              const web_settings_view_t *view)
{
    web_json_writer_t writer;
    web_json_init(&writer, output, output_size, output_size);
    web_settings_write(&writer, view);
    if (!web_json_valid(&writer)) {
        web_json_truncate(&writer);
        return 0U;
    }
    return web_json_length(&writer);
}
