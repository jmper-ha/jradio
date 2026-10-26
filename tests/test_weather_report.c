#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "weather_report.h"

/* Captured off the wire on 2026-09-11 from this PC, Moscow's coordinates. The
 * OpenWeatherMap document is the documented shape rather than a capture: the
 * key was not to hand that day. When one is, replace it with a real answer. */
static const char k_open_meteo[] =
    "{\"latitude\":55.75,\"longitude\":37.625,\"generationtime_ms\":0.037670135498046875,"
    "\"utc_offset_seconds\":10800,\"timezone\":\"Europe/Moscow\",\"timezone_abbreviation\":"
    "\"GMT+3\",\"elevation\":152.0,\"current_units\":{\"time\":\"iso8601\",\"interval\":"
    "\"seconds\",\"temperature_2m\":\"°C\",\"weather_code\":\"wmo code\",\"is_day\":\"\"},"
    "\"current\":{\"time\":\"2026-09-11T20:00\",\"interval\":900,\"temperature_2m\":12.7,"
    "\"weather_code\":2,\"is_day\":0}}";

static const char k_wttr[] = "+13°C|o|05:53:09|18:59:44|20:17:45+0300\n";

static const char k_openweathermap[] =
    "{\"coord\":{\"lon\":37.62,\"lat\":55.75},\"weather\":[{\"id\":802,\"main\":\"Clouds\","
    "\"description\":\"scattered clouds\",\"icon\":\"03n\"}],\"base\":\"stations\",\"main\":"
    "{\"temp\":12.66,\"feels_like\":11.9,\"temp_min\":11.2,\"temp_max\":13.5,\"pressure\":1017,"
    "\"humidity\":76},\"visibility\":10000,\"wind\":{\"speed\":2.1,\"deg\":230},\"clouds\":"
    "{\"all\":40},\"dt\":1789149600,\"sys\":{\"type\":2,\"id\":2000,\"country\":\"RU\","
    "\"sunrise\":1789098789,\"sunset\":1789145984},\"timezone\":10800,\"id\":524901,"
    "\"name\":\"Moscow\",\"cod\":200}";

static void test_open_meteo_answer_becomes_a_report(void)
{
    weather_report_t report;
    assert(weather_parse_open_meteo(k_open_meteo, strlen(k_open_meteo), &report));
    assert(report.valid);
    assert(report.temperature_c == 13);
    /* Code 2 is partly cloudy, and is_day 0 says the sun is down. */
    assert(report.icon == WEATHER_ICON_PARTLY_CLOUDY_NIGHT);

    /* Not NUL-terminated on the wire: the length is what bounds the parse. */
    char buffer[sizeof(k_open_meteo) + 8];
    memset(buffer, 'x', sizeof(buffer));
    memcpy(buffer, k_open_meteo, strlen(k_open_meteo));
    assert(weather_parse_open_meteo(buffer, strlen(k_open_meteo), &report));
    assert(report.temperature_c == 13);

    /* An answer without a current block - the request asked for something
     * else, or the service changed - is not a report. */
    static const char no_current[] = "{\"latitude\":55.75,\"hourly\":{}}";
    assert(!weather_parse_open_meteo(no_current, strlen(no_current), &report));
    assert(!report.valid);
    assert(!weather_parse_open_meteo("not json", 8U, &report));
    assert(!weather_parse_open_meteo(NULL, 0U, &report));
    assert(!weather_parse_open_meteo(k_open_meteo, 0U, &report));

    /* A flag left out reads as day. */
    static const char no_flag[] = "{\"current\":{\"temperature_2m\":-3.5,\"weather_code\":71}}";
    assert(weather_parse_open_meteo(no_flag, strlen(no_flag), &report));
    assert(report.temperature_c == -4);
    assert(report.icon == WEATHER_ICON_SNOW);
}

