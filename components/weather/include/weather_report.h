#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "device_settings.h"

/* The weather as the panel shows it: one temperature and one picture. Pure -
 * no network, no clock, no ESP-IDF - so the three services' answers can be
 * parsed under host tests against captured fixtures, and so the mapping from
 * what a service says to which icon is drawn is in one place per service.
 *
 * Three services rather than one, because which of them answers from a given
 * network is not the firmware's to know - see device_weather_provider_t. Each
 * has its own answer shape, and each is parsed here into the same report. */

/* What is drawn beside the temperature. Ten pictures for the whole sky: every
 * service reports finer distinctions than these, and every one of them is
 * folded onto this set here, because a 16 px icon cannot tell drizzle from
 * light rain and a status strip has no room to try. Day and night differ only
 * where the sky is what the picture shows - a cloud looks the same at
 * midnight. */
typedef enum {
    WEATHER_ICON_NONE = 0,
    WEATHER_ICON_CLEAR_DAY,
    WEATHER_ICON_CLEAR_NIGHT,
    WEATHER_ICON_PARTLY_CLOUDY_DAY,
    WEATHER_ICON_PARTLY_CLOUDY_NIGHT,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_FOG,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_SLEET,
    WEATHER_ICON_THUNDERSTORM,
    WEATHER_ICON_COUNT,
} weather_icon_t;

typedef struct {
    bool valid;
    /* Whole degrees Celsius, rounded half away from zero. The services
     * answer to a tenth, and a tenth is more than a strip 16 px tall can be
     * read to from across a room. */
    int temperature_c;
    weather_icon_t icon;
} weather_report_t;

/* The three answers. Each takes the body as received - a JSON document from
 * Open-Meteo and OpenWeatherMap, one line from wttr.in - and fills `report`,
 * or returns false and leaves it invalid when the body is not what that
 * service sends. A body that parses but describes no current weather is a
 * false too: the caller keeps showing the previous report rather than nothing.
 *
 * wttr.in says nothing about whether it is day, so its line is asked to carry
 * sunrise, sunset and the local time, and the parser works it out from them.
 * The other two say so themselves. */
bool weather_parse_open_meteo(const char *body, size_t length, weather_report_t *report);
bool weather_parse_wttr(const char *body, size_t length, weather_report_t *report);
bool weather_parse_openweathermap(const char *body, size_t length,
                                  weather_report_t *report);

/* The mappings on their own, for the tests and for anyone reading the
 * tables: WMO codes (Open-Meteo), OpenWeatherMap's condition ids, and the
 * plain-text symbols wttr.in's `%x` answers with. */
weather_icon_t weather_icon_from_wmo(int code, bool is_day);
weather_icon_t weather_icon_from_openweathermap(int condition, bool is_day);
weather_icon_t weather_icon_from_wttr_symbol(const char *symbol, bool is_day);

/* The request for a provider, over plain HTTP - the whole reason these three
 * were chosen over the ones that insist on TLS, which on this board wants AES
 * in the internal SRAM everything else is short of. Returns the length, or 0
 * when the provider is off, the coordinates are missing, a key is needed and
 * absent, or the buffer is too small. `key` matters to OpenWeatherMap only. */
#define WEATHER_URL_MAX 256
size_t weather_request_url(char *url, size_t url_size, device_weather_provider_t provider,
                           const char *latitude, const char *longitude, const char *key);

/* "+12°", "-3°", "0°": the sign is part of the reading - a Russian weather
 * report writes the plus - and zero gets neither. Empty for an invalid
 * report, so the label can be set from this unconditionally. */
void weather_temperature_text(char *text, size_t text_size, const weather_report_t *report);

/* Whole degrees out of a tenth-degree reading, half away from zero: 12.5 is
 * 13 and -12.5 is -13, which is what a thermometer's owner would say. */
int weather_round_temperature(double celsius);

/* The icon's name for the browser - "clear_night" - so the page can draw the
 * same picture the panel does without either side knowing the other's
 * numbers. "none" for no picture. */
const char *weather_icon_name(weather_icon_t icon);

/* What an OpenWeatherMap key may look like: printable ASCII with no quote or
 * backslash, so it can go into a JSON file and a URL unescaped, and between
 * eight and sixty-four characters - theirs are thirty-two hex digits, and the
 * bounds are there to refuse a pasted sentence, not to know their format. */
#define WEATHER_KEY_MAX 64
bool weather_key_valid(const char *key);
