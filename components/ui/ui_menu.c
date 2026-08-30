#include "ui_menu.h"

#include <stddef.h>

#include "board_features.h"

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

bool ui_menu_item_is_visible(ui_menu_item_t item, bool yandex_visible)
{
    if (item >= UI_MENU_ITEM_COUNT) return false;
    /* The build decides first: with the feature out, the flag saved in
     * settings.csv is stale data about a source this firmware does not have. */
    if (!ui_menu_item_is_built(item)) return false;
    return item != UI_MENU_ITEM_YANDEX_MUSIC || yandex_visible;
}

bool ui_menu_item_is_enabled(ui_menu_item_t item, bool yandex_visible, bool wifi_connected)
{
    if (!ui_menu_item_is_visible(item, yandex_visible)) return false;
    /* Both of these are streams off the internet and there is nothing they can
     * do without one. Everything else - the drive, the card, Settings - works
     * with the network down. */
    if (item == UI_MENU_ITEM_INTERNET_RADIO || item == UI_MENU_ITEM_YANDEX_MUSIC) {
        return wifi_connected;
    }
    return true;
}

bool ui_menu_home_screen_needed(uint8_t visible_count)
{
    return visible_count > 2U;
}

uint8_t ui_menu_visible_count(bool yandex_visible)
{
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < UI_MENU_ITEM_COUNT; ++index) {
        if (ui_menu_item_is_visible((ui_menu_item_t)index, yandex_visible)) count++;
    }
    return count;
}

uint8_t ui_menu_visible_position(ui_menu_item_t item, bool yandex_visible)
{
    if (!ui_menu_item_is_visible(item, yandex_visible)) return 0U;
    uint8_t position = 0U;
    for (uint8_t index = 0U; index < (uint8_t)item; ++index) {
        if (ui_menu_item_is_visible((ui_menu_item_t)index, yandex_visible)) position++;
    }
    return position;
}

ui_menu_item_t ui_menu_visible_item_at(uint8_t position, bool yandex_visible)
{
    uint8_t seen = 0U;
    for (uint8_t index = 0U; index < UI_MENU_ITEM_COUNT; ++index) {
        if (!ui_menu_item_is_visible((ui_menu_item_t)index, yandex_visible)) continue;
        if (seen == position) return (ui_menu_item_t)index;
        seen++;
    }
    return UI_MENU_ITEM_INTERNET_RADIO;
}

ui_menu_item_t ui_menu_item_step(ui_menu_item_t item, int direction, bool yandex_visible)
{
    if (direction == 0 || item >= UI_MENU_ITEM_COUNT) return item;
    uint8_t index = (uint8_t)item;
    /* Bounded rather than "until a visible one turns up": one always does
     * today, and a loop that trusts that is a hang on the day it does not. */
    for (uint8_t guard = 0U; guard < UI_MENU_ITEM_COUNT; ++guard) {
        index = direction < 0 ? (index == 0U ? UI_MENU_ITEM_COUNT - 1U : (uint8_t)(index - 1U))
                              : (uint8_t)((index + 1U) % UI_MENU_ITEM_COUNT);
        if (ui_menu_item_is_visible((ui_menu_item_t)index, yandex_visible)) break;
    }
    return (ui_menu_item_t)index;
}

void ui_menu_init(ui_menu_state_t *state)
{
    if (state != NULL) {
        state->selected_index = UI_MENU_ITEM_INTERNET_RADIO;
        /* Shown until told otherwise: the setting is read from flash after the
         * screens are built, and a row that appears is less alarming than one
         * that vanishes a moment after boot. */
        state->yandex_visible = BOARD_HAS_YANDEX_MUSIC;
    }
}

void ui_menu_set_yandex_visible(ui_menu_state_t *state, bool visible)
{
    if (state == NULL) return;
    state->yandex_visible = visible && BOARD_HAS_YANDEX_MUSIC;
    if (!ui_menu_item_is_visible((ui_menu_item_t)state->selected_index, state->yandex_visible)) {
        state->selected_index = UI_MENU_ITEM_INTERNET_RADIO;
    }
}

bool ui_menu_yandex_visible(const ui_menu_state_t *state)
{
    return state != NULL && state->yandex_visible && BOARD_HAS_YANDEX_MUSIC;
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
            action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1, state->yandex_visible);
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
        if (!ui_menu_item_is_visible((ui_menu_item_t)index, state->yandex_visible)) continue;
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
