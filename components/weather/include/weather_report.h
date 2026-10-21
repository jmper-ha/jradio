#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* A reading a service did not give: wttr.in has no gusts and no cloud cover,
 * OpenWeatherMap no forecast in the answer the strip asks for. Drawn as
 * nothing rather than as a zero that would read as a measurement. */
#define WEATHER_UNKNOWN INT16_MIN

/* The days the weather screensaver shows: today and the two after. */
#define WEATHER_FORECAST_DAYS 3

typedef struct {
    bool valid;
    weather_icon_t icon;  // the day's weather, always the day picture
    int16_t min_c;
    int16_t max_c;
    // Chance of rain or snow, 0..100, or WEATHER_UNKNOWN.
    int16_t precipitation_pct;
    // 0 Sunday .. 6 Saturday, as struct tm counts; the label is the panel's.
    uint8_t weekday;
} weather_day_t;

typedef struct {
    bool valid;
    /* Whole degrees Celsius, rounded half away from zero. The services
     * answer to a tenth, and a tenth is more than a strip 16 px tall can be
     * read to from across a room. */
    int temperature_c;
    weather_icon_t icon;
    /* The rest of the current weather, for the weather screensaver; each one
     * WEATHER_UNKNOWN where the service did not say. Whole units, as they
     * are shown: a feels-like temperature in degrees, the pressure already
     * in millimetres of mercury, the wind in metres a second. */
    int16_t feels_like_c;
    int16_t humidity_pct;
    int16_t pressure_mmhg;
    int16_t wind_ms;
    int16_t wind_gust_ms;
    // Where the wind comes from, 0..359 degrees (0 north, 90 east).
    int16_t wind_from_deg;
    int16_t cloud_pct;
    // What fell in the last hour or so, in tenths of a millimetre.
    int16_t precipitation_tenths_mm;
    // Local time, minutes after midnight.
    int16_t sunrise_min;
    int16_t sunset_min;
    weather_day_t days[WEATHER_FORECAST_DAYS];
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

/* OpenWeatherMap's forecast is a second request (weather_forecast_url) and a
 * second document - a list of three-hour steps, some 14 KB for three days -
 * read here into `report->days` without touching the rest. Read without
 * cJSON: a tree over it is a thousand small nodes, which this board would put
 * in the internal RAM it is short of. Each day is the steps of that local
 * date: the lowest and highest temperature, the highest chance of rain, and
 * the picture of the step nearest midday. False, with the days left invalid,
 * when nothing usable was in it. */
bool weather_parse_openweathermap_forecast(const char *body, size_t length,
                                           weather_report_t *report);

/* The second request, for the provider that needs one; 0 for the others. */
size_t weather_forecast_url(char *url, size_t url_size, device_weather_provider_t provider,
                            const char *latitude, const char *longitude, const char *key);

/* hPa to whole millimetres of mercury, the way a Russian forecast gives it. */
int weather_hpa_to_mmhg(double hpa);

/* Where the wind comes from as one of eight points, 0 north, 1 north-east
 * ... 7 north-west; -1 for an unknown direction. The names are the panel's. */
int weather_wind_point(int from_deg);

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
#define WEATHER_URL_MAX 512
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
