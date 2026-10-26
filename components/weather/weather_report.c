#include "weather_report.h"

#include <math.h>
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

int weather_hpa_to_mmhg(double hpa)
{
    return weather_round_temperature(hpa * 0.750062);
}

int weather_wind_point(int from_deg)
{
    if (from_deg == WEATHER_UNKNOWN || from_deg < 0) return -1;
    return ((from_deg % 360) + 22) / 45 % 8;
}

// A reading as the whole number it is shown as, or unknown when it is none.
static int16_t whole(double value)
{
    if (!isfinite(value) || value > 30000.0 || value < -30000.0) return WEATHER_UNKNOWN;
    return (int16_t)weather_round_temperature(value);
}

// Everything but the temperature and the picture starts unknown.
static void start_report(weather_report_t *report)
{
    *report = (weather_report_t){0};
    report->feels_like_c = WEATHER_UNKNOWN;
    report->humidity_pct = WEATHER_UNKNOWN;
    report->pressure_mmhg = WEATHER_UNKNOWN;
    report->wind_ms = WEATHER_UNKNOWN;
    report->wind_gust_ms = WEATHER_UNKNOWN;
    report->wind_from_deg = WEATHER_UNKNOWN;
    report->cloud_pct = WEATHER_UNKNOWN;
    report->precipitation_tenths_mm = WEATHER_UNKNOWN;
    report->sunrise_min = WEATHER_UNKNOWN;
    report->sunset_min = WEATHER_UNKNOWN;
}

/* The day of the week of a civil date, 0 Sunday: Sakamoto's method, which
 * needs no table of month lengths and no time library. */
static uint8_t weekday_of(int year, int month, int day)
{
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year -= 1;
    return (uint8_t)((year + year / 4 - year / 100 + year / 400 + offsets[(month - 1) % 12] + day) %
                     7);
}

/* "2026-10-21" or "2026-10-21T07:12", as Open-Meteo writes local dates and
 * times: the weekday, and the minutes after midnight when there is a time. */
static bool parse_local_date(const char *text, uint8_t *weekday, int *minutes)
{
    if (text == NULL || strlen(text) < 10U || text[4] != '-' || text[7] != '-') return false;
    const int year = atoi(text);
    const int month = atoi(text + 5);
    const int day = atoi(text + 8);
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    if (weekday != NULL) *weekday = weekday_of(year, month, day);
    if (minutes != NULL) {
        if (strlen(text) < 16U || text[10] != 'T' || text[13] != ':') return false;
        const int hour = atoi(text + 11);
        const int minute = atoi(text + 14);
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
        *minutes = hour * 60 + minute;
    }
    return true;
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

static int16_t whole_of(const cJSON *object, const char *name)
{
    double value = 0.0;
    return number_of(object, name, &value) ? whole(value) : WEATHER_UNKNOWN;
}

static const cJSON *array_item(const cJSON *object, const char *name, int index)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsArray(array) ? cJSON_GetArrayItem(array, index) : NULL;
}

/* The current block's other readings and the daily block's three days. All
 * optional: an answer to the strip's old request has neither, and still
 * gives its temperature. */
static void open_meteo_details(const cJSON *root, const cJSON *current, weather_report_t *report)
{
    double value = 0.0;
    report->feels_like_c = whole_of(current, "apparent_temperature");
    report->humidity_pct = whole_of(current, "relative_humidity_2m");
    if (number_of(current, "pressure_msl", &value)) {
        report->pressure_mmhg = (int16_t)weather_hpa_to_mmhg(value);
    }
    report->wind_ms = whole_of(current, "wind_speed_10m");
    report->wind_gust_ms = whole_of(current, "wind_gusts_10m");
    report->wind_from_deg = whole_of(current, "wind_direction_10m");
    report->cloud_pct = whole_of(current, "cloud_cover");
    if (number_of(current, "precipitation", &value)) report->precipitation_tenths_mm = whole(value * 10.0);

    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(daily)) return;
    int minutes = 0;
    const cJSON *sunrise = array_item(daily, "sunrise", 0);
    const cJSON *sunset = array_item(daily, "sunset", 0);
    if (cJSON_IsString(sunrise) && parse_local_date(sunrise->valuestring, NULL, &minutes)) {
        report->sunrise_min = (int16_t)minutes;
    }
    if (cJSON_IsString(sunset) && parse_local_date(sunset->valuestring, NULL, &minutes)) {
        report->sunset_min = (int16_t)minutes;
    }
    for (int index = 0; index < WEATHER_FORECAST_DAYS; ++index) {
        weather_day_t *day = &report->days[index];
        const cJSON *date = array_item(daily, "time", index);
        const cJSON *code = array_item(daily, "weather_code", index);
        const cJSON *low = array_item(daily, "temperature_2m_min", index);
        const cJSON *high = array_item(daily, "temperature_2m_max", index);
        const cJSON *chance = array_item(daily, "precipitation_probability_max", index);
        if (!cJSON_IsString(date) || !cJSON_IsNumber(code) || !cJSON_IsNumber(low) ||
            !cJSON_IsNumber(high) || !temperature_plausible(low->valuedouble) ||
            !temperature_plausible(high->valuedouble) ||
            !parse_local_date(date->valuestring, &day->weekday, NULL)) {
            continue;
        }
        day->icon = weather_icon_from_wmo((int)code->valuedouble, true);
        day->min_c = whole(low->valuedouble);
        day->max_c = whole(high->valuedouble);
        day->precipitation_pct = cJSON_IsNumber(chance) ? whole(chance->valuedouble) : WEATHER_UNKNOWN;
        day->valid = true;
    }
}