static void test_wttr_line_becomes_a_report(void)
{
    weather_report_t report;
    assert(weather_parse_wttr(k_wttr, strlen(k_wttr), &report));
    assert(report.temperature_c == 13);
    /* "o" is Sunny, but 20:17 is past the 18:59 sunset: the moon, not the
     * sun. That is the whole reason the three times ride on the line. */
    assert(report.icon == WEATHER_ICON_CLEAR_NIGHT);

    static const char midday[] = "-2°C|m|08:30:00|16:45:00|12:00:00+0300";
    assert(weather_parse_wttr(midday, strlen(midday), &report));
    assert(report.temperature_c == -2);
    assert(report.icon == WEATHER_ICON_PARTLY_CLOUDY_DAY);

    /* Without the times, day. Without the symbol, nothing. */
    static const char bare[] = "+5°C|///";
    assert(weather_parse_wttr(bare, strlen(bare), &report));
    assert(report.icon == WEATHER_ICON_RAIN);
    assert(!weather_parse_wttr("+5°C", 6U, &report));

    /* What the service says instead of weather, none of which is weather. */
    static const char not_found[] = "location not found: location not found";
    assert(!weather_parse_wttr(not_found, strlen(not_found), &report));
    static const char busy[] = "Sorry, we are running out of queries to the weather service";
    assert(!weather_parse_wttr(busy, strlen(busy), &report));
    static const char html[] = "<!DOCTYPE html><html>";
    assert(!weather_parse_wttr(html, strlen(html), &report));
    assert(!weather_parse_wttr("", 0U, &report));

    /* A symbol the table does not have keeps the temperature and draws no
     * picture, rather than throwing the reading away. */
    static const char odd[] = "+9°C|?";
    assert(weather_parse_wttr(odd, strlen(odd), &report));
    assert(report.valid);
    assert(report.icon == WEATHER_ICON_NONE);
}

static void test_openweathermap_answer_becomes_a_report(void)
{
    weather_report_t report;
    assert(weather_parse_openweathermap(k_openweathermap, strlen(k_openweathermap), &report));
    assert(report.temperature_c == 13);
    /* 802 is scattered clouds, and the icon "03n" ends in night. */
    assert(report.icon == WEATHER_ICON_PARTLY_CLOUDY_NIGHT);

    /* The refusal a wrong key gets: a document, but not a report. */
    static const char refused[] =
        "{\"cod\":401, \"message\": \"Invalid API key. Please see "
        "https://openweathermap.org/faq#error401 for more info.\"}";
    assert(!weather_parse_openweathermap(refused, strlen(refused), &report));
    assert(!report.valid);

    /* Kelvin - the units parameter lost on the way - is not a temperature
     * this panel shows. */
    static const char kelvin[] =
        "{\"weather\":[{\"id\":800,\"icon\":\"01d\"}],\"main\":{\"temp\":285.8}}";
    assert(!weather_parse_openweathermap(kelvin, strlen(kelvin), &report));
}

