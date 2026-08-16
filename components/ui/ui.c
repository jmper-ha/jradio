#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board.h"
#include "board_display_profile.h"
#include "device_settings.h"
#include "player_control.h"
#include "wifi_provisioning.h"
#include "ui_draw_buffer.h"
#include "ui_font_cyrillic_14.h"
#include "ui_menu.h"
#include "ui_player_state.h"
#include "ui_radio_text.h"
#include "ui_settings_model.h"
#include "ui_station_list.h"
#include "ui_usb_notice.h"

#define UI_DRAW_BUFFER_LINES 20
#define UI_DRAW_BUFFER_SIZE ui_rgb565_draw_buffer_size(TFT_WIDTH, UI_DRAW_BUFFER_LINES)
#define UI_INPUT_QUEUE_LENGTH 16
#define UI_TASK_STACK_SIZE 6144
#define UI_TASK_PRIORITY 4
#define UI_STATION_LIST_MAX_ROWS 7U
#define UI_RADIO_EMPTY_LIST_DELAY_MS 250U
#define UI_STATION_LIST_IDLE_TIMEOUT_MS 10000U
#define UI_SETTINGS_MAX_ROWS 7U

static const char *TAG = "ui";
static QueueHandle_t s_input_queue;
static ui_menu_state_t s_menu;
static lv_display_t *s_display;
static lv_obj_t *s_menu_screen;
static lv_obj_t *s_source_screen;
static lv_obj_t *s_menu_rows[UI_MENU_ITEM_COUNT];
static lv_obj_t *s_menu_notice;
static lv_obj_t *s_source_title;
static lv_obj_t *s_source_status;
static lv_obj_t *s_source_detail;
static lv_obj_t *s_source_stream;
static lv_obj_t *s_source_wifi;
static lv_obj_t *s_settings_screen;
static lv_obj_t *s_settings_rows[UI_SETTINGS_MAX_ROWS];
static lv_obj_t *s_settings_notice;
static ui_settings_model_t s_settings_model;
static device_settings_t s_device_settings;
static bool s_settings_open;
static lv_obj_t *s_station_list_screen;
static lv_obj_t *s_station_list_title;
static lv_obj_t *s_station_list_rows[UI_STATION_LIST_MAX_ROWS];
static station_list_state_t s_station_list;
static ui_player_state_t s_player_ui;
static bool s_waiting_for_radio_station;
static uint32_t s_radio_station_wait_started_ms;
// The USB browser reuses this list screen. Outside the drive's root it shows a
// ".." row above the entries, so every listing index is one below its row.
static bool s_usb_browser_has_parent_row;
static unsigned int s_usb_listing_revision;
// The USB source screen has nothing to show until a file is picked, so
// selecting the source jumps straight to the browser. The jump waits for the
// listing revision to move, otherwise the list would open on the previous
// directory's rows for a poll or two.
static bool s_usb_list_open_requested;
static unsigned int s_usb_list_open_revision;

static bool ui_list_shows_usb(void)
{
    return ui_player_state_source(&s_player_ui) == AUDIO_SOURCE_USB;
}

static size_t ui_usb_row_offset(void)
{
    return s_usb_browser_has_parent_row ? 1U : 0U;
}

static uint32_t ui_tick_get_ms(void);

static void ui_set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL) {
        return;
    }
    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static const char *ui_radio_state_text(player_playback_state_t state)
{
    switch (state) {
    case PLAYER_PLAYBACK_CONNECTING: return "Connecting...";
    case PLAYER_PLAYBACK_PLAYING: return "Playing";
    case PLAYER_PLAYBACK_PAUSED: return "Paused";
    case PLAYER_PLAYBACK_RECONNECTING: return "Reconnecting...";
    case PLAYER_PLAYBACK_ERROR: return "Connection error";
    case PLAYER_PLAYBACK_STOPPED: return "Stopped";
    }
    return "Unknown";
}

static void ui_update_usb_status(const player_snapshot_t *snapshot)
{
    ui_set_label_text_if_changed(s_source_status,
                                 snapshot->playback_state == PLAYER_PLAYBACK_STOPPED
                                     ? "Выберите файл"
                                     : ui_radio_state_text(snapshot->playback_state));
    // The USB player puts the file name where the radio puts the ICY title.
    ui_set_label_text_if_changed(s_source_detail, snapshot->stream_title);
    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), snapshot->codec,
                         snapshot->bitrate_kbps, snapshot->sample_rate_hz);
    ui_set_label_text_if_changed(s_source_stream, stream_text);
    // Nothing on this screen depends on the network, so the Wi-Fi line would
    // only be noise.
    ui_set_label_text_if_changed(s_source_wifi, "");
}

