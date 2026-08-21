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

bool ui_feed_model_select_source(ui_feed_model_t *model, audio_source_t source)
{
    if (model == NULL || source == AUDIO_SOURCE_NONE) return false;
    // Asks the mapping above rather than carrying a second copy of it, so the
    // two directions cannot drift apart as items are added.
    for (uint8_t index = 0U; index < UI_FEED_ITEM_COUNT; ++index) {
        audio_source_t item_source = AUDIO_SOURCE_NONE;
        if (ui_feed_model_activate((ui_feed_item_t)index, &item_source) &&
            item_source == source) {
            model->selected = index;
            return true;
        }
    }
    return false;
}