static void test_the_three_tables_agree_on_the_sky(void)
{
    /* Day and night matter only where the sky itself is the picture. */
    assert(weather_icon_from_wmo(0, true) == WEATHER_ICON_CLEAR_DAY);
    assert(weather_icon_from_wmo(0, false) == WEATHER_ICON_CLEAR_NIGHT);
    assert(weather_icon_from_wmo(1, true) == WEATHER_ICON_CLEAR_DAY);
    assert(weather_icon_from_wmo(2, false) == WEATHER_ICON_PARTLY_CLOUDY_NIGHT);
    assert(weather_icon_from_wmo(3, false) == WEATHER_ICON_CLOUDY);
    assert(weather_icon_from_wmo(45, true) == WEATHER_ICON_FOG);
    assert(weather_icon_from_wmo(48, true) == WEATHER_ICON_FOG);
    assert(weather_icon_from_wmo(53, true) == WEATHER_ICON_RAIN);
    assert(weather_icon_from_wmo(57, true) == WEATHER_ICON_SLEET);
    assert(weather_icon_from_wmo(65, true) == WEATHER_ICON_RAIN);
    assert(weather_icon_from_wmo(67, true) == WEATHER_ICON_SLEET);
    assert(weather_icon_from_wmo(77, true) == WEATHER_ICON_SNOW);
    assert(weather_icon_from_wmo(82, true) == WEATHER_ICON_RAIN);
    assert(weather_icon_from_wmo(86, true) == WEATHER_ICON_SNOW);
    assert(weather_icon_from_wmo(95, true) == WEATHER_ICON_THUNDERSTORM);
    assert(weather_icon_from_wmo(99, true) == WEATHER_ICON_THUNDERSTORM);
    assert(weather_icon_from_wmo(4, true) == WEATHER_ICON_NONE);
    assert(weather_icon_from_wmo(-1, true) == WEATHER_ICON_NONE);

    assert(weather_icon_from_openweathermap(211, true) == WEATHER_ICON_THUNDERSTORM);
    assert(weather_icon_from_openweathermap(301, true) == WEATHER_ICON_RAIN);
    assert(weather_icon_from_openweathermap(500, true) == WEATHER_ICON_RAIN);
    assert(weather_icon_from_openweathermap(511, true) == WEATHER_ICON_SLEET);
    assert(weather_icon_from_openweathermap(600, true) == WEATHER_ICON_SNOW);
    assert(weather_icon_from_openweathermap(611, true) == WEATHER_ICON_SLEET);
    assert(weather_icon_from_openweathermap(616, true) == WEATHER_ICON_SLEET);
    assert(weather_icon_from_openweathermap(620, true) == WEATHER_ICON_SNOW);
    assert(weather_icon_from_openweathermap(741, true) == WEATHER_ICON_FOG);
    assert(weather_icon_from_openweathermap(800, false) == WEATHER_ICON_CLEAR_NIGHT);
    assert(weather_icon_from_openweathermap(801, true) == WEATHER_ICON_PARTLY_CLOUDY_DAY);
    assert(weather_icon_from_openweathermap(804, false) == WEATHER_ICON_CLOUDY);
    assert(weather_icon_from_openweathermap(900, true) == WEATHER_ICON_NONE);

    /* All nineteen of wttr.in's symbols land somewhere, and "?" lands on
     * nothing on purpose. */
    static const char *const symbols[] = {"o", "m", "mm", "mmm", "=", "/", ".", "//", "///",
                                          "*", "*/", "**", "*/*", "x", "x/", "/!/", "!/",
                                          "*!*"};
    for (size_t index = 0U; index < sizeof(symbols) / sizeof(symbols[0]); ++index) {
        assert(weather_icon_from_wttr_symbol(symbols[index], true) != WEATHER_ICON_NONE);
    }
    assert(weather_icon_from_wttr_symbol("?", true) == WEATHER_ICON_NONE);
    assert(weather_icon_from_wttr_symbol("o", false) == WEATHER_ICON_CLEAR_NIGHT);
    assert(weather_icon_from_wttr_symbol("mm", false) == WEATHER_ICON_CLOUDY);
    assert(weather_icon_from_wttr_symbol("*!*", true) == WEATHER_ICON_THUNDERSTORM);
    assert(weather_icon_from_wttr_symbol(NULL, true) == WEATHER_ICON_NONE);
    assert(weather_icon_from_wttr_symbol("", true) == WEATHER_ICON_NONE);
}

static void test_the_request_is_plain_http_and_carries_the_coordinates(void)
{
    char url[WEATHER_URL_MAX];
    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OPEN_METEO, "55.75", "37.62",
                               NULL) > 0U);
    assert(strncmp(url, "http://api.open-meteo.com/v1/forecast?latitude=55.75&longitude=37.62"
                        "&current=temperature_2m,weather_code,is_day,", 96U) == 0);
    // Everything the screensaver shows, in one answer.
    assert(strstr(url, "pressure_msl") != NULL && strstr(url, "&daily=") != NULL &&
           strstr(url, "&forecast_days=3&timezone=auto&wind_speed_unit=ms") != NULL);

    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_WTTR, "-33.8688", "151.2093",
                               NULL) > 0U);
    /* The format is asked for encoded: a bare `%` or `|` is not a request line. */
    assert(strcmp(url, "http://wttr.in/-33.8688,151.2093?format=%25t%7C%25x%7C%25S%7C%25s%7C%25T"
                       "%7C%25f%7C%25h%7C%25P%7C%25w%7C%25p") == 0);
    assert(strchr(url, '|') == NULL);

    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OPENWEATHERMAP, "55.75", "37.62",
                               "0123456789abcdef0123456789abcdef") > 0U);
    assert(strcmp(url, "http://api.openweathermap.org/data/2.5/weather?lat=55.75&lon=37.62"
                       "&units=metric&appid=0123456789abcdef0123456789abcdef") == 0);
    /* Without a key there is no request to make, not a request that fails. */
    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OPENWEATHERMAP, "55.75", "37.62",
                               "") == 0U);
    assert(url[0] == '\0');
    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OPENWEATHERMAP, "55.75", "37.62",
                               NULL) == 0U);

    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OFF, "55.75", "37.62", NULL) == 0U);
    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OPEN_METEO, "", "37.62", NULL) == 0U);
    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_OPEN_METEO, "55.75", NULL, NULL) == 0U);
    char tight[40];
    assert(weather_request_url(tight, sizeof(tight), DEVICE_WEATHER_OPEN_METEO, "55.75", "37.62",
                               NULL) == 0U);
    assert(tight[0] == '\0');

    // The second request is OpenWeatherMap's and wttr.in's.
    assert(weather_forecast_url(url, sizeof(url), DEVICE_WEATHER_OPENWEATHERMAP, "55.75", "37.62",
                                "k") > 0U);
    assert(strcmp(url, "http://api.openweathermap.org/data/2.5/forecast?lat=55.75&lon=37.62"
                       "&units=metric&cnt=24&appid=k") == 0);
    assert(weather_forecast_url(url, sizeof(url), DEVICE_WEATHER_OPENWEATHERMAP, "55.75", "37.62",
                                "") == 0U);
    assert(weather_forecast_url(url, sizeof(url), DEVICE_WEATHER_OPEN_METEO, "55.75", "37.62",
                                "k") == 0U);
    assert(weather_forecast_url(url, sizeof(url), DEVICE_WEATHER_WTTR, "55.75", "37.62", NULL) > 0U);
    assert(strcmp(url, "http://wttr.in/55.75,37.62?format=j1") == 0);
    assert(weather_forecast_url(url, sizeof(url), DEVICE_WEATHER_WTTR, "", "37.62", NULL) == 0U);

    /* Every request is http://, which is the whole point of these three. */
    for (device_weather_provider_t provider = DEVICE_WEATHER_OPEN_METEO;
         provider <= DEVICE_WEATHER_OPENWEATHERMAP; ++provider) {
        assert(weather_request_url(url, sizeof(url), provider, "1", "2", "key") > 0U);
        assert(strncmp(url, "http://", 7U) == 0);
    }
}