static void ui_update_radio_status(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    if (ui_player_state_source(&s_player_ui) == AUDIO_SOURCE_USB) {
        ui_update_usb_status(snapshot);
        return;
    }
    if (ui_player_state_source(&s_player_ui) != AUDIO_SOURCE_INTERNET_RADIO) return;
    ui_set_label_text_if_changed(s_source_status,
                                 ui_radio_state_text(snapshot->playback_state));
    // While a station switch is pending confirmation, snapshot->active_item_index
    // still reflects the previous station; keep showing the one the user just
    // picked instead of flipping back to the old one for the pending window.
    size_t pending_item_index;
    const size_t display_item_index =
        ui_player_state_pending_item(&s_player_ui, &pending_item_index)
            ? pending_item_index
            : snapshot->active_item_index;
    const station_catalog_entry_t *entry =
        player_control_station_at(display_item_index);
    const char *display_title = (entry != NULL && entry->flag == 0)
                                    ? entry->name
                                    : (snapshot->stream_title[0] == '\0'
                                           ? snapshot->context
                                           : snapshot->stream_title);
    if (entry != NULL) {
        ui_set_label_text_if_changed(s_source_title, entry->name);
    }
    ui_set_label_text_if_changed(s_source_detail, display_title);
    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), snapshot->codec,
                         snapshot->bitrate_kbps, snapshot->sample_rate_hz);
    ui_set_label_text_if_changed(s_source_stream, stream_text);

    char wifi_text[32];
    if (snapshot->wifi_rssi_valid) {
        snprintf(wifi_text, sizeof(wifi_text), "Wi-Fi  %d dBm",
                 (int)snapshot->wifi_rssi_dbm);
    } else {
        snprintf(wifi_text, sizeof(wifi_text), "Wi-Fi  -- dBm");
    }
    ui_set_label_text_if_changed(s_source_wifi, wifi_text);
}

static uint32_t ui_tick_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void ui_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const esp_err_t err = board_display_draw_rgb565(area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                                                     (const uint16_t *)pixels);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(err));
    }
    lv_display_flush_ready(display);
}

static void ui_update_menu_highlight(void)
{
    const uint8_t selected = ui_menu_selected_index(&s_menu);
    for (uint8_t index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        const bool is_selected = index == selected;
        const lv_color_t color = lv_color_hex(is_selected ? 0x1769AA : 0x101820);
        lv_obj_set_style_bg_color(s_menu_rows[index], color, 0);
        lv_obj_set_style_bg_opa(s_menu_rows[index], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_menu_rows[index], lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text_fmt(s_menu_rows[index], "%c %s", is_selected ? '>' : ' ',
                              ui_menu_item_label((ui_menu_item_t)index));
    }
}

static void ui_create_menu_screen(void)
{
    s_menu_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_menu_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_menu_screen, 0, 0);
    lv_obj_set_style_pad_all(s_menu_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_menu_screen, &ui_font_cyrillic_14, 0);

    lv_obj_t *title = lv_label_create(s_menu_screen);
    lv_label_set_text(title, "jradio");
    lv_obj_set_pos(title, 12, 8);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    for (uint8_t index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        s_menu_rows[index] = lv_label_create(s_menu_screen);
        lv_label_set_text(s_menu_rows[index], ui_menu_item_label((ui_menu_item_t)index));
        lv_obj_set_pos(s_menu_rows[index], 14, 38 + index * 27);
        lv_obj_set_size(s_menu_rows[index], 292, 24);
        lv_obj_set_style_pad_left(s_menu_rows[index], 6, 0);
        lv_obj_set_style_pad_top(s_menu_rows[index], 2, 0);
        lv_obj_set_style_radius(s_menu_rows[index], 3, 0);
    }

    // Not a key hint: the only thing this line ever says is why a source
    // refused to open, and it is empty the rest of the time.
    s_menu_notice = lv_label_create(s_menu_screen);
    lv_label_set_text(s_menu_notice, "");
    lv_obj_set_pos(s_menu_notice, 12, 220);
    lv_obj_set_style_text_color(s_menu_notice, lv_color_hex(0xFFD54F), 0);
    ui_update_menu_highlight();
}

