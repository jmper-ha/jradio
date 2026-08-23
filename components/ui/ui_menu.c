#include "ui_menu.h"

#include <stddef.h>

typedef struct {
    const char *label;
    audio_source_t source;
} ui_menu_item_config_t;

static const ui_menu_item_config_t s_items[UI_MENU_ITEM_COUNT] = {
    [UI_MENU_ITEM_INTERNET_RADIO] = {.label = "Internet radio", .source = AUDIO_SOURCE_INTERNET_RADIO},
    [UI_MENU_ITEM_USB_FILES] = {.label = "USB files", .source = AUDIO_SOURCE_USB},
    [UI_MENU_ITEM_SD_CARD] = {.label = "SD card", .source = AUDIO_SOURCE_SD},
    [UI_MENU_ITEM_BLUETOOTH] = {.label = "Bluetooth", .source = AUDIO_SOURCE_BLUETOOTH},
    [UI_MENU_ITEM_FM_RADIO] = {.label = "FM radio", .source = AUDIO_SOURCE_FM},
    [UI_MENU_ITEM_DLNA] = {.label = "DLNA", .source = AUDIO_SOURCE_DLNA},
    [UI_MENU_ITEM_YANDEX_MUSIC] = {.label = "Yandex Music", .source = AUDIO_SOURCE_YANDEX},
    [UI_MENU_ITEM_SETTINGS] = {.label = "Настройки", .source = AUDIO_SOURCE_NONE},
};

void ui_menu_init(ui_menu_state_t *state)
{
    if (state != NULL) {
        state->selected_index = UI_MENU_ITEM_INTERNET_RADIO;
    }
}

bool ui_menu_handle_input(ui_menu_state_t *state, board_input_action_t action)
{
    if (state == NULL) {
        return false;
    }

    if (action == BOARD_INPUT_ACTION_ENCODER_LEFT) {
        state->selected_index = state->selected_index == 0 ? UI_MENU_ITEM_COUNT - 1 :
                                                           state->selected_index - 1;
        return true;
    }
    if (action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
        state->selected_index = (state->selected_index + 1) % UI_MENU_ITEM_COUNT;
        return true;
    }
    return false;
}

bool ui_menu_select_source(ui_menu_state_t *state, audio_source_t source)
{
    if (state == NULL || source == AUDIO_SOURCE_NONE) {
        return false;
    }
    for (uint8_t index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        if (s_items[index].source == source) {
            state->selected_index = index;
            return true;
        }
    }
    return false;
}

uint8_t ui_menu_selected_index(const ui_menu_state_t *state)
{
    return state == NULL ? 0 : state->selected_index;
}

const char *ui_menu_item_label(ui_menu_item_t item)
{
    return item < UI_MENU_ITEM_COUNT ? s_items[item].label : "";
}

bool ui_menu_selection_is_settings(const ui_menu_state_t *state)
{
    return state != NULL && state->selected_index == UI_MENU_ITEM_SETTINGS;
}

audio_source_t ui_menu_activate(const ui_menu_state_t *state)
{
    return state == NULL || state->selected_index >= UI_MENU_ITEM_COUNT ? AUDIO_SOURCE_NONE :
                                                                        s_items[state->selected_index].source;
}