bool weather_parse_open_meteo(const char *body, size_t length, weather_report_t *report)
{
    if (report == NULL) return false;
    start_report(report);
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
        if (ok) open_meteo_details(root, current, report);
    }
    cJSON_Delete(root);
    return ok;
}

// A unix time as minutes after local midnight, `offset` seconds east of UTC.
static int16_t local_minutes(double unix_time, double offset)
{
    if (!isfinite(unix_time) || unix_time <= 0.0) return WEATHER_UNKNOWN;
    long long local = (long long)unix_time + (long long)offset;
    local %= 86400LL;
    if (local < 0) local += 86400LL;
    return (int16_t)(local / 60LL);
}

static void openweathermap_details(const cJSON *root, const cJSON *main, weather_report_t *report)
{
    double value = 0.0;
    report->feels_like_c = whole_of(main, "feels_like");
    report->humidity_pct = whole_of(main, "humidity");
    if (number_of(main, "pressure", &value)) report->pressure_mmhg = (int16_t)weather_hpa_to_mmhg(value);
    const cJSON *wind = cJSON_GetObjectItemCaseSensitive(root, "wind");
    if (cJSON_IsObject(wind)) {
        report->wind_ms = whole_of(wind, "speed");
        report->wind_gust_ms = whole_of(wind, "gust");
        report->wind_from_deg = whole_of(wind, "deg");
    }
    const cJSON *clouds = cJSON_GetObjectItemCaseSensitive(root, "clouds");
    if (cJSON_IsObject(clouds)) report->cloud_pct = whole_of(clouds, "all");
    /* Only there while something falls: no "rain" block is no rain, which
     * is a measurement - zero - and not an unknown. */
    double fallen = 0.0;
    const cJSON *rain = cJSON_GetObjectItemCaseSensitive(root, "rain");
    const cJSON *snow = cJSON_GetObjectItemCaseSensitive(root, "snow");
    if (cJSON_IsObject(rain) && number_of(rain, "1h", &value)) fallen += value;
    if (cJSON_IsObject(snow) && number_of(snow, "1h", &value)) fallen += value;
    report->precipitation_tenths_mm = whole(fallen * 10.0);
    double offset = 0.0;
    (void)number_of(root, "timezone", &offset);
    const cJSON *sys = cJSON_GetObjectItemCaseSensitive(root, "sys");
    if (cJSON_IsObject(sys)) {
        if (number_of(sys, "sunrise", &value)) report->sunrise_min = local_minutes(value, offset);
        if (number_of(sys, "sunset", &value)) report->sunset_min = local_minutes(value, offset);
    }
}

