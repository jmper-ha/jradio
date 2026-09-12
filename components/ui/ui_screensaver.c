#include "ui_screensaver.h"

#include <stdio.h>

void ui_screensaver_init(ui_screensaver_t *saver, uint32_t now_ms)
{
    if (saver == NULL) return;
    *saver = (ui_screensaver_t){.last_input_ms = now_ms, .seed = now_ms ^ 0x5EEDU};
}

/* The two modes that leave nothing readable on the panel. */
static bool mode_hides_screen(device_screensaver_t mode)
{
    return mode == DEVICE_SCREENSAVER_BLANK || mode == DEVICE_SCREENSAVER_CLOCK;
}

bool ui_screensaver_wake(ui_screensaver_t *saver, device_screensaver_t mode, uint32_t now_ms)
{
    if (saver == NULL) return false;
    bool swallow = saver->active && mode_hides_screen(mode);
    if (swallow) {
        saver->swallowing = true;
        saver->woke_ms = now_ms;
    } else if (saver->swallowing) {
        swallow = (uint32_t)(now_ms - saver->woke_ms) < UI_SCREENSAVER_WAKE_GRACE_MS;
        saver->swallowing = swallow;
    }
    saver->active = false;
    saver->last_input_ms = now_ms;
    return swallow;
}

bool ui_screensaver_step(ui_screensaver_t *saver, const device_settings_t *settings,
                         uint32_t now_ms, int area_w, int area_h, int block_w, int block_h,
                         ui_screensaver_view_t *view)
{
    if (saver == NULL || settings == NULL || view == NULL) return false;
    const device_screensaver_t mode = settings->screensaver;
    const uint32_t wait_ms = (uint32_t)settings->screensaver_seconds * 1000U;

    if (mode == DEVICE_SCREENSAVER_OFF || wait_ms == 0U) {
        /* Switched off from the page while resting: the panel comes straight
         * back, and the next touch is an ordinary one. */
        saver->active = false;
    } else if (!saver->active && (uint32_t)(now_ms - saver->last_input_ms) >= wait_ms) {
        saver->active = true;
        /* From the middle, heading down and right, every time - the place
         * the eye goes first, and the same place in every test. */
        /* A linear congruential step - the textbook constants - and the
         * remainder over the room there is. Plenty for "not where it was
         * last time", which is all this has to be. */
        saver->seed = saver->seed * 1103515245U + 12345U;
        const int room_x = area_w > block_w ? area_w - block_w : 0;
        const int room_y = area_h > block_h ? area_h - block_h : 0;
        saver->x = room_x > 0 ? (int)((saver->seed >> 8) % (uint32_t)(room_x + 1)) : 0;
        saver->y = room_y > 0 ? (int)((saver->seed >> 20) % (uint32_t)(room_y + 1)) : 0;
    }

    ui_screensaver_view_t next = {
        .active = saver->active,
        .backlight = settings->brightness,
    };
    if (saver->active) {
        next.cover = mode_hides_screen(mode);
        next.clock = mode == DEVICE_SCREENSAVER_CLOCK;
        next.backlight = mode == DEVICE_SCREENSAVER_BLANK ? 0U : settings->screensaver_brightness;
        if (next.clock) {
            next.x = saver->x;
            next.y = saver->y;
        }
    }

    const bool changed = !saver->have_view || next.active != view->active ||
                         next.backlight != view->backlight || next.cover != view->cover ||
                         next.clock != view->clock || next.x != view->x || next.y != view->y;
    *view = next;
    saver->have_view = true;
    return changed;
}

uint32_t ui_screensaver_leg_ms(int from, int to)
{
    const int distance = from > to ? from - to : to - from;
    const uint32_t ms = (uint32_t)distance * 1000U / UI_SCREENSAVER_SPEED_PX_PER_S;
    return ms < 40U ? 40U : ms;
}

void ui_screensaver_time_text(char *out, size_t out_size, bool have_time, int hour, int minute)
{
    if (out == NULL || out_size == 0U) return;
    if (!have_time || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        snprintf(out, out_size, "--:--");
        return;
    }
    snprintf(out, out_size, "%02d:%02d", hour, minute);
}

/* Genitive in Russian, because the day number comes first: "11 сентября",
 * never "11 сентябрь". */
static const char *const k_months[2][12] = {
    [DEVICE_LANGUAGE_RU] = {"января", "февраля", "марта", "апреля", "мая", "июня", "июля",
                            "августа", "сентября", "октября", "ноября", "декабря"},
    [DEVICE_LANGUAGE_EN] = {"January", "February", "March", "April", "May", "June", "July",
                            "August", "September", "October", "November", "December"},
};

static const char *const k_weekdays[2][7] = {
    [DEVICE_LANGUAGE_RU] = {"воскресенье", "понедельник", "вторник", "среда", "четверг",
                            "пятница", "суббота"},
    [DEVICE_LANGUAGE_EN] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                            "Saturday"},
};

void ui_screensaver_date_text(char *out, size_t out_size, device_language_t language,
                              bool have_date, int day, int month, int weekday)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (!have_date || day < 1 || day > 31 || month < 1 || month > 12 || weekday < 0 ||
        weekday > 6) {
        return;
    }
    const int lang = language == DEVICE_LANGUAGE_EN ? DEVICE_LANGUAGE_EN : DEVICE_LANGUAGE_RU;
    if (lang == DEVICE_LANGUAGE_EN) {
        snprintf(out, out_size, "%s, %d %s", k_weekdays[lang][weekday], day,
                 k_months[lang][month - 1]);
    } else {
        snprintf(out, out_size, "%d %s, %s", day, k_months[lang][month - 1],
                 k_weekdays[lang][weekday]);
    }
}

static bool has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

void ui_screensaver_track_text(char *out, size_t out_size, const char *heading,
                               const char *artist, const char *title)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (has_text(title)) {
        if (has_text(artist)) {
            snprintf(out, out_size, "%s - %s", artist, title);
        } else {
            snprintf(out, out_size, "%s", title);
        }
        return;
    }
    if (has_text(heading)) snprintf(out, out_size, "%s", heading);
}
