#include "ui_menu.h"

#include <stddef.h>

#include "board_features.h"

typedef struct {
    device_text_id_t label;
    audio_source_t source;
} ui_menu_item_config_t;

/* The label is an id rather than a string: this list is the home screen, and
 * the home screen was the half of the interface that stayed in English while
 * the settings beside it were translated. */
static const ui_menu_item_config_t s_items[UI_MENU_ITEM_COUNT] = {
    [UI_MENU_ITEM_INTERNET_RADIO] = {.label = DEVICE_TEXT_SOURCE_INTERNET_RADIO,
                                     .source = AUDIO_SOURCE_INTERNET_RADIO},
    [UI_MENU_ITEM_USB_FILES] = {.label = DEVICE_TEXT_SOURCE_USB, .source = AUDIO_SOURCE_USB},
    [UI_MENU_ITEM_SD_CARD] = {.label = DEVICE_TEXT_SOURCE_SD, .source = AUDIO_SOURCE_SD},
    [UI_MENU_ITEM_BLUETOOTH] = {.label = DEVICE_TEXT_SOURCE_BLUETOOTH,
                                .source = AUDIO_SOURCE_BLUETOOTH},
    [UI_MENU_ITEM_FM_RADIO] = {.label = DEVICE_TEXT_SOURCE_FM, .source = AUDIO_SOURCE_FM},
    [UI_MENU_ITEM_DLNA] = {.label = DEVICE_TEXT_SOURCE_DLNA, .source = AUDIO_SOURCE_DLNA},
    /* The long name here, where the row has the width for it: the player block
     * shows the short one. */
    [UI_MENU_ITEM_YANDEX_MUSIC] = {.label = DEVICE_TEXT_YANDEX_LONG,
                                   .source = AUDIO_SOURCE_YANDEX},
    [UI_MENU_ITEM_SETTINGS] = {.label = DEVICE_TEXT_SETTINGS, .source = AUDIO_SOURCE_NONE},
};

/* What this firmware was built with. Internet radio and Settings answer yes
 * unconditionally: the radio needs no part beyond the Wi-Fi that is on the
 * chip, and a device with no way into Settings could not be configured. */
static bool ui_menu_item_is_built(ui_menu_item_t item)
{
    switch (item) {
    case UI_MENU_ITEM_USB_FILES:
        return BOARD_HAS_USB;
    case UI_MENU_ITEM_SD_CARD:
        return BOARD_HAS_SD_CARD;
    case UI_MENU_ITEM_BLUETOOTH:
        return BOARD_HAS_BLUETOOTH;
    case UI_MENU_ITEM_FM_RADIO:
        return BOARD_HAS_FM_RADIO;
    case UI_MENU_ITEM_DLNA:
        return BOARD_HAS_DLNA;
    case UI_MENU_ITEM_YANDEX_MUSIC:
        return BOARD_HAS_YANDEX_MUSIC;
    default:
        return true;
    }
}

bool ui_menu_item_is_visible(ui_menu_item_t item, ui_menu_visible_mask_t visible)
{
    if (item >= UI_MENU_ITEM_COUNT) return false;
    /* The build decides first: with the feature out, the flag saved in
     * settings.csv is stale data about a source this firmware does not have. */
    if (!ui_menu_item_is_built(item)) return false;
    /* Only these two can be taken away from Settings. Everything else is the
     * build's answer alone, so its bit is not consulted at all. */
    if (item == UI_MENU_ITEM_YANDEX_MUSIC || item == UI_MENU_ITEM_DLNA) {
        return (visible & UI_MENU_VISIBLE(item)) != 0U;
    }
    return true;
}

bool ui_menu_item_is_enabled(ui_menu_item_t item, ui_menu_visible_mask_t visible,
                             bool wifi_connected)
{
    if (!ui_menu_item_is_visible(item, visible)) return false;
    /* These three are reached over the network and there is nothing they can do
     * without it. The media server is on the LAN rather than on the internet,
     * but with no join there is nothing to search for and nothing to stream.
     * Everything else - the drive, the card, Settings - works with the network
     * down. */
    if (item == UI_MENU_ITEM_INTERNET_RADIO || item == UI_MENU_ITEM_YANDEX_MUSIC ||
        item == UI_MENU_ITEM_DLNA) {
        return wifi_connected;
    }
    return true;
}