bool weather_parse_openweathermap(const char *body, size_t length,
                                  weather_report_t *report)
{
    if (report == NULL) return false;
    start_report(report);
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
        if (ok) openweathermap_details(root, main, report);
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

/* A field's leading number - "+6°C", "47%", "1036hPa", "0.0mm" - or false
 * when it does not start with one. */
static bool field_number(const char *text, size_t length, double *value)
{
    char copy[24];
    if (length == 0U || length >= sizeof(copy)) return false;
    memcpy(copy, text, length);
    copy[length] = '\0';
    char *end = NULL;
    *value = strtod(copy, &end);
    return end != copy && isfinite(*value);
}

/* wttr.in's wind, "↖9km/h": an arrow pointing where the wind goes, then the
 * speed. The direction is turned round into where it comes from - "↓" is a
 * north wind - and the speed into metres a second. */
static void wttr_wind(const char *text, size_t length, weather_report_t *report)
{
    static const struct {
        const char *arrow;
        int16_t from_deg;
    } arrows[] = {
        {"\xE2\x86\x93", 0},   {"\xE2\x86\x99", 45},  {"\xE2\x86\x90", 90},
        {"\xE2\x86\x96", 135}, {"\xE2\x86\x91", 180}, {"\xE2\x86\x97", 225},
        {"\xE2\x86\x92", 270}, {"\xE2\x86\x98", 315},
    };
    size_t skip = 0U;
    for (size_t index = 0U; index < sizeof(arrows) / sizeof(arrows[0]); ++index) {
        if (length >= 3U && memcmp(text, arrows[index].arrow, 3U) == 0) {
            report->wind_from_deg = arrows[index].from_deg;
            skip = 3U;
            break;
        }
    }
    double kmh = 0.0;
    if (field_number(text + skip, length - skip, &kmh) && kmh >= 0.0) {
        report->wind_ms = whole(kmh / 3.6);
    }
}

bool weather_parse_wttr(const char *body, size_t length, weather_report_t *report)
{
    if (report == NULL) return false;
    start_report(report);
    if (body == NULL || length == 0U) return false;
    /* One line, as `format=%t|%x|%S|%s|%T|%f|%h|%P|%w|%p` produces it:
     * "+8°C|mmm|07:11:48|17:16:16|22:46:16+0300|+6°C|47%|1036hPa|↖9km/h|0.0mm".
     * The first five are the strip's, in the order they always had; the rest
     * are the screensaver's. Anything wttr.in says instead - "location not
     * found", "Sorry, we are running out of queries" - starts with a letter
     * and stops here. */
    char line[160];
    size_t copied = 0U;
    while (copied < length && copied + 1U < sizeof(line) && body[copied] != '\n' &&
           body[copied] != '\r') {
        line[copied] = body[copied];
        ++copied;
    }
    line[copied] = '\0';

    const char *fields[10] = {0};
    size_t lengths[10] = {0};
    size_t count = 0U;
    const char *cursor = line;
    while (count < 10U) {
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
    long sunrise = -1;
    long sunset = -1;
    if (count >= 5U) {
        sunrise = seconds_of_day(fields[2], lengths[2]);
        sunset = seconds_of_day(fields[3], lengths[3]);
        const long now = seconds_of_day(fields[4], lengths[4]);
        if (sunrise >= 0 && sunset >= 0 && now >= 0) {
            is_day = now >= sunrise && now < sunset;
        }
    }
    if (!finish(report, celsius, weather_icon_from_wttr_symbol(symbol, is_day))) return false;
    if (sunrise >= 0) report->sunrise_min = (int16_t)(sunrise / 60);
    if (sunset >= 0) report->sunset_min = (int16_t)(sunset / 60);
    double value = 0.0;
    if (count >= 6U && field_number(fields[5], lengths[5], &value) && temperature_plausible(value)) {
        report->feels_like_c = whole(value);
    }
    if (count >= 7U && field_number(fields[6], lengths[6], &value)) report->humidity_pct = whole(value);
    if (count >= 8U && field_number(fields[7], lengths[7], &value)) {
        report->pressure_mmhg = (int16_t)weather_hpa_to_mmhg(value);
    }
    if (count >= 9U) wttr_wind(fields[8], lengths[8], report);
    if (count >= 10U && field_number(fields[9], lengths[9], &value)) {
        report->precipitation_tenths_mm = whole(value * 10.0);
    }
    return true;
}

/* --- OpenWeatherMap's forecast, read by scanning ------------------------ */

// `needle` inside [from, to), or NULL.
static const char *find_in(const char *from, const char *to, const char *needle)
{
    const size_t length = strlen(needle);
    for (const char *at = from; at != NULL && at + length <= to; ++at) {
        at = memchr(at, needle[0], (size_t)(to - at));
        if (at == NULL || at + length > to) return NULL;
        if (memcmp(at, needle, length) == 0) return at;
    }
    return NULL;
}

// The number after `"key":` inside [from, to).
static bool number_in(const char *from, const char *to, const char *key, double *value)
{
    const char *at = find_in(from, to, key);
    if (at == NULL) return false;
    at += strlen(key);
    size_t length = 0U;
    while (at + length < to && length < 23U &&
           strchr("0123456789+-.eE", at[length]) != NULL && at[length] != '\0') {
        ++length;
    }
    return field_number(at, length, value);
}

bool weather_parse_openweathermap_forecast(const char *body, size_t length,
                                           weather_report_t *report)
{
    if (report == NULL) return false;
    for (int index = 0; index < WEATHER_FORECAST_DAYS; ++index) report->days[index].valid = false;
    if (body == NULL || length == 0U) return false;
    const char *end = body + length;
    const char *list = find_in(body, end, "\"list\":[");
    if (list == NULL) return false;
    /* The city block follows the list; its offset says where local midnight
     * falls, without which a step cannot be given a date. */
    const char *city = find_in(list, end, "\"city\":");
    double offset = 0.0;
    if (city != NULL) (void)number_in(city, end, "\"timezone\":", &offset);
    const char *list_end = city != NULL ? city : end;

    long long first_day = -1;
    int best_distance[WEATHER_FORECAST_DAYS];
    bool any = false;
    const char *step = find_in(list, list_end, "\"dt\":");
    while (step != NULL) {
        const char *next = find_in(step + 5, list_end, "\"dt\":");
        const char *step_end = next != NULL ? next : list_end;
        double when = 0.0;
        double low = 0.0;
        double high = 0.0;
        if (number_in(step, step_end, "\"dt\":", &when) &&
            number_in(step, step_end, "\"temp_min\":", &low) &&
            number_in(step, step_end, "\"temp_max\":", &high) && temperature_plausible(low) &&
            temperature_plausible(high)) {
            const long long local = (long long)when + (long long)offset;
            const long long day_number = local >= 0 ? local / 86400LL : (local - 86399LL) / 86400LL;
            if (first_day < 0) first_day = day_number;
            const long long index = day_number - first_day;
            if (index >= 0 && index < WEATHER_FORECAST_DAYS) {
                weather_day_t *day = &report->days[index];
                const int hour = (int)((local - day_number * 86400LL) / 3600LL);
                double chance = -1.0;
                (void)number_in(step, step_end, "\"pop\":", &chance);
                const char *weather = find_in(step, step_end, "\"weather\":[");
                double condition = 0.0;
                const bool has_condition =
                    weather != NULL && number_in(weather, step_end, "\"id\":", &condition);
                if (!day->valid) {
                    day->valid = true;
                    day->min_c = whole(low);
                    day->max_c = whole(high);
                    day->precipitation_pct = WEATHER_UNKNOWN;
                    day->icon = WEATHER_ICON_NONE;
                    // 1970-01-01 was a Thursday.
                    day->weekday = (uint8_t)((day_number % 7 + 7 + 4) % 7);
                    best_distance[index] = 99;
                }
                if (whole(low) < day->min_c) day->min_c = whole(low);
                if (whole(high) > day->max_c) day->max_c = whole(high);
                if (chance >= 0.0) {
                    const int16_t percent = whole(chance * 100.0);
                    if (day->precipitation_pct == WEATHER_UNKNOWN || percent > day->precipitation_pct) {
                        day->precipitation_pct = percent;
                    }
                }
                const int distance = abs(hour - 12);
                if (has_condition && distance < best_distance[index]) {
                    best_distance[index] = distance;
                    day->icon = weather_icon_from_openweathermap((int)condition, true);
                }
                any = true;
            }
        }
        step = next;
    }
    return any;
}

/* --- wttr.in's forecast, read by scanning ------------------------------- */

weather_icon_t weather_icon_from_wwo(int code)
{
    /* Grouped as wttr.in's own table groups them (its WWO_CODE), and drawn
     * as its one-line symbols are above: a snow shower is snow, freezing
     * drizzle and ice pellets are sleet. */
    switch (code) {
    case 113: return WEATHER_ICON_CLEAR_DAY;
    case 116: return WEATHER_ICON_PARTLY_CLOUDY_DAY;
    case 119:
    case 122: return WEATHER_ICON_CLOUDY;
    case 143:
    case 248:
    case 260: return WEATHER_ICON_FOG;
    case 176: case 263: case 266: case 293: case 296: case 299: case 302:
    case 305: case 308: case 353: case 356: case 359:
        return WEATHER_ICON_RAIN;
    case 179: case 182: case 185: case 281: case 284: case 311: case 314:
    case 317: case 350: case 362: case 365: case 374: case 377:
        return WEATHER_ICON_SLEET;
    case 227: case 230: case 320: case 323: case 326: case 329: case 332:
    case 335: case 338: case 368: case 371: case 395:
        return WEATHER_ICON_SNOW;
    case 200: case 386: case 389: case 392:
        return WEATHER_ICON_THUNDERSTORM;
    default: return WEATHER_ICON_NONE;
    }
}

/* The number after `key` inside [from, to), where the key is followed by
 * spaces, a colon, more spaces and maybe a quote: wttr.in indents its JSON
 * and quotes every number. */
static bool quoted_number_in(const char *from, const char *to, const char *key, double *value)
{
    const char *at = find_in(from, to, key);
    if (at == NULL) return false;
    at += strlen(key);
    while (at < to && *at == ' ') ++at;
    if (at >= to || *at != ':') return false;
    ++at;
    while (at < to && *at == ' ') ++at;
    if (at < to && *at == '"') ++at;
    size_t length = 0U;
    while (at + length < to && length < 23U && strchr("0123456789+-.", at[length]) != NULL &&
           at[length] != '\0') {
        ++length;
    }
    return field_number(at, length, value);
}

/* 0 Sunday .. 6 Saturday for "YYYY-MM-DD" at `at`; -1 when it is not one. */
static int weekday_of_date(const char *at, const char *to)
{
    if (to - at < 10 || at[4] != '-' || at[7] != '-') return -1;
    int parts[3] = {0, 0, 0};
    const int widths[3] = {4, 2, 2};
    const int starts[3] = {0, 5, 8};
    for (int part = 0; part < 3; ++part) {
        for (int i = 0; i < widths[part]; ++i) {
            const char c = at[starts[part] + i];
            if (c < '0' || c > '9') return -1;
            parts[part] = parts[part] * 10 + (c - '0');
        }
    }
    int year = parts[0];
    const int month = parts[1];
    const int day = parts[2];
    if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
    // Days since 1970-01-01 by the civil-calendar count, then from its Thursday.
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int year_of_era = year - era * 400;
    const int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const long days = (long)era * 146097L + day_of_era - 719468L;
    return (int)(((days % 7) + 7 + 4) % 7);
}

bool weather_parse_wttr_forecast(const char *body, size_t length, weather_report_t *report)
{
    if (report == NULL) return false;
    for (int index = 0; index < WEATHER_FORECAST_DAYS; ++index) report->days[index].valid = false;
    if (body == NULL || length == 0U) return false;
    const char *end = body + length;
    const char *weather = find_in(body, end, "\"weather\"");
    if (weather == NULL) return false;
    /* A day's object opens with its astronomy and then its date; from one
     * date to the next is that day's figures and hours, and the next day's
     * sun and moon, which hold none of the keys read here. */
    const char *date = find_in(weather, end, "\"date\"");
    bool any = false;
    for (int index = 0; index < WEATHER_FORECAST_DAYS && date != NULL; ++index) {
        const char *next = find_in(date + 6, end, "\"date\"");
        const char *day_end = next != NULL ? next : end;
        double low = 0.0;
        double high = 0.0;
        const char *date_value = date + 6;
        while (date_value < day_end && (*date_value == ' ' || *date_value == ':')) ++date_value;
        if (date_value < day_end && *date_value == '"') ++date_value;
        const int weekday = weekday_of_date(date_value, day_end);
        if (weekday >= 0 && quoted_number_in(date, day_end, "\"mintempC\"", &low) &&
            quoted_number_in(date, day_end, "\"maxtempC\"", &high) &&
            temperature_plausible(low) && temperature_plausible(high)) {
            weather_day_t *day = &report->days[index];
            day->valid = true;
            day->min_c = whole(low);
            day->max_c = whole(high);
            day->precipitation_pct = WEATHER_UNKNOWN;
            day->icon = WEATHER_ICON_NONE;
            day->weekday = (uint8_t)weekday;
            /* The hours: each step carries one of every key, so the n-th
             * time, code and chances are all the n-th step's - which saves
             * finding where a step ends among its nested arrays. */
            const char *hours = find_in(date, day_end, "\"hourly\"");
            const char *time_at = hours != NULL ? find_in(hours, day_end, "\"time\"") : NULL;
            const char *code_at = hours != NULL ? find_in(hours, day_end, "\"weatherCode\"") : NULL;
            const char *rain_at = hours != NULL ? find_in(hours, day_end, "\"chanceofrain\"") : NULL;
            const char *snow_at = hours != NULL ? find_in(hours, day_end, "\"chanceofsnow\"") : NULL;
            int best_distance = 9999;
            while (time_at != NULL) {
                double when = 0.0;
                double code = 0.0;
                double rain = -1.0;
                double snow = -1.0;
                const bool has_time = quoted_number_in(time_at, day_end, "\"time\"", &when);
                const bool has_code =
                    code_at != NULL && quoted_number_in(code_at, day_end, "\"weatherCode\"", &code);
                if (rain_at != NULL) (void)quoted_number_in(rain_at, day_end, "\"chanceofrain\"", &rain);
                if (snow_at != NULL) (void)quoted_number_in(snow_at, day_end, "\"chanceofsnow\"", &snow);
                const double chance = rain > snow ? rain : snow;
                if (chance >= 0.0) {
                    const int16_t percent = whole(chance);
                    if (day->precipitation_pct == WEATHER_UNKNOWN || percent > day->precipitation_pct) {
                        day->precipitation_pct = percent;
                    }
                }
                // "1200" is noon: hours and minutes run together.
                const int distance = has_time ? abs((int)when - 1200) : 9999;
                if (has_code && distance < best_distance) {
                    best_distance = distance;
                    day->icon = weather_icon_from_wwo((int)code);
                }
                time_at = find_in(time_at + 6, day_end, "\"time\"");
                code_at = code_at != NULL ? find_in(code_at + 13, day_end, "\"weatherCode\"") : NULL;
                rain_at = rain_at != NULL ? find_in(rain_at + 14, day_end, "\"chanceofrain\"") : NULL;
                snow_at = snow_at != NULL ? find_in(snow_at + 14, day_end, "\"chanceofsnow\"") : NULL;
            }
            any = true;
        }
        date = next;
    }
    return any;
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
        /* The current weather and three days in one answer of about 1.3 KB:
         * pressure at sea level, as a forecast gives it, and the wind in
         * metres a second. */
        written = snprintf(url, url_size,
                           "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                           "&current=temperature_2m,weather_code,is_day,apparent_temperature,"
                           "relative_humidity_2m,pressure_msl,wind_speed_10m,wind_gusts_10m,"
                           "wind_direction_10m,precipitation,cloud_cover"
                           "&daily=weather_code,temperature_2m_min,temperature_2m_max,"
                           "precipitation_probability_max,sunrise,sunset"
                           "&forecast_days=3&timezone=auto&wind_speed_unit=ms",
                           latitude, longitude);
        break;
    case DEVICE_WEATHER_WTTR:
        /* The format string travels URL-encoded: `%` and `|` are not
         * characters a request line may carry bare, whatever curl lets
         * through. */
        written = snprintf(url, url_size,
                           "http://wttr.in/%s,%s?format=%%25t%%7C%%25x%%7C%%25S%%7C%%25s%%7C%%25T"
                           "%%7C%%25f%%7C%%25h%%7C%%25P%%7C%%25w%%7C%%25p",
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

size_t weather_forecast_url(char *url, size_t url_size, device_weather_provider_t provider,
                            const char *latitude, const char *longitude, const char *key)
{
    if (url == NULL || url_size == 0U) return 0U;
    url[0] = '\0';
    if (latitude == NULL || longitude == NULL || latitude[0] == '\0' || longitude[0] == '\0') {
        return 0U;
    }
    if (provider == DEVICE_WEATHER_WTTR) {
        const int written = snprintf(url, url_size, "http://wttr.in/%s,%s?format=j1", latitude, longitude);
        if (written <= 0 || (size_t)written >= url_size) {
            url[0] = '\0';
            return 0U;
        }
        return (size_t)written;
    }
    if (provider != DEVICE_WEATHER_OPENWEATHERMAP || key == NULL || key[0] == '\0') return 0U;
    /* Twenty-four three-hour steps: today's rest and the two days after,
     * whatever the hour. */
    const int written = snprintf(url, url_size,
                                 "http://api.openweathermap.org/data/2.5/forecast?lat=%s&lon=%s"
                                 "&units=metric&cnt=24&appid=%s",
                                 latitude, longitude, key);
    if (written <= 0 || (size_t)written >= url_size) {
        url[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}
