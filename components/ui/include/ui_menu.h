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
    bool yandex_visible;
} ui_menu_state_t;

void ui_menu_init(ui_menu_state_t *state);

/* Yandex Music is the only item that can be missing from the home screen: it
 * is left out of the build when board_options.h does not select it, and hidden
 * by the switch in Settings when it is. Everything else is always there, so
 * "what is visible" is one flag rather than a set.
 *
 * A firmware built without the feature ignores the flag, which is why the
 * setter takes the state instead of the caller storing the answer itself.
 * Setting it moves the cursor off a row that just disappeared. */
void ui_menu_set_yandex_visible(ui_menu_state_t *state, bool visible);
bool ui_menu_yandex_visible(const ui_menu_state_t *state);

/* The two home screens draw a run of rows; the model stores items. These
 * convert between them with the hidden items taken out, so neither screen has
 * to know which item that is. A hidden item has no position and reports 0. */
bool ui_menu_item_is_visible(ui_menu_item_t item, bool yandex_visible);
uint8_t ui_menu_visible_count(bool yandex_visible);
uint8_t ui_menu_visible_position(ui_menu_item_t item, bool yandex_visible);
ui_menu_item_t ui_menu_visible_item_at(uint8_t position, bool yandex_visible);
/* The next visible item in `direction`, wrapping. Shared with the feed screen,
 * which moves over the same items in a carousel. */
ui_menu_item_t ui_menu_item_step(ui_menu_item_t item, int direction, bool yandex_visible);
bool ui_menu_handle_input(ui_menu_state_t *state, board_input_action_t action);
bool ui_menu_select_source(ui_menu_state_t *state, audio_source_t source);
uint8_t ui_menu_selected_index(const ui_menu_state_t *state);
const char *ui_menu_item_label(ui_menu_item_t item);
audio_source_t ui_menu_activate(const ui_menu_state_t *state);

/* True when the highlighted row opens the settings screen instead of starting
 * a source. Needed because ui_menu_activate() reports AUDIO_SOURCE_NONE both
 * for settings and for the sources that have no implementation yet. */
bool ui_menu_selection_is_settings(const ui_menu_state_t *state);
