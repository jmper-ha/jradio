#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_source.h"

typedef enum {
    UI_FEED_INTERNET_RADIO = 0,
    UI_FEED_USB,
    UI_FEED_BLUETOOTH,
    UI_FEED_FM,
    UI_FEED_DLNA,
    UI_FEED_YANDEX,
    UI_FEED_SETTINGS,
    UI_FEED_ITEM_COUNT,
} ui_feed_item_t;

typedef struct {
    uint8_t selected;
} ui_feed_model_t;

void ui_feed_model_init(ui_feed_model_t *model, uint8_t initial_index);
void ui_feed_model_move(ui_feed_model_t *model, int direction);
ui_feed_item_t ui_feed_model_selected(const ui_feed_model_t *model);
bool ui_feed_model_is_supported(ui_feed_item_t item);
bool ui_feed_model_activate(ui_feed_item_t item, audio_source_t *source_out);
