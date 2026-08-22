#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_source.h"
#include "board_input.h"

typedef enum {
    UI_MENU_ITEM_INTERNET_RADIO = 0,
    UI_MENU_ITEM_USB_FILES,
    UI_MENU_ITEM_SD_CARD,
    UI_MENU_ITEM_BLUETOOTH,
    UI_MENU_ITEM_FM_RADIO,
    UI_MENU_ITEM_DLNA,
    UI_MENU_ITEM_YANDEX_MUSIC,
    /* Not a source. Kept last so the sources stay a contiguous run at the top
     * of the main screen. */
    UI_MENU_ITEM_SETTINGS,
    UI_MENU_ITEM_COUNT,
} ui_menu_item_t;

typedef struct {
    uint8_t selected_index;
} ui_menu_state_t;

void ui_menu_init(ui_menu_state_t *state);
bool ui_menu_handle_input(ui_menu_state_t *state, board_input_action_t action);
bool ui_menu_select_source(ui_menu_state_t *state, audio_source_t source);
uint8_t ui_menu_selected_index(const ui_menu_state_t *state);
const char *ui_menu_item_label(ui_menu_item_t item);
audio_source_t ui_menu_activate(const ui_menu_state_t *state);

/* True when the highlighted row opens the settings screen instead of starting
 * a source. Needed because ui_menu_activate() reports AUDIO_SOURCE_NONE both
 * for settings and for the sources that have no implementation yet. */
bool ui_menu_selection_is_settings(const ui_menu_state_t *state);
