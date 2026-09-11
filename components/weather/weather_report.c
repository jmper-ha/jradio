#include "weather_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* Outside this the reading is not weather but a service returning a sentinel
 * - OpenWeatherMap answers in Kelvin when the units parameter is lost, and
 * 285 degrees on a status strip would be read as a fault in this firmware. */
#define WEATHER_TEMPERATURE_MIN (-90.0)
#define WEATHER_TEMPERATURE_MAX 60.0

int weather_round_temperature(double celsius)
{
    return celsius >= 0.0 ? (int)(celsius + 0.5) : -(int)(-celsius + 0.5);
}

static bool temperature_plausible(double celsius)
{
    return celsius >= WEATHER_TEMPERATURE_MIN && celsius <= WEATHER_TEMPERATURE_MAX;
}

weather_icon_t weather_icon_from_wmo(int code, bool is_day)
{
    /* WMO 4677 as Open-Meteo issues it: the codes come in families, and the
     * family is what the picture shows. Freezing rain and drizzle are sleet -
     * what falls is ice, whatever it was in the cloud. */
    switch (code) {
    case 0:
    case 1: return is_day ? WEATHER_ICON_CLEAR_DAY : WEATHER_ICON_CLEAR_NIGHT;
    case 2: return is_day ? WEATHER_ICON_PARTLY_CLOUDY_DAY : WEATHER_ICON_PARTLY_CLOUDY_NIGHT;
    case 3: return WEATHER_ICON_CLOUDY;
    case 45:
    case 48: return WEATHER_ICON_FOG;
    case 51:
    case 53:
    case 55:
    case 61:
    case 63:
    case 65:
    case 80:
    case 81:
    case 82: return WEATHER_ICON_RAIN;
    case 56:
    case 57:
    case 66:
    case 67: return WEATHER_ICON_SLEET;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return WEATHER_ICON_SNOW;
    case 95:
    case 96:
    case 99: return WEATHER_ICON_THUNDERSTORM;
    default: return WEATHER_ICON_NONE;
    }
}

weather_icon_t weather_icon_from_openweathermap(int condition, bool is_day)
{
    /* The ids are grouped by their hundreds: 2xx thunderstorm, 3xx drizzle,
     * 5xx rain, 6xx snow, 7xx anything that hangs in the air, 800 clear and
     * 80x clouds. The exceptions inside a group are the ones where ice is
     * involved. */
    if (condition >= 200 && condition < 300) return WEATHER_ICON_THUNDERSTORM;
    if (condition >= 300 && condition < 400) return WEATHER_ICON_RAIN;
    if (condition == 511) return WEATHER_ICON_SLEET;
    if (condition >= 500 && condition < 600) return WEATHER_ICON_RAIN;
    if (condition >= 611 && condition <= 616) return WEATHER_ICON_SLEET;
    if (condition >= 600 && condition < 700) return WEATHER_ICON_SNOW;
    if (condition >= 700 && condition < 800) return WEATHER_ICON_FOG;
    if (condition == 800) return is_day ? WEATHER_ICON_CLEAR_DAY : WEATHER_ICON_CLEAR_NIGHT;
    if (condition == 801 || condition == 802) {
        return is_day ? WEATHER_ICON_PARTLY_CLOUDY_DAY : WEATHER_ICON_PARTLY_CLOUDY_NIGHT;
    }
    if (condition == 803 || condition == 804) return WEATHER_ICON_CLOUDY;
    return WEATHER_ICON_NONE;
}

weather_icon_t weather_icon_from_wttr_symbol(const char *symbol, bool is_day)
{
    /* wttr.in's own plain-text symbol set, nineteen of them, out of its
     * oneline renderer: a closed list, which is what makes `%x` the format to
     * ask for rather than the condition's name, of which there are dozens. */
    static const struct {
        const char *symbol;
        weather_icon_t day;
        weather_icon_t night;
    } table[] = {
        {"o", WEATHER_ICON_CLEAR_DAY, WEATHER_ICON_CLEAR_NIGHT},
        {"m", WEATHER_ICON_PARTLY_CLOUDY_DAY, WEATHER_ICON_PARTLY_CLOUDY_NIGHT},
        {"mm", WEATHER_ICON_CLOUDY, WEATHER_ICON_CLOUDY},
        {"mmm", WEATHER_ICON_CLOUDY, WEATHER_ICON_CLOUDY},
        {"=", WEATHER_ICON_FOG, WEATHER_ICON_FOG},
        {"/", WEATHER_ICON_RAIN, WEATHER_ICON_RAIN},
        {".", WEATHER_ICON_RAIN, WEATHER_ICON_RAIN},
        {"//", WEATHER_ICON_RAIN, WEATHER_ICON_RAIN},
        {"///", WEATHER_ICON_RAIN, WEATHER_ICON_RAIN},
        {"*", WEATHER_ICON_SNOW, WEATHER_ICON_SNOW},
        {"*/", WEATHER_ICON_SNOW, WEATHER_ICON_SNOW},
        {"**", WEATHER_ICON_SNOW, WEATHER_ICON_SNOW},
        {"*/*", WEATHER_ICON_SNOW, WEATHER_ICON_SNOW},
        {"x", WEATHER_ICON_SLEET, WEATHER_ICON_SLEET},
        {"x/", WEATHER_ICON_SLEET, WEATHER_ICON_SLEET},
        {"/!/", WEATHER_ICON_THUNDERSTORM, WEATHER_ICON_THUNDERSTORM},
        {"!/", WEATHER_ICON_THUNDERSTORM, WEATHER_ICON_THUNDERSTORM},
        {"*!*", WEATHER_ICON_THUNDERSTORM, WEATHER_ICON_THUNDERSTORM},
    };
    if (symbol == NULL) return WEATHER_ICON_NONE;
    for (size_t index = 0U; index < sizeof(table) / sizeof(table[0]); ++index) {
        if (strcmp(table[index].symbol, symbol) == 0) {
            return is_day ? table[index].day : table[index].night;
        }
    }
    return WEATHER_ICON_NONE;
}