static void test_the_reading_is_whole_degrees_with_a_sign(void)
{
    assert(weather_round_temperature(12.4) == 12);
    assert(weather_round_temperature(12.5) == 13);
    assert(weather_round_temperature(-0.4) == 0);
    assert(weather_round_temperature(-0.5) == -1);
    assert(weather_round_temperature(-12.5) == -13);

    char text[16];
    weather_report_t report = {.valid = true, .temperature_c = 12};
    weather_temperature_text(text, sizeof(text), &report);
    assert(strcmp(text, "+12°") == 0);
    report.temperature_c = -3;
    weather_temperature_text(text, sizeof(text), &report);
    assert(strcmp(text, "-3°") == 0);
    report.temperature_c = 0;
    weather_temperature_text(text, sizeof(text), &report);
    assert(strcmp(text, "0°") == 0);
    report.valid = false;
    weather_temperature_text(text, sizeof(text), &report);
    assert(text[0] == '\0');
    weather_temperature_text(text, sizeof(text), NULL);
    assert(text[0] == '\0');
    /* The degree sign is two bytes, and the widest reading has to fit the
     * label the strip gives it. */
    report = (weather_report_t){.valid = true, .temperature_c = -45};
    weather_temperature_text(text, sizeof(text), &report);
    assert(strlen(text) == 5U);
}

static void test_the_key_and_the_icon_names(void)
{
    assert(weather_key_valid("0123456789abcdef0123456789abcdef"));
    assert(weather_key_valid("abcdefgh"));
    assert(!weather_key_valid("abcdefg"));
    assert(!weather_key_valid(""));
    assert(!weather_key_valid(NULL));
    /* A quote or a backslash would break the JSON file, an ampersand or a
     * percent the URL, a space either. */
    assert(!weather_key_valid("0123456789abcdef\"0123456789abcde"));
    assert(!weather_key_valid("0123456789abcdef\\0123456789abcde"));
    assert(!weather_key_valid("0123456789abcdef&0123456789abcde"));
    assert(!weather_key_valid("0123456789abcdef%0123456789abcde"));
    assert(!weather_key_valid("0123456789abcdef 0123456789abcde"));
    char long_key[WEATHER_KEY_MAX + 2];
    memset(long_key, 'a', sizeof(long_key) - 1U);
    long_key[sizeof(long_key) - 1U] = '\0';
    assert(!weather_key_valid(long_key));
    long_key[WEATHER_KEY_MAX] = '\0';
    assert(weather_key_valid(long_key));

    /* Every picture has a name, no two share one, and nothing is "none" but
     * none. */
    for (weather_icon_t icon = WEATHER_ICON_NONE; icon < WEATHER_ICON_COUNT; ++icon) {
        const char *name = weather_icon_name(icon);
        assert(name != NULL && name[0] != '\0');
        assert((strcmp(name, "none") == 0) == (icon == WEATHER_ICON_NONE));
        for (weather_icon_t other = WEATHER_ICON_NONE; other < icon; ++other) {
            assert(strcmp(weather_icon_name(other), name) != 0);
        }
    }
    assert(strcmp(weather_icon_name(WEATHER_ICON_COUNT), "none") == 0);
}

