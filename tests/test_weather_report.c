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
    assert(strcmp(url, "http://api.open-meteo.com/v1/forecast?latitude=55.75&longitude=37.62"
                       "&current=temperature_2m,weather_code,is_day") == 0);

    assert(weather_request_url(url, sizeof(url), DEVICE_WEATHER_WTTR, "-33.8688", "151.2093",
                               NULL) > 0U);
    /* The format is asked for encoded: a bare `%` or `|` is not a request line. */
    assert(strcmp(url, "http://wttr.in/-33.8688,151.2093?format=%25t%7C%25x%7C%25S%7C%25s%7C%25T")
           == 0);
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

int main(void)
{
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
