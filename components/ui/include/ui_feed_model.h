#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_source.h"

typedef enum {
    UI_FEED_INTERNET_RADIO = 0,
    UI_FEED_USB,
    UI_FEED_SD_CARD,
    UI_FEED_BLUETOOTH,
    UI_FEED_FM,
    UI_FEED_DLNA,
    UI_FEED_YANDEX,
    UI_FEED_SETTINGS,
    UI_FEED_ITEM_COUNT,
} ui_feed_item_t;

typedef struct {
    uint8_t selected;
    bool yandex_visible;
} ui_feed_model_t;

void ui_feed_model_init(ui_feed_model_t *model, uint8_t initial_index);

/* The carousel carries the same items as the list home screen, so it hides the
 * same ones. Which item that is, and the arithmetic of stepping past it, live
 * in ui_menu.c - this only holds the flag and asks. */
void ui_feed_model_set_yandex_visible(ui_feed_model_t *model, bool visible);
bool ui_feed_model_yandex_visible(const ui_feed_model_t *model);
void ui_feed_model_move(ui_feed_model_t *model, int direction);
ui_feed_item_t ui_feed_model_selected(const ui_feed_model_t *model);
bool ui_feed_model_activate(ui_feed_item_t item, audio_source_t *source_out);

/* Puts the cursor on the item that starts `source` - the inverse of
 * ui_feed_model_activate(). False, and the cursor left alone, when nothing on
 * the feed starts it.
 *
 * This is what makes leaving the player land on the icon it was started from.
 * The cursor cannot simply be remembered from the press that started it: a
 * source also starts from autoplay at boot and from the web UI, and neither
 * goes anywhere near this screen. */
bool ui_feed_model_select_source(ui_feed_model_t *model, audio_source_t source);