// Fills `text` with what the row at `list_index` should read, and reports
// whether that row is the one currently playing.
static bool ui_list_row_text(size_t list_index, char *text, size_t text_size, bool *active)
{
    *active = false;
    if (!ui_list_shows_usb()) {
        const station_catalog_entry_t *entry = player_control_station_at(list_index);
        snprintf(text, text_size, "%s", entry == NULL ? "" : entry->name);
        *active = list_index == station_list_active_index(&s_station_list);
        return true;
    }
    if (s_usb_browser_has_parent_row && list_index == 0U) {
        snprintf(text, text_size, "..");
        return true;
    }
    usb_browser_entry_t entry;
    if (!player_control_usb_entry_at(list_index - ui_usb_row_offset(), &entry)) {
        text[0] = '\0';
        return false;
    }
    // Directories are marked rather than merely sorted first, so the row tells
    // you what the encoder click will do before you press it.
    if (entry.kind == USB_BROWSER_ENTRY_DIRECTORY) {
        snprintf(text, text_size, "[%s]", entry.name);
    } else {
        snprintf(text, text_size, "%s", entry.name);
    }
    *active = list_index == station_list_active_index(&s_station_list);
    return true;
}

static void ui_update_station_list(void)
{
    size_t cursor_row = 0U;
    const int count = (int)s_station_list.count;
    const int window_top = station_list_window_top(&s_station_list,
                                                   UI_STATION_LIST_MAX_ROWS, &cursor_row);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        const int entry_index = window_top + (int)row;
        if (entry_index < 0 || entry_index >= count) {
            // Padding: the cursor stays on the middle row, so the rows beyond
            // either end of the catalogue are simply blank.
            lv_obj_add_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
        char text[USB_BROWSER_NAME_MAX_LEN + 3U];
        bool active = false;
        (void)ui_list_row_text((size_t)entry_index, text, sizeof(text), &active);
        const bool selected = row == cursor_row;
        lv_obj_set_style_bg_color(s_station_list_rows[row], lv_color_hex(selected ? 0x1769AA : 0x101820), 0);
        lv_obj_set_style_bg_opa(s_station_list_rows[row], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_station_list_rows[row], active ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_station_list_rows[row], lv_color_hex(0xFFD54F), 0);
        lv_obj_set_style_border_opa(s_station_list_rows[row], LV_OPA_COVER, 0);
        lv_label_set_text_fmt(s_station_list_rows[row], "%c %s", selected ? '>' : ' ', text);
    }
}

static void ui_load_menu_screen(void);

static void ui_create_settings_screen(void)
{
    s_settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_settings_screen, 0, 0);
    lv_obj_set_style_pad_all(s_settings_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_settings_screen, &ui_font_cyrillic_14, 0);

    lv_obj_t *title = lv_label_create(s_settings_screen);
    lv_label_set_text(title, "Настройки");
    lv_obj_set_pos(title, 12, 8);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        s_settings_rows[row] = lv_label_create(s_settings_screen);
        lv_obj_set_pos(s_settings_rows[row], 10, 36 + (int)row * 26);
        lv_obj_set_width(s_settings_rows[row], 300);
        lv_obj_set_height(s_settings_rows[row], 24);
        lv_obj_set_style_pad_left(s_settings_rows[row], 6, 0);
        lv_obj_set_style_pad_top(s_settings_rows[row], 3, 0);
        lv_label_set_long_mode(s_settings_rows[row], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_settings_rows[row], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_settings_rows[row], LV_OPA_COVER, 0);
        lv_label_set_text(s_settings_rows[row], "");
    }
    s_settings_notice = lv_label_create(s_settings_screen);
    lv_obj_set_pos(s_settings_notice, 10, 218);
    lv_obj_set_width(s_settings_notice, 300);
    lv_label_set_long_mode(s_settings_notice, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_settings_notice, lv_color_hex(0xFFCC80), 0);
    lv_label_set_text(s_settings_notice, "");
}

static const char *ui_settings_group_text(ui_settings_group_t group, bool english)
{
    switch (group) {
    case UI_SETTINGS_GROUP_LANGUAGE: return english ? "Language" : "Язык";
    case UI_SETTINGS_GROUP_GENERAL: return english ? "General" : "Общие";
    case UI_SETTINGS_GROUP_DISPLAY: return english ? "Display" : "Экран";
    default: return "";
    }
}