static char *read_fixture(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    static char buffer[4096];
    *length = fread(buffer, 1U, sizeof(buffer), file);
    fclose(file);
    return buffer;
}

/* The answers to the screensaver's requests, captured on 2026-10-21 from this
 * PC at Moscow's coordinates. */
static void test_the_full_open_meteo_answer(void)
{
    size_t length = 0U;
    const char *body = read_fixture("tests/fixtures/weather_open_meteo.json", &length);
    weather_report_t report;
    assert(weather_parse_open_meteo(body, length, &report));
    assert(report.temperature_c == 6 && report.icon == WEATHER_ICON_CLEAR_NIGHT);
    assert(report.feels_like_c == 3);
    assert(report.humidity_pct == 63);
    // 1035.4 hPa at sea level.
    assert(report.pressure_mmhg == 777);
    assert(report.wind_ms == 1 && report.wind_gust_ms == 2 && report.wind_from_deg == 100);
    assert(weather_wind_point(report.wind_from_deg) == 2);  // east
    assert(report.cloud_pct == 32 && report.precipitation_tenths_mm == 0);
    assert(report.sunrise_min == 7 * 60 + 12 && report.sunset_min == 17 * 60 + 15);
    // Wednesday, Thursday, Friday: overcast, then rain.
    assert(report.days[0].valid && report.days[0].weekday == 3);
    assert(report.days[0].icon == WEATHER_ICON_CLOUDY);
    assert(report.days[0].min_c == 2 && report.days[0].max_c == 12);
    assert(report.days[0].precipitation_pct == 3);
    assert(report.days[2].valid && report.days[2].weekday == 5);
    assert(report.days[2].icon == WEATHER_ICON_RAIN && report.days[2].precipitation_pct == 50);
    assert(report.days[2].min_c == 5 && report.days[2].max_c == 11);

    // The strip's old, short answer still gives what it always did.
    assert(weather_parse_open_meteo(k_open_meteo, strlen(k_open_meteo), &report));
    assert(report.feels_like_c == WEATHER_UNKNOWN && !report.days[0].valid);
    assert(report.sunrise_min == WEATHER_UNKNOWN);
}

static void test_the_full_wttr_line(void)
{
    size_t length = 0U;
    const char *body = read_fixture("tests/fixtures/weather_wttr.txt", &length);
    weather_report_t report;
    assert(weather_parse_wttr(body, length, &report));
    assert(report.temperature_c == 8 && report.icon == WEATHER_ICON_CLOUDY);
    assert(report.feels_like_c == 6 && report.humidity_pct == 47);
    assert(report.pressure_mmhg == 777);  // 1036 hPa
    // "↖9km/h": blowing north-west, so from the south-east, at 2.5 m/s.
    assert(report.wind_from_deg == 135 && report.wind_ms == 3);
    assert(report.wind_gust_ms == WEATHER_UNKNOWN && report.cloud_pct == WEATHER_UNKNOWN);
    assert(report.precipitation_tenths_mm == 0);
    assert(report.sunrise_min == 7 * 60 + 11 && report.sunset_min == 17 * 60 + 16);
    assert(!report.days[0].valid);  // no forecast from this service
}

