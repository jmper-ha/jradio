#include "ui_feed_model.h"

#include <stddef.h>

#include "board_features.h"
#include "ui_menu.h"

/* The feed draws the home screen's items as a carousel, and ui.c already casts
 * one enum to the other to borrow the labels. Saying so out loud means a new
 * item added to one and not the other fails to build, rather than showing the
 * wrong icon under the right name. */
_Static_assert((int)UI_FEED_ITEM_COUNT == (int)UI_MENU_ITEM_COUNT,
               "the feed and the menu list the same items");
_Static_assert((int)UI_FEED_YANDEX == (int)UI_MENU_ITEM_YANDEX_MUSIC,
               "the feed and the menu list the same items in the same order");
_Static_assert((int)UI_FEED_SETTINGS == (int)UI_MENU_ITEM_SETTINGS,
               "the feed and the menu list the same items in the same order");

void ui_feed_model_init(ui_feed_model_t *model, uint8_t initial_index)
{
    if (model == NULL) return;
    model->selected = initial_index < UI_FEED_ITEM_COUNT ? initial_index : 0U;
    model->yandex_visible = BOARD_HAS_YANDEX_MUSIC;
}

void ui_feed_model_set_yandex_visible(ui_feed_model_t *model, bool visible)
{
    if (model == NULL) return;
    model->yandex_visible = visible && BOARD_HAS_YANDEX_MUSIC;
    if (!ui_menu_item_is_visible((ui_menu_item_t)model->selected, model->yandex_visible)) {
        model->selected = UI_FEED_INTERNET_RADIO;
    }
}

bool ui_feed_model_yandex_visible(const ui_feed_model_t *model)
{
    return model != NULL && model->yandex_visible;
}

void ui_feed_model_move(ui_feed_model_t *model, int direction)
{
    if (model == NULL || direction == 0) return;
    if (model->selected >= UI_FEED_ITEM_COUNT) model->selected = 0U;
    model->selected = (uint8_t)ui_menu_item_step((ui_menu_item_t)model->selected, direction,
                                                 model->yandex_visible);
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
    case UI_FEED_SD_CARD:
        *source_out = AUDIO_SOURCE_SD;
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
        if (!ui_menu_item_is_visible((ui_menu_item_t)index, model->yandex_visible)) continue;
        audio_source_t item_source = AUDIO_SOURCE_NONE;
        if (ui_feed_model_activate((ui_feed_item_t)index, &item_source) &&
            item_source == source) {
            model->selected = index;
            return true;
        }
    }
    return false;
}