static void ui_settings_row_text(const ui_settings_row_t *row, char *text, size_t text_size)
{
    const bool english = s_device_settings.language == DEVICE_LANGUAGE_EN;
    const char *group = ui_settings_group_text(row->group, english);
    if (row->kind == UI_SETTINGS_ROW_GROUP) {
        snprintf(text, text_size, "%c %s",
                 ui_settings_model_is_expanded(&s_settings_model, row->group) ? 'v' : '>', group);
        return;
    }
    switch (row->id) {
    case UI_SETTINGS_ROW_LANGUAGE_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Language" : "Язык",
                 english ? "English" : "Русский");
        break;
    case UI_SETTINGS_ROW_HOME_SCREEN_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Home screen" : "Главный экран",
                 s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED ?
                     (english ? "Feed" : "Лента") : (english ? "Text" : "Текст"));
        break;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Flip vertical" : "Поворот по вертикали",
                 s_device_settings.flip_vertical ? "ON" : "OFF");
        break;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Flip horizontal" : "Поворот по горизонтали",
                 s_device_settings.flip_horizontal ? "ON" : "OFF");
        break;
    default:
        text[0] = '\0';
        break;
    }
}

static void ui_update_settings(void)
{
    if (!s_settings_open) return;
    const size_t row_count = ui_settings_model_row_count(&s_settings_model);
    const ui_settings_row_id_t selected = ui_settings_model_selected(&s_settings_model);
    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        if (row >= row_count) {
            lv_label_set_text(s_settings_rows[row], "");
            continue;
        }
        const ui_settings_row_t item = ui_settings_model_row_at(&s_settings_model, row);
        char text[96];
        ui_settings_row_text(&item, text, sizeof(text));
        ui_set_label_text_if_changed(s_settings_rows[row], text);
        const bool selected_row = item.id == selected;
        const uint32_t background = selected_row ? 0x1769AA :
            item.kind == UI_SETTINGS_ROW_FIELD ? 0x263746 : 0x101820;
        lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(background), 0);
    }
}

static void ui_show_settings(void)
{
    s_settings_open = true;
    ui_settings_model_init(&s_settings_model);
    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
    } else {
        lv_label_set_text(s_settings_notice, "");
        (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                         s_device_settings.flip_horizontal);
    }
    ui_update_settings();
    lv_screen_load(s_settings_screen);
}

static void ui_close_settings(void)
{
    if (!s_settings_open) return;
    s_settings_open = false;
    ui_load_menu_screen();
}

static void ui_settings_change_selected(void)
{
    const ui_settings_row_id_t selected = ui_settings_model_selected(&s_settings_model);
    bool changed = false;
    switch (selected) {
    case UI_SETTINGS_ROW_LANGUAGE_FIELD:
        changed = device_settings_set_language(
            &s_device_settings,
            s_device_settings.language == DEVICE_LANGUAGE_EN ? DEVICE_LANGUAGE_RU : DEVICE_LANGUAGE_EN);
        break;
    case UI_SETTINGS_ROW_HOME_SCREEN_FIELD:
        changed = device_settings_set_home_screen(
            &s_device_settings,
            s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED ?
                DEVICE_HOME_SCREEN_TEXT : DEVICE_HOME_SCREEN_FEED);
        break;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        changed = device_settings_set_flip_vertical(&s_device_settings,
                                                    !s_device_settings.flip_vertical);
        if (changed) {
            (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                             s_device_settings.flip_horizontal);
        }
        break;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        changed = device_settings_set_flip_horizontal(&s_device_settings,
                                                      !s_device_settings.flip_horizontal);
        if (changed) {
            (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                             s_device_settings.flip_horizontal);
        }
        break;
    default:
        return;
    }
    lv_label_set_text(s_settings_notice, changed ? "" : "Ошибка записи settings.csv");
}

static void ui_create_station_list_screen(void)
{
    s_station_list_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_station_list_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_station_list_screen, 0, 0);
    lv_obj_set_style_pad_all(s_station_list_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_station_list_screen, &ui_font_cyrillic_14, 0);
    s_station_list_title = lv_label_create(s_station_list_screen);
    lv_label_set_text(s_station_list_title, "Internet radio | Stations");
    lv_obj_set_pos(s_station_list_title, 12, 8);
    lv_obj_set_width(s_station_list_title, 300);
    lv_label_set_long_mode(s_station_list_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_station_list_title, lv_color_hex(0xFFFFFF), 0);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        s_station_list_rows[row] = lv_label_create(s_station_list_screen);
        lv_obj_set_pos(s_station_list_rows[row], 10, 34 + (int)row * 26);
        lv_obj_set_size(s_station_list_rows[row], 300, 24);
        lv_obj_set_style_pad_left(s_station_list_rows[row], 6, 0);
        lv_obj_set_style_pad_top(s_station_list_rows[row], 2, 0);
        lv_obj_set_style_radius(s_station_list_rows[row], 3, 0);
        lv_obj_set_style_text_color(s_station_list_rows[row], lv_color_hex(0xFFFFFF), 0);
    }
}