bool ui_menu_home_screen_needed(uint8_t visible_count)
{
    return visible_count > 2U;
}

uint8_t ui_menu_visible_count(ui_menu_visible_mask_t visible)
{
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < UI_MENU_ITEM_COUNT; ++index) {
        if (ui_menu_item_is_visible((ui_menu_item_t)index, visible)) count++;
    }
    return count;
}

uint8_t ui_menu_visible_position(ui_menu_item_t item, ui_menu_visible_mask_t visible)
{
    if (!ui_menu_item_is_visible(item, visible)) return 0U;
    uint8_t position = 0U;
    for (uint8_t index = 0U; index < (uint8_t)item; ++index) {
        if (ui_menu_item_is_visible((ui_menu_item_t)index, visible)) position++;
    }
    return position;
}

ui_menu_item_t ui_menu_visible_item_at(uint8_t position, ui_menu_visible_mask_t visible)
{
    uint8_t seen = 0U;
    for (uint8_t index = 0U; index < UI_MENU_ITEM_COUNT; ++index) {
        if (!ui_menu_item_is_visible((ui_menu_item_t)index, visible)) continue;
        if (seen == position) return (ui_menu_item_t)index;
        seen++;
    }
    return UI_MENU_ITEM_INTERNET_RADIO;
}

ui_menu_item_t ui_menu_item_step(ui_menu_item_t item, int direction,
                                 ui_menu_visible_mask_t visible)
{
    if (direction == 0 || item >= UI_MENU_ITEM_COUNT) return item;
    uint8_t index = (uint8_t)item;
    /* Bounded rather than "until a visible one turns up": one always does
     * today, and a loop that trusts that is a hang on the day it does not. */
    for (uint8_t guard = 0U; guard < UI_MENU_ITEM_COUNT; ++guard) {
        index = direction < 0 ? (index == 0U ? UI_MENU_ITEM_COUNT - 1U : (uint8_t)(index - 1U))
                              : (uint8_t)((index + 1U) % UI_MENU_ITEM_COUNT);
        if (ui_menu_item_is_visible((ui_menu_item_t)index, visible)) break;
    }
    return (ui_menu_item_t)index;
}

void ui_menu_init(ui_menu_state_t *state)
{
    if (state != NULL) {
        state->selected_index = UI_MENU_ITEM_INTERNET_RADIO;
        /* Shown until told otherwise: the settings are read from flash after
         * the screens are built, and a row that appears is less alarming than
         * one that vanishes a moment after boot. Bits for items the build does
         * not have cost nothing - ui_menu_item_is_visible() asks the build
         * first. */
        state->visible = UI_MENU_VISIBLE_ALL;
    }
}

void ui_menu_set_source_visible(ui_menu_state_t *state, ui_menu_item_t item, bool visible)
{
    if (state == NULL || item >= UI_MENU_ITEM_COUNT) return;
    if (visible) {
        state->visible |= UI_MENU_VISIBLE(item);
    } else {
        state->visible &= ~UI_MENU_VISIBLE(item);
    }
    if (!ui_menu_item_is_visible((ui_menu_item_t)state->selected_index, state->visible)) {
        state->selected_index = UI_MENU_ITEM_INTERNET_RADIO;
    }
}

ui_menu_visible_mask_t ui_menu_visible_mask(const ui_menu_state_t *state)
{
    return state == NULL ? UI_MENU_VISIBLE_ALL : state->visible;
}

bool ui_menu_handle_input(ui_menu_state_t *state, board_input_action_t action)
{
    if (state == NULL) {
        return false;
    }

    if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
        action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
        state->selected_index = (uint8_t)ui_menu_item_step(
            (ui_menu_item_t)state->selected_index,
            action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1, state->visible);
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
        /* A hidden source is still startable - from the web, or from autoplay
         * - but the cursor must not park on a row nobody can see. */
        if (!ui_menu_item_is_visible((ui_menu_item_t)index, state->visible)) continue;
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

const char *ui_menu_item_label(ui_menu_item_t item, device_language_t language)
{
    return item < UI_MENU_ITEM_COUNT ? device_text(s_items[item].label, language) : "";
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
