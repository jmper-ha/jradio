#include "ui_weather_saver.h"

#include <stdio.h>

static bool known(int value)
{
    return value != WEATHER_UNKNOWN;
}

static void empty(char *out, size_t size)
{
    if (out != NULL && size > 0U) out[0] = '\0';
}

void ui_wx_temperature_text(char *out, size_t size, int celsius)
{
    if (out == NULL || size == 0U) return;
    if (celsius == WEATHER_UNKNOWN) {
        empty(out, size);
    } else if (celsius > 0) {
        snprintf(out, size, "+%d\xC2\xB0", celsius);
    } else if (celsius < 0) {
        snprintf(out, size, "\xE2\x80\x93%d\xC2\xB0", -celsius);
    } else {
        snprintf(out, size, "0\xC2\xB0");
    }
}

void ui_wx_feels_text(char *out, size_t size, device_language_t language,
                      const weather_report_t *report)
{
    if (out == NULL || size == 0U) return;
    if (report == NULL || !known(report->feels_like_c)) {
        empty(out, size);
        return;
    }
    char degrees[16];
    ui_wx_temperature_text(degrees, sizeof(degrees), report->feels_like_c);
    snprintf(out, size, "%s %s",
             language == DEVICE_LANGUAGE_EN ? "feels like" : "ощущается", degrees);
}

void ui_wx_humidity_text(char *out, size_t size, const weather_report_t *report)
{
    if (out == NULL || size == 0U) return;
    if (report == NULL || !known(report->humidity_pct)) {
        empty(out, size);
        return;
    }
    snprintf(out, size, "%d%%", report->humidity_pct);
}

void ui_wx_pressure_text(char *out, size_t size, device_language_t language,
                         const weather_report_t *report)
{
    if (out == NULL || size == 0U) return;
    if (report == NULL || !known(report->pressure_mmhg)) {
        empty(out, size);
        return;
    }
    snprintf(out, size, "%d %s", report->pressure_mmhg,
             language == DEVICE_LANGUAGE_EN ? "mmHg" : "мм");
}