static void ui_show_station_list(void);

static void ui_create_source_screen(void)
{
    s_source_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_source_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_source_screen, 0, 0);
    lv_obj_set_style_pad_all(s_source_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_source_screen, &ui_font_cyrillic_14, 0);

    s_source_title = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_title, 14, 32);
    lv_obj_set_style_text_color(s_source_title, lv_color_hex(0xFFFFFF), 0);

    s_source_status = lv_label_create(s_source_screen);
    lv_label_set_text(s_source_status, "Not implemented");
    lv_obj_set_pos(s_source_status, 14, 64);
    // Carries Russian copy for the USB source; the built-in LVGL font has no
    // Cyrillic and renders it as empty boxes.
    lv_obj_set_style_text_color(s_source_status, lv_color_hex(0xB0BEC5), 0);

    s_source_detail = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_detail, 14, 92);
    lv_obj_set_width(s_source_detail, 290);
    lv_obj_set_height(s_source_detail, 52);
    lv_label_set_long_mode(s_source_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_source_detail, lv_color_hex(0xFFFFFF), 0);

    s_source_stream = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_stream, 14, 154);
    lv_obj_set_style_text_color(s_source_stream, lv_color_hex(0xB0BEC5), 0);

    s_source_wifi = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_wifi, 14, 180);
    lv_obj_set_style_text_color(s_source_wifi, lv_color_hex(0xB0BEC5), 0);

}

static bool ui_submit_player_command(const player_command_t *command)
{
    if (!ui_player_state_can_post(&s_player_ui, command)) {
        ESP_LOGW(TAG, "player command rejected: busy or invalid; kind=%d",
                 command == NULL ? -1 : (int)command->kind);
        return false;
    }
    if (!player_control_post(command)) {
        ESP_LOGW(TAG, "player command queue full; kind=%d", (int)command->kind);
        return false;
    }
    if (!ui_player_state_apply_post_result(&s_player_ui, command, true,
                                           ui_tick_get_ms())) {
        ESP_LOGW(TAG, "player command rejected after post; kind=%d",
                 (int)command->kind);
        return false;
    }
    return true;
}

static void ui_load_source_screen(audio_source_t selected_source)
{
    (void)ui_menu_select_source(&s_menu, selected_source);
    const uint8_t index = ui_menu_selected_index(&s_menu);
    lv_label_set_text(s_source_title, ui_menu_item_label((ui_menu_item_t)index));
    lv_screen_load(s_source_screen);
    if (selected_source == AUDIO_SOURCE_INTERNET_RADIO) {
        ui_set_label_text_if_changed(s_source_status, "Connecting...");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        ui_set_label_text_if_changed(s_source_wifi, "");
        s_waiting_for_radio_station = true;
        s_radio_station_wait_started_ms = ui_tick_get_ms();
    } else if (selected_source == AUDIO_SOURCE_USB) {
        s_waiting_for_radio_station = false;
        // Nothing plays until a file is chosen, so this screen opens idle
        // rather than pretending to connect.
        ui_set_label_text_if_changed(s_source_status, "Выберите файл");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        ui_set_label_text_if_changed(s_source_wifi, "");
    } else {
        s_waiting_for_radio_station = false;
        ui_set_label_text_if_changed(s_source_status, "Not implemented");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        ui_set_label_text_if_changed(s_source_wifi, "");
    }
}

