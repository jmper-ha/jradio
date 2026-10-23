#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_language.h"
#include "weather_report.h"

/* The weather screensaver's words and numbers: what each line of it says,
 * worked out from a weather_report_t without LVGL, so the wording, the units
 * and the cases where a service said nothing are tested on the host. ui.c
 * only places the labels and the pictures.
 *
 * Every function writes an empty string for a reading that is unknown, so a
 * label can be set from it unconditionally and a service that did not give
 * something shows nothing there - never a zero that would read as weather. */

/* "+6°", "–3°", "0°": the temperature, in the big face and the text faces.
 * The minus is an en dash (U+2013), as over the volume figure: a hyphen is
 * too short to read as a sign, and the true minus (U+2212) is not in the text
 * faces. */
void ui_wx_temperature_text(char *out, size_t size, int celsius);

/* "ощущается +3°" / "feels like +3°", or "ощущ. +3°" / "feels +3°" where
 * `brief` - a small panel, whose top has the clock, the picture and this
 * line in 300 px. */
void ui_wx_feels_text(char *out, size_t size, device_language_t language,
                      const weather_report_t *report, bool brief);

// "62%".
void ui_wx_humidity_text(char *out, size_t size, const weather_report_t *report);

// "763 мм" / "763 mmHg".
void ui_wx_pressure_text(char *out, size_t size, device_language_t language,
                         const weather_report_t *report);

/* "1 м/с В" / "1 m/s E": the speed and where the wind comes from, as a
 * forecast says it - or the speed alone, where `point` is false and the
 * arrow beside it has to say the direction. A calm says "штиль" / "calm". */
void ui_wx_wind_text(char *out, size_t size, device_language_t language,
                     const weather_report_t *report, bool point);

/* The line under the wind's figure: "ветер, порывы 5" when the gusts are
 * known and stronger than the wind, else just "ветер" / "wind". */
void ui_wx_wind_label(char *out, size_t size, device_language_t language,
                      const weather_report_t *report);

// "07:11", from minutes after midnight.
void ui_wx_clock_text(char *out, size_t size, int minutes);

/* The sky in words, under the readings: the cloud cover and what is falling -
 * "облачно 27% · осадков нет", "облачно 90% · осадки 0,4 мм". Either half
 * goes when it is unknown; empty when both are. */
void ui_wx_sky_text(char *out, size_t size, device_language_t language,
                    const weather_report_t *report);

/* A forecast day's name: "Сегодня" / "Today" for the first, then the
 * weekday - "Чт" / "Thu". */
void ui_wx_day_name(char *out, size_t size, device_language_t language, int index,
                    const weather_day_t *day);

/* "+2…+12°": the day's low and high - "+2…+12" without `degree`, where a
 * third of a small panel beside the day's picture has no room for it. */
void ui_wx_day_range_text(char *out, size_t size, const weather_day_t *day, bool degree);

// "15%": the chance of rain or snow; empty when unknown.
void ui_wx_day_chance_text(char *out, size_t size, const weather_day_t *day);

/* The panel is not allowed to hold one picture for hours: once a minute the
 * whole screen moves by a few pixels, round a small square - `step` counts
 * the minutes, and the offset comes back to where it began after 4 x `range`
 * of them. `range` is how far it may move, in pixels, on each axis. */
void ui_wx_shift(uint32_t step, int range, int *dx, int *dy);