void ui_wx_wind_text(char *out, size_t size, device_language_t language,
                     const weather_report_t *report, bool point_shown)
{
    static const char *const ru[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
    static const char *const en[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    if (out == NULL || size == 0U) return;
    if (report == NULL || !known(report->wind_ms)) {
        empty(out, size);
        return;
    }
    const bool english = language == DEVICE_LANGUAGE_EN;
    if (report->wind_ms == 0) {
        snprintf(out, size, "%s", english ? "calm" : "штиль");
        return;
    }
    const int point = point_shown ? weather_wind_point(report->wind_from_deg) : -1;
    if (point < 0) {
        snprintf(out, size, "%d %s", report->wind_ms, english ? "m/s" : "м/с");
    } else {
        snprintf(out, size, "%d %s %s", report->wind_ms, english ? "m/s" : "м/с",
                 english ? en[point] : ru[point]);
    }
}

void ui_wx_wind_label(char *out, size_t size, device_language_t language,
                      const weather_report_t *report)
{
    if (out == NULL || size == 0U) return;
    const bool english = language == DEVICE_LANGUAGE_EN;
    if (report != NULL && known(report->wind_gust_ms) && known(report->wind_ms) &&
        report->wind_gust_ms > report->wind_ms) {
        snprintf(out, size, "%s %d", english ? "wind, gusts" : "ветер, порывы",
                 report->wind_gust_ms);
    } else {
        snprintf(out, size, "%s", english ? "wind" : "ветер");
    }
}

void ui_wx_clock_text(char *out, size_t size, int minutes)
{
    if (out == NULL || size == 0U) return;
    if (minutes == WEATHER_UNKNOWN || minutes < 0 || minutes >= 24 * 60) {
        empty(out, size);
        return;
    }
    snprintf(out, size, "%02d:%02d", minutes / 60, minutes % 60);
}

void ui_wx_sky_text(char *out, size_t size, device_language_t language,
                    const weather_report_t *report)
{
    if (out == NULL || size == 0U) return;
    empty(out, size);
    if (report == NULL) return;
    const bool english = language == DEVICE_LANGUAGE_EN;
    char clouds[32] = "";
    char fallen[40] = "";
    if (known(report->cloud_pct)) {
        snprintf(clouds, sizeof(clouds), "%s %d%%", english ? "clouds" : "облачно",
                 report->cloud_pct);
    }
    if (known(report->precipitation_tenths_mm)) {
        if (report->precipitation_tenths_mm <= 0) {
            snprintf(fallen, sizeof(fallen), "%s", english ? "no rain" : "осадков нет");
        } else {
            const int tenths = report->precipitation_tenths_mm;
            snprintf(fallen, sizeof(fallen), "%s %d%s%d %s", english ? "rain" : "осадки",
                     tenths / 10, english ? "." : ",", tenths % 10, english ? "mm" : "мм");
        }
    }
    if (clouds[0] != '\0' && fallen[0] != '\0') {
        snprintf(out, size, "%s \xC2\xB7 %s", clouds, fallen);
    } else {
        snprintf(out, size, "%s%s", clouds, fallen);
    }
}

void ui_wx_day_name(char *out, size_t size, device_language_t language, int index,
                    const weather_day_t *day)
{
    static const char *const ru[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
    static const char *const en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (out == NULL || size == 0U) return;
    const bool english = language == DEVICE_LANGUAGE_EN;
    if (index == 0) {
        snprintf(out, size, "%s", english ? "Today" : "Сегодня");
    } else if (day == NULL || !day->valid || day->weekday > 6U) {
        empty(out, size);
    } else {
        snprintf(out, size, "%s", english ? en[day->weekday] : ru[day->weekday]);
    }
}

void ui_wx_day_range_text(char *out, size_t size, const weather_day_t *day, bool degree)
{
    if (out == NULL || size == 0U) return;
    if (day == NULL || !day->valid) {
        empty(out, size);
        return;
    }
    char low[16];
    char high[16];
    ui_wx_temperature_text(low, sizeof(low), day->min_c);
    ui_wx_temperature_text(high, sizeof(high), day->max_c);
    // The degree sign once, after the pair: "+2…+12°".
    size_t low_length = 0U;
    while (low[low_length] != '\0') ++low_length;
    if (low_length >= 2U && (unsigned char)low[low_length - 2U] == 0xC2U &&
        (unsigned char)low[low_length - 1U] == 0xB0U) {
        low[low_length - 2U] = '\0';
    }
    if (!degree) {
        size_t high_length = 0U;
        while (high[high_length] != '\0') ++high_length;
        if (high_length >= 2U && (unsigned char)high[high_length - 2U] == 0xC2U &&
            (unsigned char)high[high_length - 1U] == 0xB0U) {
            high[high_length - 2U] = '\0';
        }
    }
    snprintf(out, size, "%s\xE2\x80\xA6%s", low, high);
}

void ui_wx_day_chance_text(char *out, size_t size, const weather_day_t *day)
{
    if (out == NULL || size == 0U) return;
    if (day == NULL || !day->valid || !known(day->precipitation_pct)) {
        empty(out, size);
        return;
    }
    snprintf(out, size, "%d%%", day->precipitation_pct);
}

void ui_wx_shift(uint32_t step, int range, int *dx, int *dy)
{
    int x = 0;
    int y = 0;
    if (range > 0) {
        const uint32_t lap = (uint32_t)range * 4U;
        const int at = (int)(step % lap);
        if (at < range) {
            x = at;
        } else if (at < range * 2) {
            x = range;
            y = at - range;
        } else if (at < range * 3) {
            x = range - (at - range * 2);
            y = range;
        } else {
            y = range - (at - range * 3);
        }
    }
    if (dx != NULL) *dx = x;
    if (dy != NULL) *dy = y;
}