static void ui_show_source(void)
{
    const audio_source_t selected_source = ui_menu_activate(&s_menu);
    if (selected_source == AUDIO_SOURCE_NONE) return;
    if (selected_source == AUDIO_SOURCE_USB) {
        // The UI switches screens optimistically, so without this check the
        // user gets an empty browser instead of being told what is wrong -
        // and "missing" needs different words from "unreadable" or "empty".
        player_snapshot_t snapshot;
        player_control_get_snapshot(&snapshot);
        const char *notice = ui_usb_notice(snapshot.usb_media, snapshot.usb_entry_count);
        if (notice != NULL) {
            ui_set_label_text_if_changed(s_menu_notice, notice);
            return;
        }
    }
    ui_set_label_text_if_changed(s_menu_notice, "");
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    const player_command_t command = {
        .kind = PLAYER_COMMAND_SELECT_SOURCE,
        .source = selected_source,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (selected_source == AUDIO_SOURCE_USB) {
        s_usb_list_open_revision = player_control_usb_listing_revision();
        s_usb_list_open_requested = true;
    }
    if (!ui_submit_player_command(&command)) return;
    if (old_view != ui_player_state_view(&s_player_ui) ||
        old_source != ui_player_state_source(&s_player_ui)) {
        ui_load_source_screen(ui_player_state_source(&s_player_ui));
    }
}

static void ui_load_menu_screen(void)
{
    s_waiting_for_radio_station = false;
    ui_update_menu_highlight();
    lv_screen_load(s_menu_screen);
}

static void ui_show_menu(void)
{
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    const player_command_t command = {
        .kind = PLAYER_COMMAND_STOP_SOURCE,
        .source = AUDIO_SOURCE_NONE,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (!ui_submit_player_command(&command)) return;
    if (old_view != ui_player_state_view(&s_player_ui) ||
        old_source != ui_player_state_source(&s_player_ui)) {
        ui_load_menu_screen();
    }
}

// Re-seeds the list state from a fresh snapshot. Split out because the USB
// browser has to do it again on every directory change, not just on open.
static void ui_reset_list_from_snapshot(const player_snapshot_t *snapshot)
{
    size_t count = snapshot->item_count;
    size_t active_index = snapshot->active_item_index;
    if (ui_list_shows_usb()) {
        s_usb_browser_has_parent_row = !usb_browser_path_is_root(snapshot->context);
        const size_t offset = ui_usb_row_offset();
        count += offset;
        // The ".." row pushes every entry down, including the playing one.
        active_index = active_index == PLAYER_ITEM_NONE ? PLAYER_ITEM_NONE
                                                        : active_index + offset;
        lv_label_set_text_fmt(s_station_list_title, "USB | %s", snapshot->context);
    } else {
        s_usb_browser_has_parent_row = false;
        ui_set_label_text_if_changed(s_station_list_title, "Internet radio | Stations");
    }
    const size_t initial_index = active_index < count ? active_index : 0U;
    station_list_init(&s_station_list, count, initial_index, active_index);
    station_list_note_activity(&s_station_list, ui_tick_get_ms());
    ui_update_station_list();
}

static void ui_load_station_list_screen(void)
{
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "show list: source=%d player_state=%d active_item=%u",
             (int)snapshot.active_source, (int)snapshot.playback_state,
             (unsigned int)snapshot.active_item_index);
    s_waiting_for_radio_station = false;
    s_usb_listing_revision = player_control_usb_listing_revision();
    ui_reset_list_from_snapshot(&snapshot);
    lv_screen_load(s_station_list_screen);
}

static void ui_show_station_list(void)
{
    if (!ui_player_state_show_station_list(&s_player_ui)) return;
    s_waiting_for_radio_station = false;
    ui_load_station_list_screen();
}

static void ui_close_station_list_to_source(void)
{
    ui_player_state_close_station_list(&s_player_ui);
    lv_screen_load(s_source_screen);
}

static void ui_render_player_state(void)
{
    switch (ui_player_state_view(&s_player_ui)) {
    case UI_PLAYER_VIEW_MENU:
        ui_load_menu_screen();
        break;
    case UI_PLAYER_VIEW_SOURCE:
        ui_load_source_screen(ui_player_state_source(&s_player_ui));
        break;
    case UI_PLAYER_VIEW_STATION_LIST:
        ui_load_station_list_screen();
        break;
    }
}

static void ui_handle_input(board_input_action_t action)
{
    // Settings sits outside the player's view state: it shows no source and
    // starts nothing, so making it a fourth view would put an entry in every
    // transition table for no gain. Any of the three ways out returns to the
    // main screen.
    if (s_settings_open) {
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_close_settings();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            const int direction = action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1;
            (void)ui_settings_model_move(&s_settings_model, direction);
            ui_update_settings();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const ui_settings_row_t row = ui_settings_model_row_at(
                &s_settings_model, s_settings_model.cursor);
            if (row.kind == UI_SETTINGS_ROW_GROUP) {
                (void)ui_settings_model_activate(&s_settings_model);
            } else {
                ui_settings_change_selected();
            }
            ui_update_settings();
        }
        return;
    }
    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_STATION_LIST) {
        station_list_note_activity(&s_station_list, ui_tick_get_ms());
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            if (station_list_handle_input(&s_station_list, action)) ui_update_station_list();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON && ui_list_shows_usb()) {
            size_t row;
            if (!station_list_get_selection(&s_station_list, &row)) return;
            if (s_usb_browser_has_parent_row && row == 0U) {
                const player_command_t up = {
                    .kind = PLAYER_COMMAND_BROWSE_UP,
                    .source = AUDIO_SOURCE_USB,
                    .item_index = PLAYER_ITEM_NONE,
                };
                (void)ui_submit_player_command(&up);
                return;
            }
            const size_t index = row - ui_usb_row_offset();
            usb_browser_entry_t entry;
            if (!player_control_usb_entry_at(index, &entry)) return;
            const player_command_t command = {
                .kind = PLAYER_COMMAND_SELECT_ITEM,
                .source = AUDIO_SOURCE_USB,
                .item_index = index,
            };
            if (!ui_submit_player_command(&command)) return;
            // Opening a directory keeps the browser on screen; the new listing
            // arrives through the snapshot poll. Only a file switches to the
            // player.
            if (entry.kind == USB_BROWSER_ENTRY_DIRECTORY) return;
            ui_load_source_screen(AUDIO_SOURCE_USB);
            lv_label_set_text(s_source_title, ui_menu_item_label(UI_MENU_ITEM_USB_FILES));
            ui_set_label_text_if_changed(s_source_status, "Открытие файла");
            ui_set_label_text_if_changed(s_source_detail, entry.name);
            ui_set_label_text_if_changed(s_source_stream,
                                         usb_browser_format_name(entry.format));
            ui_set_label_text_if_changed(s_source_wifi, "");
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            size_t index;
            if (!station_list_get_selection(&s_station_list, &index) ||
                !ui_player_state_can_select_item(&s_player_ui, index)) return;
            const station_catalog_entry_t *entry = player_control_station_at(index);
            const player_command_t command = {
                .kind = PLAYER_COMMAND_SELECT_ITEM,
                .source = AUDIO_SOURCE_INTERNET_RADIO,
                .item_index = index,
            };
            if (!ui_submit_player_command(&command)) return;
            ui_load_source_screen(AUDIO_SOURCE_INTERNET_RADIO);
            if (entry != NULL) lv_label_set_text(s_source_title, entry->name);
            ui_set_label_text_if_changed(s_source_status, "Connecting");
            ui_set_label_text_if_changed(s_source_detail,
                                         entry == NULL ? "" : entry->name);
            char stream_text[32];
            ui_radio_stream_text_for_url(stream_text, sizeof(stream_text),
                                         entry == NULL ? NULL : entry->url);
            ui_set_label_text_if_changed(s_source_stream, stream_text);
            ui_set_label_text_if_changed(s_source_wifi, "");
        }
        return;
    }
    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_SOURCE) {
        const audio_source_t source = ui_player_state_source(&s_player_ui);
        const bool has_list = source == AUDIO_SOURCE_INTERNET_RADIO ||
                              source == AUDIO_SOURCE_USB;
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        } else if (has_list && action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            ui_show_station_list();
        } else if (has_list && action == BOARD_INPUT_ACTION_F3) {
            const player_command_t command = {
                .kind = PLAYER_COMMAND_TOGGLE,
                .source = AUDIO_SOURCE_NONE,
                .item_index = PLAYER_ITEM_NONE,
            };
            (void)ui_submit_player_command(&command);
        }
        return;
    }
    if (ui_menu_handle_input(&s_menu, action)) {
        // Clears the "insert a drive" notice as soon as the user moves on.
        ui_set_label_text_if_changed(s_menu_notice, "");
        ui_update_menu_highlight();
    } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
        if (ui_menu_selection_is_settings(&s_menu)) {
            ui_show_settings();
            return;
        }
        ui_show_source();
    }
}