static bool number_of(const cJSON *object, const char *name, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) return false;
    *value = item->valuedouble;
    return true;
}

static bool finish(weather_report_t *report, double celsius, weather_icon_t icon)
{
    if (!temperature_plausible(celsius)) return false;
    report->valid = true;
    report->temperature_c = weather_round_temperature(celsius);
    report->icon = icon;
    return true;
}

bool weather_parse_open_meteo(const char *body, size_t length, weather_report_t *report)
{
    if (report == NULL) return false;
    *report = (weather_report_t){0};
    if (body == NULL || length == 0U) return false;
    cJSON *root = cJSON_ParseWithLength(body, length);
    if (root == NULL) return false;
    bool ok = false;
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    double celsius = 0.0;
    double code = 0.0;
    double is_day = 1.0;
    if (cJSON_IsObject(current) && number_of(current, "temperature_2m", &celsius) &&
        number_of(current, "weather_code", &code)) {
        /* Absent when the request did not ask for it; a day picture is the
         * safer wrong answer, since the sun is drawn on more of the icons. */
        (void)number_of(current, "is_day", &is_day);
        ok = finish(report, celsius, weather_icon_from_wmo((int)code, is_day != 0.0));
    }
    cJSON_Delete(root);
    return ok;
}

bool weather_parse_openweathermap(const char *body, size_t length,
                                  weather_report_t *report)
{
    if (report == NULL) return false;
    *report = (weather_report_t){0};
    if (body == NULL || length == 0U) return false;
    cJSON *root = cJSON_ParseWithLength(body, length);
    if (root == NULL) return false;
    bool ok = false;
    const cJSON *main = cJSON_GetObjectItemCaseSensitive(root, "main");
    const cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    const cJSON *first = cJSON_IsArray(weather) ? cJSON_GetArrayItem(weather, 0) : NULL;
    double celsius = 0.0;
    double condition = 0.0;
    if (cJSON_IsObject(main) && number_of(main, "temp", &celsius) && cJSON_IsObject(first) &&
        number_of(first, "id", &condition)) {
        /* The icon name ends in "d" or "n", and that is the one place the
         * answer says whether the sun is up without a sunrise to compare. */
        const cJSON *icon = cJSON_GetObjectItemCaseSensitive(first, "icon");
        bool is_day = true;
        if (cJSON_IsString(icon) && icon->valuestring != NULL && icon->valuestring[0] != '\0') {
            is_day = icon->valuestring[strlen(icon->valuestring) - 1U] != 'n';
        }
        ok = finish(report, celsius,
                    weather_icon_from_openweathermap((int)condition, is_day));
    }
    cJSON_Delete(root);
    return ok;
}

/* "HH:MM:SS", with anything after the seconds - wttr.in's local time carries
 * a "+0300" - ignored. -1 for anything else. */
static long seconds_of_day(const char *text, size_t length)
{
    if (length < 8U) return -1;
    for (size_t index = 0U; index < 8U; ++index) {
        const bool colon = index == 2U || index == 5U;
        if (colon ? text[index] != ':' : (text[index] < '0' || text[index] > '9')) return -1;
    }
    const long hour = (text[0] - '0') * 10 + (text[1] - '0');
    const long minute = (text[3] - '0') * 10 + (text[4] - '0');
    const long second = (text[6] - '0') * 10 + (text[7] - '0');
    if (hour > 23 || minute > 59 || second > 59) return -1;
    return hour * 3600 + minute * 60 + second;
}

