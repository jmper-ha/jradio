#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_weather_saver.h"

static weather_report_t moscow(void)
{
    weather_report_t report = {.valid = true, .temperature_c = 6, .icon = WEATHER_ICON_CLEAR_NIGHT};
    report.feels_like_c = 3;
    report.humidity_pct = 62;
    report.pressure_mmhg = 763;
    report.wind_ms = 1;
    report.wind_gust_ms = 2;
    report.wind_from_deg = 100;
    report.cloud_pct = 27;
    report.precipitation_tenths_mm = 0;
    report.sunrise_min = 7 * 60 + 11;
    report.sunset_min = 17 * 60 + 16;
    report.days[0] = (weather_day_t){.valid = true, .icon = WEATHER_ICON_PARTLY_CLOUDY_DAY,
                                     .min_c = 2, .max_c = 12, .precipitation_pct = 3, .weekday = 3};
    report.days[1] = (weather_day_t){.valid = true, .icon = WEATHER_ICON_RAIN, .min_c = -3,
                                     .max_c = 0, .precipitation_pct = WEATHER_UNKNOWN, .weekday = 4};
    return report;
}

// Exactly the macro's text, so a test reads like the screen.
#define EXPECT(call, want)                                     \
    do {                                                       \
        char out[64];                                          \
        call;                                                  \
        if (strcmp(out, want) != 0) printf("got \"%s\"\n", out); \
        assert(strcmp(out, want) == 0);                        \
    } while (0)

static void test_the_readings_as_the_mockup_has_them(void)
{
    const weather_report_t report = moscow();
    EXPECT(ui_wx_temperature_text(out, sizeof(out), 6), "+6\xC2\xB0");
    EXPECT(ui_wx_temperature_text(out, sizeof(out), -3), "\xE2\x80\x93" "3\xC2\xB0");
    EXPECT(ui_wx_temperature_text(out, sizeof(out), 0), "0\xC2\xB0");
    EXPECT(ui_wx_feels_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, false),
           "ощущается +3\xC2\xB0");
    EXPECT(ui_wx_feels_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, true),
           "ощущ. +3\xC2\xB0");
    EXPECT(ui_wx_feels_text(out, sizeof(out), DEVICE_LANGUAGE_EN, &report, false),
           "feels like +3\xC2\xB0");
    EXPECT(ui_wx_humidity_text(out, sizeof(out), &report), "62%");
    EXPECT(ui_wx_pressure_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report), "763 мм");
    EXPECT(ui_wx_pressure_text(out, sizeof(out), DEVICE_LANGUAGE_EN, &report), "763 mmHg");
    EXPECT(ui_wx_wind_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, true), "1 м/с В");
    EXPECT(ui_wx_wind_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, false), "1 м/с");
    EXPECT(ui_wx_wind_text(out, sizeof(out), DEVICE_LANGUAGE_EN, &report, true), "1 m/s E");
    EXPECT(ui_wx_wind_label(out, sizeof(out), DEVICE_LANGUAGE_RU, &report), "ветер, порывы 2");
    EXPECT(ui_wx_clock_text(out, sizeof(out), report.sunrise_min), "07:11");
    EXPECT(ui_wx_sky_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report),
           "облачно 27% \xC2\xB7 осадков нет");
}

static void test_what_a_service_did_not_say_is_left_out(void)
{
    weather_report_t report = moscow();
    report.feels_like_c = WEATHER_UNKNOWN;
    report.wind_gust_ms = WEATHER_UNKNOWN;
    report.wind_from_deg = WEATHER_UNKNOWN;
    report.cloud_pct = WEATHER_UNKNOWN;
    report.precipitation_tenths_mm = 4;
    EXPECT(ui_wx_feels_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, false), "");
    EXPECT(ui_wx_wind_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, true), "1 м/с");
    EXPECT(ui_wx_wind_label(out, sizeof(out), DEVICE_LANGUAGE_RU, &report), "ветер");
    EXPECT(ui_wx_sky_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report), "осадки 0,4 мм");
    EXPECT(ui_wx_sky_text(out, sizeof(out), DEVICE_LANGUAGE_EN, &report), "rain 0.4 mm");
    report.precipitation_tenths_mm = WEATHER_UNKNOWN;
    EXPECT(ui_wx_sky_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report), "");
    report.humidity_pct = WEATHER_UNKNOWN;
    report.pressure_mmhg = WEATHER_UNKNOWN;
    report.sunrise_min = WEATHER_UNKNOWN;
    EXPECT(ui_wx_humidity_text(out, sizeof(out), &report), "");
    EXPECT(ui_wx_pressure_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report), "");
    EXPECT(ui_wx_clock_text(out, sizeof(out), report.sunrise_min), "");
    report.wind_ms = 0;
    EXPECT(ui_wx_wind_text(out, sizeof(out), DEVICE_LANGUAGE_RU, &report, true), "штиль");
    // Gusts no stronger than the wind say nothing worth a line.
    report.wind_ms = 5;
    report.wind_gust_ms = 5;
    EXPECT(ui_wx_wind_label(out, sizeof(out), DEVICE_LANGUAGE_EN, &report), "wind");
    EXPECT(ui_wx_wind_text(out, sizeof(out), DEVICE_LANGUAGE_RU, NULL, true), "");
}