static void ui_sync_player_snapshot(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    ui_player_state_apply_snapshot(&s_player_ui, snapshot, ui_tick_get_ms());
    if (old_view != ui_player_state_view(&s_player_ui) ||
        old_source != ui_player_state_source(&s_player_ui)) {
        ui_render_player_state();
    }

    if (s_usb_list_open_requested &&
        ui_player_state_source(&s_player_ui) == AUDIO_SOURCE_USB &&
        ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_SOURCE &&
        player_control_usb_listing_revision() != s_usb_list_open_revision) {
        s_usb_list_open_requested = false;
        ui_show_station_list();
    }

    if (s_waiting_for_radio_station &&
        snapshot->active_source == AUDIO_SOURCE_INTERNET_RADIO &&
        snapshot->active_item_index == PLAYER_ITEM_NONE &&
        snapshot->playback_state == PLAYER_PLAYBACK_STOPPED &&
        (uint32_t)(ui_tick_get_ms() - s_radio_station_wait_started_ms) >=
            UI_RADIO_EMPTY_LIST_DELAY_MS) {
        ui_show_station_list();
    }

    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_STATION_LIST) {
        const unsigned int revision = player_control_usb_listing_revision();
        if (ui_list_shows_usb() && revision != s_usb_listing_revision) {
            // A directory was opened or left: the rows now describe different
            // files, so the cursor cannot keep its position.
            s_usb_listing_revision = revision;
            ui_reset_list_from_snapshot(snapshot);
        } else if (station_list_sync_counts(&s_station_list,
                                            snapshot->item_count + ui_usb_row_offset(),
                                            snapshot->active_item_index == PLAYER_ITEM_NONE
                                                ? PLAYER_ITEM_NONE
                                                : snapshot->active_item_index +
                                                      ui_usb_row_offset())) {
            // The playlist can be replaced from the web UI while this screen is
            // open, so the count captured at open time may be stale.
            ui_update_station_list();
        }
        // The timeout exists to return to something worth looking at. With
        // nothing playing there is nothing behind this screen, so browsing is
        // allowed to take as long as the user wants.
        const bool playback_running =
            snapshot->playback_state == PLAYER_PLAYBACK_PLAYING ||
            snapshot->playback_state == PLAYER_PLAYBACK_PAUSED ||
            snapshot->playback_state == PLAYER_PLAYBACK_CONNECTING ||
            snapshot->playback_state == PLAYER_PLAYBACK_RECONNECTING;
        if (playback_running &&
            station_list_idle_timeout_elapsed(&s_station_list, ui_tick_get_ms(),
                                              UI_STATION_LIST_IDLE_TIMEOUT_MS)) {
            ui_close_station_list_to_source();
        }
    }
    ui_update_radio_status(snapshot);
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        board_input_action_t action;
        if (xQueueReceive(s_input_queue, &action, pdMS_TO_TICKS(10)) == pdTRUE) {
            ui_handle_input(action);
        }
        player_snapshot_t snapshot;
        player_control_get_snapshot(&snapshot);
        if (s_settings_open) {
            ui_update_settings();
        } else {
            ui_sync_player_snapshot(&snapshot);
        }
        lv_timer_handler();
    }
}