bool weather_parse_wttr(const char *body, size_t length, weather_report_t *report)
{
    if (report == NULL) return false;
    *report = (weather_report_t){0};
    if (body == NULL || length == 0U) return false;
    /* One line, as `format=%t|%x|%S|%s|%T` produces it:
     * "+13°C|o|05:53:09|18:59:44|20:17:45+0300". Anything wttr.in says instead
     * - "location not found", "Sorry, we are running out of queries" - starts
     * with a letter and stops here. */
    char line[96];
    size_t copied = 0U;
    while (copied < length && copied + 1U < sizeof(line) && body[copied] != '\n' &&
           body[copied] != '\r') {
        line[copied] = body[copied];
        ++copied;
    }
    line[copied] = '\0';

    const char *fields[5] = {0};
    size_t lengths[5] = {0};
    size_t count = 0U;
    const char *cursor = line;
    while (count < 5U) {
        const char *bar = strchr(cursor, '|');
        fields[count] = cursor;
        lengths[count] = bar == NULL ? strlen(cursor) : (size_t)(bar - cursor);
        ++count;
        if (bar == NULL) break;
        cursor = bar + 1;
    }
    if (count < 2U) return false;

    const char *temperature = fields[0];
    if (*temperature != '+' && *temperature != '-' && (*temperature < '0' || *temperature > '9')) {
        return false;
    }
    char *end = NULL;
    const double celsius = strtod(temperature, &end);
    if (end == temperature) return false;

    char symbol[8];
    if (lengths[1] == 0U || lengths[1] >= sizeof(symbol)) return false;
    memcpy(symbol, fields[1], lengths[1]);
    symbol[lengths[1]] = '\0';

    /* Day if the local time falls between sunrise and sunset. A line without
     * the three times - an older format, a field the service dropped - is
     * read as day, for the reason Open-Meteo's missing flag is. */
    bool is_day = true;
    if (count == 5U) {
        const long sunrise = seconds_of_day(fields[2], lengths[2]);
        const long sunset = seconds_of_day(fields[3], lengths[3]);
        const long now = seconds_of_day(fields[4], lengths[4]);
        if (sunrise >= 0 && sunset >= 0 && now >= 0) {
            is_day = now >= sunrise && now < sunset;
        }
    }
    return finish(report, celsius, weather_icon_from_wttr_symbol(symbol, is_day));
}

size_t weather_request_url(char *url, size_t url_size, device_weather_provider_t provider,
                           const char *latitude, const char *longitude, const char *key)
{
    if (url == NULL || url_size == 0U) return 0U;
    url[0] = '\0';
    if (latitude == NULL || longitude == NULL || latitude[0] == '\0' || longitude[0] == '\0') {
        return 0U;
    }
    int written = 0;
    switch (provider) {
    case DEVICE_WEATHER_OPEN_METEO:
        written = snprintf(url, url_size,
                           "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                           "&current=temperature_2m,weather_code,is_day",
                           latitude, longitude);
        break;
    case DEVICE_WEATHER_WTTR:
        /* The format string travels URL-encoded: `%` and `|` are not
         * characters a request line may carry bare, whatever curl lets
         * through. */
        written = snprintf(url, url_size,
                           "http://wttr.in/%s,%s?format=%%25t%%7C%%25x%%7C%%25S%%7C%%25s%%7C%%25T",
                           latitude, longitude);
        break;
    case DEVICE_WEATHER_OPENWEATHERMAP:
        if (key == NULL || key[0] == '\0') return 0U;
        written = snprintf(url, url_size,
                           "http://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s"
                           "&units=metric&appid=%s",
                           latitude, longitude, key);
        break;
    case DEVICE_WEATHER_OFF:
        return 0U;
    }
    if (written <= 0 || (size_t)written >= url_size) {
        url[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

void weather_temperature_text(char *text, size_t text_size, const weather_report_t *report)
{
    if (text == NULL || text_size == 0U) return;
    if (report == NULL || !report->valid) {
        text[0] = '\0';
        return;
    }
    if (report->temperature_c == 0) {
        snprintf(text, text_size, "0\xC2\xB0");
    } else {
        snprintf(text, text_size, "%+d\xC2\xB0", report->temperature_c);
    }
}

const char *weather_icon_name(weather_icon_t icon)
{
    switch (icon) {
    case WEATHER_ICON_CLEAR_DAY: return "clear_day";
    case WEATHER_ICON_CLEAR_NIGHT: return "clear_night";
    case WEATHER_ICON_PARTLY_CLOUDY_DAY: return "partly_cloudy_day";
    case WEATHER_ICON_PARTLY_CLOUDY_NIGHT: return "partly_cloudy_night";
    case WEATHER_ICON_CLOUDY: return "cloudy";
    case WEATHER_ICON_FOG: return "fog";
    case WEATHER_ICON_RAIN: return "rain";
    case WEATHER_ICON_SNOW: return "snow";
    case WEATHER_ICON_SLEET: return "sleet";
    case WEATHER_ICON_THUNDERSTORM: return "thunderstorm";
    case WEATHER_ICON_NONE:
    case WEATHER_ICON_COUNT: break;
    }
    return "none";
}

bool weather_key_valid(const char *key)
{
    if (key == NULL) return false;
    size_t length = 0U;
    for (; key[length] != '\0'; ++length) {
        const unsigned char character = (unsigned char)key[length];
        if (character < 0x21U || character > 0x7EU || character == '"' ||
            character == '\\' || character == '&' || character == '%') {
            return false;
        }
        if (length >= WEATHER_KEY_MAX) return false;
    }
    return length >= 8U;
}