static void test_openweathermap_details_and_forecast(void)
{
    weather_report_t report;
    assert(weather_parse_openweathermap(k_openweathermap, strlen(k_openweathermap), &report));
    assert(report.feels_like_c == 12 && report.humidity_pct == 76);
    assert(report.pressure_mmhg == 763);  // 1017 hPa
    assert(report.wind_ms == 2 && report.wind_from_deg == 230);
    assert(report.wind_gust_ms == WEATHER_UNKNOWN);
    assert(report.cloud_pct == 40);
    // No rain block is no rain: a reading, not an unknown.
    assert(report.precipitation_tenths_mm == 0);
    // 1789098789 is 03:53 UTC, 06:53 at UTC+3.
    assert(report.sunrise_min == 6 * 60 + 53);

    /* The documented shape of /data/2.5/forecast, cut to five steps: one at
     * 21:00 on a Saturday local (UTC+3), four on the Sunday - 00:00, 03:00,
     * 12:00, 15:00. The dt_txt strings are not read. */
    static const char forecast[] =
        "{\"cod\":\"200\",\"message\":0,\"cnt\":5,\"list\":["
        "{\"dt\":1789236000,\"main\":{\"temp\":10.1,\"feels_like\":9.0,\"temp_min\":9.6,"
        "\"temp_max\":10.4,\"pressure\":1015,\"humidity\":80},\"weather\":[{\"id\":803,"
        "\"main\":\"Clouds\",\"icon\":\"04d\"}],\"clouds\":{\"all\":75},\"wind\":{\"speed\":3.1,"
        "\"deg\":200},\"visibility\":10000,\"pop\":0.1,\"sys\":{\"pod\":\"d\"},"
        "\"dt_txt\":\"2026-09-11 18:00:00\"},"
        "{\"dt\":1789246800,\"main\":{\"temp\":8.0,\"temp_min\":7.5,\"temp_max\":8.2},"
        "\"weather\":[{\"id\":800,\"icon\":\"01n\"}],\"pop\":0,\"dt_txt\":\"2026-09-11 21:00:00\"},"
        "{\"dt\":1789257600,\"main\":{\"temp\":6.0,\"temp_min\":5.4,\"temp_max\":6.1},"
        "\"weather\":[{\"id\":800,\"icon\":\"01n\"}],\"pop\":0,\"dt_txt\":\"2026-09-12 00:00:00\"},"
        "{\"dt\":1789290000,\"main\":{\"temp\":14.0,\"temp_min\":13.2,\"temp_max\":14.4},"
        "\"weather\":[{\"id\":500,\"icon\":\"10d\"}],\"pop\":0.64,\"rain\":{\"3h\":0.4},"
        "\"dt_txt\":\"2026-09-12 09:00:00\"},"
        "{\"dt\":1789300800,\"main\":{\"temp\":16.0,\"temp_min\":15.7,\"temp_max\":16.3},"
        "\"weather\":[{\"id\":801,\"icon\":\"02d\"}],\"pop\":0.3,\"dt_txt\":\"2026-09-12 12:00:00\"}"
        "],\"city\":{\"id\":524901,\"name\":\"Moscow\",\"coord\":{\"lat\":55.75,\"lon\":37.62},"
        "\"country\":\"RU\",\"population\":1000000,\"timezone\":10800,\"sunrise\":1789184392,"
        "\"sunset\":1789231456}}";
    assert(weather_parse_openweathermap_forecast(forecast, strlen(forecast), &report));
    // The current weather is left as it was.
    assert(report.temperature_c == 13 && report.humidity_pct == 76);
    assert(report.days[0].valid && report.days[0].weekday == 6);  // Saturday
    assert(report.days[0].min_c == 10 && report.days[0].max_c == 10);
    assert(report.days[0].precipitation_pct == 10);
    assert(report.days[0].icon == WEATHER_ICON_CLOUDY);  // its only step
    assert(report.days[1].valid && report.days[1].weekday == 0);  // Sunday
    assert(report.days[1].min_c == 5 && report.days[1].max_c == 16);
    assert(report.days[1].precipitation_pct == 64);
    // The noon step is rain, though the night's were clear.
    assert(report.days[1].icon == WEATHER_ICON_RAIN);
    assert(!report.days[2].valid);

    static const char refused[] = "{\"cod\":401,\"message\":\"Invalid API key\"}";
    assert(!weather_parse_openweathermap_forecast(refused, strlen(refused), &report));
    assert(!report.days[0].valid);
}

/* wttr.in's j1, cut down to what is read and laid out as it comes: indented,
 * every number quoted, a day's sun and moon before its date. The current
 * block carries keys of the same names and is not a day. */