esp_err_t ui_init(void)
{
    if (s_input_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "init memory: internal_free=%u internal_largest=%u dma_largest=%u draw_buffer=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned int)UI_DRAW_BUFFER_SIZE);
    s_input_queue = xQueueCreate(UI_INPUT_QUEUE_LENGTH, sizeof(board_input_action_t));
    if (s_input_queue == NULL) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ui_menu_init(&s_menu);
    ui_player_state_init(&s_player_ui);
    s_waiting_for_radio_station = false;
    lv_init();
    lv_tick_set_cb(ui_tick_get_ms);

    lv_color_t *buffer1 = heap_caps_malloc(UI_DRAW_BUFFER_SIZE,
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer1 == NULL) {
        ESP_LOGE(TAG, "draw buffer allocation failed: buffer=%p dma_largest=%u",
                 buffer1,
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        heap_caps_free(buffer1);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "LVGL display allocation failed: internal_largest=%u",
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        heap_caps_free(buffer1);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, buffer1, NULL, UI_DRAW_BUFFER_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, ui_flush);
    ui_create_menu_screen();
    ui_create_settings_screen();
    ui_create_source_screen();
    ui_create_station_list_screen();
    lv_screen_load(s_menu_screen);

    // Pinned to core 0 alongside Wi-Fi and lwIP: LVGL rendering and the
    // synchronous SPI flush are not time critical, but they are long enough to
    // stall the audio decoder pinned to core 1 if the scheduler puts them
    // there.
    if (xTaskCreatePinnedToCore(ui_task, "ui", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY,
                                NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "UI task allocation failed: internal_free=%u internal_largest=%u",
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        lv_display_delete(s_display);
        s_display = NULL;
        heap_caps_free(buffer1);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LVGL UI initialized");
    return ESP_OK;
}

bool ui_post_input(board_input_action_t action)
{
    if (s_input_queue == NULL || action == BOARD_INPUT_ACTION_NONE) {
        return false;
    }
    if (xQueueSend(s_input_queue, &action, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)action);
        return false;
    }
    return true;
}
