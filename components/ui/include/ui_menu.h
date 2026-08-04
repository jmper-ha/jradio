#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_source.h"
#include "board_input.h"

typedef enum {
    UI_MENU_ITEM_INTERNET_RADIO = 0,
    UI_MENU_ITEM_USB_FILES,
    UI_MENU_ITEM_BLUETOOTH,
    UI_MENU_ITEM_FM_RADIO,
    UI_MENU_ITEM_DLNA,
    UI_MENU_ITEM_YANDEX_MUSIC,
    UI_MENU_ITEM_COUNT,
} ui_menu_item_t;

typedef struct {
    uint8_t selected_index;
} ui_menu_state_t;

void ui_menu_init(ui_menu_state_t *state);
bool ui_menu_handle_input(ui_menu_state_t *state, board_input_action_t action);
uint8_t ui_menu_selected_index(const ui_menu_state_t *state);
const char *ui_menu_item_label(ui_menu_item_t item);
audio_source_t ui_menu_activate(const ui_menu_state_t *state);
