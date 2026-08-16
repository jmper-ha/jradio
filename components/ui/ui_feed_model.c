#include "ui_feed_model.h"

#include <stddef.h>

void ui_feed_model_init(ui_feed_model_t *model, uint8_t initial_index)
{
    if (model == NULL) return;
    model->selected = initial_index < UI_FEED_ITEM_COUNT ? initial_index : 0U;
}

void ui_feed_model_move(ui_feed_model_t *model, int direction)
{
    if (model == NULL || direction == 0) return;
    if (model->selected >= UI_FEED_ITEM_COUNT) model->selected = 0U;
    if (direction < 0) {
        model->selected = model->selected == 0U ? UI_FEED_ITEM_COUNT - 1U
                                                : model->selected - 1U;
    } else {
        model->selected = (uint8_t)((model->selected + 1U) % UI_FEED_ITEM_COUNT);
    }
}

ui_feed_item_t ui_feed_model_selected(const ui_feed_model_t *model)
{
    if (model == NULL || model->selected >= UI_FEED_ITEM_COUNT) {
        return UI_FEED_INTERNET_RADIO;
    }
    return (ui_feed_item_t)model->selected;
}

bool ui_feed_model_is_supported(ui_feed_item_t item)
{
    return item == UI_FEED_INTERNET_RADIO || item == UI_FEED_USB;
}

bool ui_feed_model_activate(ui_feed_item_t item, audio_source_t *source_out)
{
    if (source_out == NULL) return false;
    switch (item) {
    case UI_FEED_INTERNET_RADIO:
        *source_out = AUDIO_SOURCE_INTERNET_RADIO;
        return true;
    case UI_FEED_USB:
        *source_out = AUDIO_SOURCE_USB;
        return true;
    default:
        *source_out = AUDIO_SOURCE_NONE;
        return false;
    }
}
