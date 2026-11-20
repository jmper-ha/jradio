#include "ui_peek.h"

#include <stddef.h>

void ui_peek_init(ui_peek_t *peek)
{
    if (peek == NULL) return;
    *peek = (ui_peek_t){.face = UI_PEEK_NONE};
}

static ui_peek_face_t saver_face(const ui_screensaver_view_t *saver)
{
    if (saver == NULL || !saver->active) return UI_PEEK_NONE;
    if (saver->clock) return UI_PEEK_CLOCK;
    if (saver->weather) return UI_PEEK_WEATHER;
    return UI_PEEK_NONE;
}

void ui_peek_press(ui_peek_t *peek, const ui_screensaver_view_t *saver, bool have_weather,
                   uint32_t now_ms)
{
    if (peek == NULL) return;
    (void)ui_peek_face(peek, now_ms);
    const ui_peek_face_t own = saver_face(saver);
    ui_peek_face_t faces[2];
    size_t count = 0U;
    if (own != UI_PEEK_CLOCK) faces[count++] = UI_PEEK_CLOCK;
    if (own != UI_PEEK_WEATHER && have_weather) faces[count++] = UI_PEEK_WEATHER;

    /* The one after the face up, or the first; past the last, back. */
    size_t next = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (faces[i] == peek->face) next = i + 1U;
    }
    peek->face = next < count ? faces[next] : UI_PEEK_NONE;
    peek->until_ms = now_ms + UI_PEEK_MS;
}

ui_peek_face_t ui_peek_face(ui_peek_t *peek, uint32_t now_ms)
{
    if (peek == NULL) return UI_PEEK_NONE;
    if (peek->face != UI_PEEK_NONE && (int32_t)(now_ms - peek->until_ms) >= 0) {
        peek->face = UI_PEEK_NONE;
    }
    return peek->face;
}

bool ui_peek_cancel(ui_peek_t *peek)
{
    if (peek == NULL || peek->face == UI_PEEK_NONE) return false;
    peek->face = UI_PEEK_NONE;
    return true;
}

void ui_peek_apply(ui_peek_face_t face, const device_settings_t *settings, int area_w,
                   int area_h, int block_w, int block_h, ui_screensaver_view_t *view)
{
    if (face == UI_PEEK_NONE || settings == NULL || view == NULL) return;
    view->cover = true;
    view->clock = face == UI_PEEK_CLOCK;
    view->weather = face == UI_PEEK_WEATHER;
    /* At night, over a dark or a dimmed panel, the saver's level: the full
     * one in a dark bedroom is a flash in the eyes. */
    view->backlight = view->active ? settings->screensaver_brightness : settings->brightness;
    view->x = area_w > block_w ? (area_w - block_w) / 2 : 0;
    view->y = area_h > block_h ? (area_h - block_h) / 2 : 0;
}