static void test_the_wttr_forecast(void)
{
    static const char j1[] =
        "{\n  \"current_condition\": [\n    {\n      \"temp_C\": \"9\",\n"
        "      \"weatherCode\": \"338\"\n    }\n  ],\n"
        "  \"weather\": [\n"
        "    {\n      \"astronomy\": [ { \"sunrise\": \"07:23 AM\" } ],\n"
        "      \"avgtempC\": \"10\",\n      \"date\": \"2026-10-26\",\n"
        "      \"hourly\": [\n"
        "        { \"chanceofrain\": \"10\", \"chanceofsnow\": \"0\", \"time\": \"900\",\n"
        "          \"weatherCode\": \"113\", \"weatherDesc\": [ { \"value\": \"Sunny\" } ] },\n"
        "        { \"chanceofrain\": \"30\", \"chanceofsnow\": \"0\", \"time\": \"1200\",\n"
        "          \"weatherCode\": \"296\", \"weatherDesc\": [ { \"value\": \"Light rain\" } ] },\n"
        "        { \"chanceofrain\": \"20\", \"chanceofsnow\": \"0\", \"time\": \"1500\",\n"
        "          \"weatherCode\": \"119\", \"weatherDesc\": [ { \"value\": \"Cloudy\" } ] }\n"
        "      ],\n      \"maxtempC\": \"15\",\n      \"mintempC\": \"8\"\n    },\n"
        "    {\n      \"astronomy\": [ { \"sunrise\": \"07:25 AM\" } ],\n"
        "      \"date\": \"2026-10-27\",\n      \"hourly\": [\n"
        "        { \"chanceofrain\": \"0\", \"chanceofsnow\": \"70\", \"time\": \"1200\",\n"
        "          \"weatherCode\": \"326\" }\n"
        "      ],\n      \"maxtempC\": \"1\",\n      \"mintempC\": \"-4\"\n    },\n"
        "    {\n      \"date\": \"2026-10-28\",\n      \"hourly\": [],\n"
        "      \"maxtempC\": \"3\",\n      \"mintempC\": \"-1\"\n    }\n  ]\n}\n";
    weather_report_t report;
    memset(&report, 0, sizeof(report));
    report.temperature_c = 9;
    assert(weather_parse_wttr_forecast(j1, strlen(j1), &report));
    assert(report.temperature_c == 9);
    assert(report.days[0].valid && report.days[0].weekday == 1);  // Monday
    assert(report.days[0].min_c == 8 && report.days[0].max_c == 15);
    // The highest chance over the day; the noon step's picture.
    assert(report.days[0].precipitation_pct == 30);
    assert(report.days[0].icon == WEATHER_ICON_RAIN);
    assert(report.days[1].valid && report.days[1].weekday == 2);
    assert(report.days[1].min_c == -4 && report.days[1].max_c == 1);
    // Snow's chance counts as much as rain's.
    assert(report.days[1].precipitation_pct == 70);
    assert(report.days[1].icon == WEATHER_ICON_SNOW);
    // A day with no hours still has its figures.
    assert(report.days[2].valid && report.days[2].weekday == 3);
    assert(report.days[2].precipitation_pct == WEATHER_UNKNOWN);
    assert(report.days[2].icon == WEATHER_ICON_NONE);

    static const char refused[] = "Unknown location; please try ~55.75,37.62";
    assert(!weather_parse_wttr_forecast(refused, strlen(refused), &report));
    assert(!report.days[0].valid);

    assert(weather_icon_from_wwo(113) == WEATHER_ICON_CLEAR_DAY);
    assert(weather_icon_from_wwo(122) == WEATHER_ICON_CLOUDY);
    assert(weather_icon_from_wwo(248) == WEATHER_ICON_FOG);
    assert(weather_icon_from_wwo(311) == WEATHER_ICON_SLEET);
    assert(weather_icon_from_wwo(389) == WEATHER_ICON_THUNDERSTORM);
    assert(weather_icon_from_wwo(1) == WEATHER_ICON_NONE);
}

int main(void)
{
    test_the_full_open_meteo_answer();
    test_the_full_wttr_line();
    test_openweathermap_details_and_forecast();
    test_the_wttr_forecast();
    test_open_meteo_answer_becomes_a_report();
    test_wttr_line_becomes_a_report();
    test_openweathermap_answer_becomes_a_report();
    test_the_three_tables_agree_on_the_sky();
    test_the_request_is_plain_http_and_carries_the_coordinates();
    test_the_reading_is_whole_degrees_with_a_sign();
    test_the_key_and_the_icon_names();
    printf("weather_report tests passed\n");
    return 0;
}