static void test_the_forecast_days(void)
{
    const weather_report_t report = moscow();
    EXPECT(ui_wx_day_name(out, sizeof(out), DEVICE_LANGUAGE_RU, 0, &report.days[0]), "Сегодня");
    EXPECT(ui_wx_day_name(out, sizeof(out), DEVICE_LANGUAGE_RU, 1, &report.days[1]), "Чт");
    EXPECT(ui_wx_day_name(out, sizeof(out), DEVICE_LANGUAGE_EN, 1, &report.days[1]), "Thu");
    EXPECT(ui_wx_day_name(out, sizeof(out), DEVICE_LANGUAGE_RU, 2, &report.days[2]), "");
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[0], true, false),
           "+2\xE2\x80\xA6+12\xC2\xB0");
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[0], false, false), "+2\xE2\x80\xA6+12");
    // One plus, where the panel is narrow; a range across zero keeps both signs.
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[0], false, true), "+2\xE2\x80\xA6" "12");
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[0], true, true),
           "+2\xE2\x80\xA6" "12\xC2\xB0");
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[1], true, true),
           "\xE2\x80\x93" "3\xE2\x80\xA6" "0\xC2\xB0");
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[1], true, false),
           "\xE2\x80\x93" "3\xE2\x80\xA6" "0\xC2\xB0");
    EXPECT(ui_wx_day_range_text(out, sizeof(out), &report.days[2], true, false), "");
    EXPECT(ui_wx_day_chance_text(out, sizeof(out), &report.days[0]), "3%");
    EXPECT(ui_wx_day_chance_text(out, sizeof(out), &report.days[1]), "");
}

static void test_the_shift_walks_round_a_square(void)
{
    int dx = -1;
    int dy = -1;
    ui_wx_shift(0U, 4, &dx, &dy);
    assert(dx == 0 && dy == 0);
    ui_wx_shift(3U, 4, &dx, &dy);
    assert(dx == 3 && dy == 0);
    ui_wx_shift(5U, 4, &dx, &dy);
    assert(dx == 4 && dy == 1);
    ui_wx_shift(10U, 4, &dx, &dy);
    assert(dx == 2 && dy == 4);
    ui_wx_shift(14U, 4, &dx, &dy);
    assert(dx == 0 && dy == 2);
    // A lap is sixteen minutes, and it never leaves the square or jumps.
    int last_x = 0;
    int last_y = 0;
    for (uint32_t step = 0U; step < 100U; ++step) {
        ui_wx_shift(step, 4, &dx, &dy);
        assert(dx >= 0 && dx <= 4 && dy >= 0 && dy <= 4);
        const int move = (dx > last_x ? dx - last_x : last_x - dx) +
                         (dy > last_y ? dy - last_y : last_y - dy);
        assert(step == 0U || move == 1);
        last_x = dx;
        last_y = dy;
    }
    ui_wx_shift(16U, 4, &dx, &dy);
    assert(dx == 0 && dy == 0);
    ui_wx_shift(7U, 0, &dx, &dy);
    assert(dx == 0 && dy == 0);
}

int main(void)
{
    test_the_readings_as_the_mockup_has_them();
    test_what_a_service_did_not_say_is_left_out();
    test_the_forecast_days();
    test_the_shift_walks_round_a_square();
    puts("ui_weather_saver tests passed");
    return 0;
}
