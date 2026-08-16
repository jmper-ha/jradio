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
#include "ui_autoplay.h"
#include "ui_click_gesture.h"
#include "ui_draw_buffer.h"
#include "ui_font_cyrillic_14.h"
#include "ui_menu.h"
#include "ui_feed_icons.h"
#include "ui_feed_model.h"
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
static lv_obj_t *s_feed_screen;
static lv_obj_t *s_feed_title;
static lv_obj_t *s_feed_notice;
static lv_obj_t *s_feed_icons[5];
static ui_feed_model_t s_feed_model;
static lv_obj_t *s_source_title;
static lv_obj_t *s_source_status;
static lv_obj_t *s_source_detail;
static lv_obj_t *s_source_stream;
static lv_obj_t *s_source_wifi;
static lv_obj_t *s_settings_screen;
static lv_obj_t *s_settings_rows[UI_SETTINGS_MAX_ROWS];
#define UI_SETTINGS_SWITCH_COUNT 3U
static lv_obj_t *s_settings_switches[UI_SETTINGS_SWITCH_COUNT];
static lv_obj_t *s_settings_notice;
static ui_settings_model_t s_settings_model;
static device_settings_t s_device_settings;
static bool s_settings_open;
static lv_obj_t *s_station_list_screen;
static lv_obj_t *s_station_list_title;
static lv_obj_t *s_station_list_notice;
static lv_obj_t *s_station_list_rows[UI_STATION_LIST_MAX_ROWS];
static station_list_state_t s_station_list;
static ui_player_state_t s_player_ui;
// Only the player screen tells a single click from a double one, so only
// there does a click wait out the window; every other screen acts at once.
static ui_click_gesture_t s_player_click;
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
// The USB screen can be opened with no usable drive: it then shows the reason
// instead of rows. Kept from the last snapshot because ui_show_source() runs
// on a key press, not on the poll that carries the snapshot.
static bool s_usb_unavailable;
static usb_browser_media_t s_last_usb_media = USB_BROWSER_MEDIA_ABSENT;
static size_t s_last_usb_entry_count;
// Autoplay runs once, and only after the drive has had time to appear: USB
// mounts around five seconds in, later still when the root port needs
// re-enumerating, so deciding "no drive" any earlier would be deciding it
// before the answer exists.
#define UI_AUTOPLAY_USB_WAIT_MS 12000U
static bool s_autoplay_pending;
static uint32_t s_autoplay_started_ms;
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

static const char *ui_feed_item_title(ui_feed_item_t item)
{
    return ui_menu_item_label((ui_menu_item_t)item);
}

static void ui_update_feed_screen(void)
{
    const ui_feed_item_t selected = ui_feed_model_selected(&s_feed_model);
    const int offsets[5] = {-2, -1, 0, 1, 2};
    const int positions[5] = {4, 54, 108, 214, 264};
    for (size_t slot = 0; slot < 5U; ++slot) {
        int index = (int)selected + offsets[slot];
        while (index < 0) index += UI_FEED_ITEM_COUNT;
        index %= UI_FEED_ITEM_COUNT;
        const ui_feed_item_t item = (ui_feed_item_t)index;
        const bool center = slot == 2U;
        lv_label_set_text(s_feed_icons[slot], ui_feed_icon_symbol(item));
        lv_obj_set_pos(s_feed_icons[slot], positions[slot], center ? 66 : 96);
        lv_obj_set_size(s_feed_icons[slot], center ? 104 : 52, center ? 104 : 52);
        lv_obj_set_style_text_font(s_feed_icons[slot],
                                   center ? &lv_font_montserrat_48 : &lv_font_montserrat_24, 0);
        /* Keep the tile mathematically centered and use a small vertical
         * inset so the visible FontAwesome shape, not its line box, is centered. */
        lv_obj_set_style_pad_left(s_feed_icons[slot], 0, 0);
        lv_obj_set_style_pad_right(s_feed_icons[slot], 0, 0);
        lv_obj_set_style_pad_top(s_feed_icons[slot], center ? 20 : 0, 0);
        lv_obj_set_style_pad_bottom(s_feed_icons[slot], 0, 0);
        lv_obj_set_style_text_color(s_feed_icons[slot],
                                    lv_color_hex(center ? 0xFFFFFF : 0x90A4AE), 0);
        lv_obj_set_style_text_align(s_feed_icons[slot], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_border_width(s_feed_icons[slot], center ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_feed_icons[slot], lv_color_hex(0x29B6F6), 0);
        lv_obj_set_style_border_opa(s_feed_icons[slot], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_feed_icons[slot], 8, 0);
        lv_obj_set_style_bg_color(s_feed_icons[slot],
                                  center ? lv_color_hex(0x1769AA) : lv_color_hex(0x101820), 0);
        lv_obj_set_style_bg_opa(s_feed_icons[slot], center ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    lv_label_set_text(s_feed_title, ui_feed_item_title(selected));
}

static void ui_create_feed_screen(void)
{
    s_feed_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_feed_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_feed_screen, 0, 0);
    lv_obj_set_style_pad_all(s_feed_screen, 0, 0);
    lv_obj_set_style_text_font(s_feed_screen, &ui_font_cyrillic_14, 0);
    s_feed_title = lv_label_create(s_feed_screen);
    lv_obj_set_pos(s_feed_title, 0, 12);
    lv_obj_set_size(s_feed_title, 320, 28);
    lv_obj_set_style_text_align(s_feed_title, LV_TEXT_ALIGN_CENTER, 0);
    /* Keep the Cyrillic-capable 14 px font, but enlarge the rendered title
     * without pulling another multi-language font into flash. */
    lv_obj_set_style_transform_scale(s_feed_title, 320, 0);
    lv_obj_set_style_transform_pivot_x(s_feed_title, 160, 0);
    lv_obj_set_style_transform_pivot_y(s_feed_title, 14, 0);
    lv_obj_set_style_text_color(s_feed_title, lv_color_hex(0xFFFFFF), 0);
    s_feed_notice = lv_label_create(s_feed_screen);
    lv_obj_set_pos(s_feed_notice, 8, 218);
    lv_obj_set_size(s_feed_notice, 304, 18);
    lv_obj_set_style_text_align(s_feed_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_feed_notice, lv_color_hex(0xFFCC80), 0);
    lv_label_set_text(s_feed_notice, "");
    for (size_t index = 0; index < 5U; ++index) {
        s_feed_icons[index] = lv_label_create(s_feed_screen);
        lv_obj_set_style_text_font(s_feed_icons[index], &lv_font_montserrat_24, 0);
        lv_obj_set_style_pad_all(s_feed_icons[index], 0, 0);
    }
    ui_feed_model_init(&s_feed_model, 0U);
    ui_update_feed_screen();
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
        lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(0x101820), 0);
        lv_label_set_text(s_settings_rows[row], "");
    }
    for (size_t index = 0; index < UI_SETTINGS_SWITCH_COUNT; ++index) {
        s_settings_switches[index] = lv_switch_create(s_settings_screen);
        lv_obj_set_size(s_settings_switches[index], 42, 20);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(0x546E7A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(0x26A69A),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(0xECEFF1), LV_PART_KNOB);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(0xFFFFFF),
                                  LV_PART_KNOB | LV_STATE_CHECKED);
        lv_obj_add_flag(s_settings_switches[index], LV_OBJ_FLAG_HIDDEN);
    }
    s_settings_notice = lv_label_create(s_settings_screen);
    lv_obj_set_pos(s_settings_notice, 10, 218);
    lv_obj_set_width(s_settings_notice, 300);
    lv_label_set_long_mode(s_settings_notice, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_settings_notice, lv_color_hex(0xFFCC80), 0);
    lv_obj_set_style_bg_opa(s_settings_notice, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_settings_notice, lv_color_hex(0x101820), 0);
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
        snprintf(text, text_size, "%s", group);
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
                     (english ? "Feed" : "Лента") : (english ? "List" : "Список"));
        break;
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Autoplay" : "Автовоспроизведение",
                 s_device_settings.autoplay ? "ON" : "OFF");
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

/* Maps a row to its switch, or answers false for rows that have none. Kept as
 * one lookup rather than a chain of ternaries: every boolean setting needs the
 * index, the current value and the "has a switch" test to agree, and three
 * separate expressions is how they stop agreeing. */
static bool ui_settings_row_switch(ui_settings_row_id_t id, size_t *index, bool *value)
{
    switch (id) {
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        *index = 0U;
        *value = s_device_settings.autoplay;
        return true;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        *index = 1U;
        *value = s_device_settings.flip_vertical;
        return true;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        *index = 2U;
        *value = s_device_settings.flip_horizontal;
        return true;
    default:
        return false;
    }
}

static void ui_update_settings(void)
{
    if (!s_settings_open) return;
    const size_t row_count = ui_settings_model_row_count(&s_settings_model);
    const ui_settings_row_id_t selected = ui_settings_model_selected(&s_settings_model);
    for (size_t index = 0; index < UI_SETTINGS_SWITCH_COUNT; ++index) {
        lv_obj_add_flag(s_settings_switches[index], LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        if (row >= row_count) {
            lv_label_set_text(s_settings_rows[row], "");
            lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(0x101820), 0);
            continue;
        }
        const ui_settings_row_t item = ui_settings_model_row_at(&s_settings_model, row);
        char text[96];
        ui_settings_row_text(&item, text, sizeof(text));
        ui_set_label_text_if_changed(s_settings_rows[row], text);
        const bool selected_row = item.id == selected;
        const uint32_t background = selected_row ? 0x1769AA :
            item.kind == UI_SETTINGS_ROW_FIELD ? 0x263746 : 0x101820;
        size_t switch_index = 0U;
        bool enabled = false;
        const bool has_switch = ui_settings_row_switch(item.id, &switch_index, &enabled);
        lv_obj_set_x(s_settings_rows[row], has_switch ? 58 : 10);
        lv_obj_set_width(s_settings_rows[row], has_switch ? 242 : 300);
        lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(background), 0);
        if (has_switch) {
            lv_obj_t *toggle = s_settings_switches[switch_index];
            if (enabled) lv_obj_add_state(toggle, LV_STATE_CHECKED);
            else lv_obj_clear_state(toggle, LV_STATE_CHECKED);
            lv_obj_set_pos(toggle, 10, 38 + (int)row * 26);
            lv_obj_clear_flag(toggle, LV_OBJ_FLAG_HIDDEN);
        }
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
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        changed = device_settings_set_autoplay(&s_device_settings,
                                               !s_device_settings.autoplay);
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
    s_station_list_notice = lv_label_create(s_station_list_screen);
    lv_label_set_text(s_station_list_notice, "");
    lv_obj_set_pos(s_station_list_notice, 14, 100);
    lv_obj_set_width(s_station_list_notice, 292);
    lv_obj_set_style_text_color(s_station_list_notice, lv_color_hex(0xFFD54F), 0);
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
    // A drive that cannot be browsed still opens its screen - the list, empty,
    // saying why. Refusing to leave the menu left the user pressing a working
    // button with nothing happening but a line of small print.
    s_usb_unavailable = selected_source == AUDIO_SOURCE_USB &&
                        !ui_usb_can_open(s_last_usb_media, s_last_usb_entry_count);
    ui_set_label_text_if_changed(s_menu_notice, "");
    const player_command_t command = {
        .kind = PLAYER_COMMAND_SELECT_SOURCE,
        .source = selected_source,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (s_usb_unavailable) {
        // No source to select, so drive the screen directly.
        s_player_ui.source = AUDIO_SOURCE_USB;
        ui_show_station_list();
        return;
    }
    if (selected_source == AUDIO_SOURCE_USB) {
        s_usb_list_open_revision = player_control_usb_listing_revision();
        s_usb_list_open_requested = true;
    }
    if (!ui_submit_player_command(&command)) return;
    // Both sources open on their list rather than the player: there is nothing
    // to look at on the player screen until something has been chosen.
    ui_show_station_list();
}

static void ui_load_menu_screen(void)
{
    s_waiting_for_radio_station = false;
    if (s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED && s_feed_screen != NULL) {
        ui_update_feed_screen();
        lv_screen_load(s_feed_screen);
        return;
    }
    ui_update_menu_highlight();
    lv_screen_load(s_menu_screen);
}

static void ui_show_menu(void)
{
    s_usb_unavailable = false;
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
    if (s_usb_unavailable) {
        // Nothing to list and nothing to scroll: the screen exists only to say
        // why, and to be left with F2 or a long press.
        ui_set_label_text_if_changed(s_station_list_title, "USB");
        ui_set_label_text_if_changed(
            s_station_list_notice,
            ui_usb_notice(s_last_usb_media, s_last_usb_entry_count));
        s_usb_browser_has_parent_row = false;
        station_list_init(&s_station_list, 0U, 0U, PLAYER_ITEM_NONE);
        ui_update_station_list();
        return;
    }
    ui_set_label_text_if_changed(s_station_list_notice, "");
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

static void ui_toggle_playback(void)
{
    const player_command_t command = {
        .kind = PLAYER_COMMAND_TOGGLE,
        .source = AUDIO_SOURCE_NONE,
        .item_index = PLAYER_ITEM_NONE,
    };
    (void)ui_submit_player_command(&command);
}

static void ui_handle_input(board_input_action_t action)
{
    // Settings sits outside the player's view state: it shows no source and
    // starts nothing, so making it a fourth view would put an entry in every
    // transition table for no gain. Any of the three ways out returns to the
    // main screen.
    if (s_settings_open) {
        if (action == BOARD_INPUT_ACTION_F2 || action == BOARD_INPUT_ACTION_ENCODER_LONG) {
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
        if (action == BOARD_INPUT_ACTION_F2 ||
            action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            // Same gesture as on the player screen: hold to leave and stop.
            // Without it the list was a dead end for anyone using the encoder
            // alone, since a short press there selects rather than exits.
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
        if (action == BOARD_INPUT_ACTION_F2 ||
            action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            // Leaving the screen entirely, so a click waiting out its window
            // must not land on the home screen.
            ui_click_gesture_cancel(&s_player_click);
            ui_show_menu();
        } else if (has_list && action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            // Double click opens the list and leaves playback alone; the
            // single click that toggles play/pause is delivered later, from
            // the poll loop, once no second press has arrived.
            if (ui_click_gesture_press(&s_player_click, ui_tick_get_ms()) ==
                UI_CLICK_DOUBLE) {
                ui_show_station_list();
            }
        } else if (has_list && action == BOARD_INPUT_ACTION_F3) {
            ui_toggle_playback();
        }
        return;
    }
    if (s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED &&
        lv_screen_active() == s_feed_screen) {
        if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
            action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            /*
             * The encoder's physical clockwise direction is opposite to the
             * visual feed order on this board.  Keep the common menu semantics
             * unchanged and reverse movement only for the horizontal feed.
             */
            ui_feed_model_move(&s_feed_model,
                               action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? -1 : 1);
            ui_update_feed_screen();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const ui_feed_item_t item = ui_feed_model_selected(&s_feed_model);
            if (item == UI_FEED_SETTINGS) {
                ui_show_settings();
            } else {
                audio_source_t source = AUDIO_SOURCE_NONE;
                if (!ui_feed_model_activate(item, &source)) {
                    ui_set_label_text_if_changed(s_feed_notice, "Функция пока недоступна");
                    return;
                }
                ui_set_label_text_if_changed(s_feed_notice, "");
                (void)ui_menu_select_source(&s_menu, source);
                ui_show_source();
            }
        } else if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
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

/* Keeps the resume point current. Written from the poll loop rather than at
 * the moment of selection because this is the one place that sees both the
 * active source and the file the USB player actually opened; the setters skip
 * a write when nothing changed, so this does not hammer the flash. */
static void ui_remember_playing(const player_snapshot_t *snapshot)
{
    if (!s_device_settings.autoplay) return;
    switch (snapshot->active_source) {
    case AUDIO_SOURCE_INTERNET_RADIO:
        (void)device_settings_set_last_source(&s_device_settings,
                                              DEVICE_LAST_SOURCE_INTERNET_RADIO);
        return;
    case AUDIO_SOURCE_USB: {
        (void)device_settings_set_last_source(&s_device_settings, DEVICE_LAST_SOURCE_USB);
        // context is the directory, stream_title the file the player opened.
        // Only remember a track once one is actually playing, or leaving the
        // browser would erase the previous resume point.
        if (snapshot->stream_title[0] == '\0') return;
        char path[USB_BROWSER_PATH_MAX_LEN];
        if (usb_browser_path_child(snapshot->context, snapshot->stream_title, path,
                                   sizeof(path))) {
            (void)device_settings_set_last_usb_file(&s_device_settings, path);
        }
        return;
    }
    default:
        return;
    }
}

static void ui_sync_player_snapshot(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    s_last_usb_media = snapshot->usb_media;
    s_last_usb_entry_count = snapshot->usb_entry_count;
    ui_remember_playing(snapshot);
    if (s_usb_unavailable && ui_usb_can_open(s_last_usb_media, s_last_usb_entry_count)) {
        // A drive appeared while its "unavailable" screen was open: pick the
        // source up rather than making the user back out and re-enter.
        s_usb_unavailable = false;
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = AUDIO_SOURCE_USB,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (ui_submit_player_command(&select)) {
            s_usb_list_open_revision = player_control_usb_listing_revision();
            s_usb_list_open_requested = true;
        }
    }
    ui_player_state_apply_snapshot(&s_player_ui, snapshot, ui_tick_get_ms());
    if (old_view != ui_player_state_view(&s_player_ui) ||
        old_source != ui_player_state_source(&s_player_ui)) {
        // The player screen went away on its own - a station finished
        // connecting, the source stopped - so a click waiting on it is stale.
        ui_click_gesture_cancel(&s_player_click);
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

static void ui_autoplay_step(const player_snapshot_t *snapshot)
{
    if (!s_autoplay_pending) return;
    const ui_autoplay_action_t action =
        ui_autoplay_decide(&s_device_settings, snapshot->usb_media, false);
    const bool waited =
        (uint32_t)(ui_tick_get_ms() - s_autoplay_started_ms) >= UI_AUTOPLAY_USB_WAIT_MS;
    // Hold off only while the answer could still change: a drive that has not
    // shown up yet may still mount.
    if (action == UI_AUTOPLAY_USB_UNAVAILABLE && !waited) return;
    s_autoplay_pending = false;

    switch (action) {
    case UI_AUTOPLAY_RADIO:
        (void)ui_menu_select_source(&s_menu, AUDIO_SOURCE_INTERNET_RADIO);
        ui_show_source();
        return;
    case UI_AUTOPLAY_USB_UNAVAILABLE:
        (void)ui_menu_select_source(&s_menu, AUDIO_SOURCE_USB);
        ui_show_source();
        return;
    case UI_AUTOPLAY_USB_FILE:
    case UI_AUTOPLAY_USB_BROWSER: {
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = AUDIO_SOURCE_USB,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (!ui_submit_player_command(&select)) return;
        // Whether the remembered file is still there is only knowable by
        // looking, so the attempt itself is the test: it opens the file's
        // directory either way, leaving the browser somewhere useful.
        if (player_control_usb_resume_path(s_device_settings.last_usb_file)) {
            ui_load_source_screen(AUDIO_SOURCE_USB);
            return;
        }
        s_usb_list_open_revision = player_control_usb_listing_revision();
        s_usb_list_open_requested = true;
        ui_show_station_list();
        return;
    }
    case UI_AUTOPLAY_HOME:
    default:
        return;
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        board_input_action_t action;
        if (xQueueReceive(s_input_queue, &action, pdMS_TO_TICKS(10)) == pdTRUE) {
            ui_handle_input(action);
        }
        // A click held for the double-click window is delivered here, since
        // nothing else runs while the UI waits for the second press.
        if (ui_click_gesture_poll(&s_player_click, ui_tick_get_ms()) == UI_CLICK_SINGLE) {
            ui_toggle_playback();
        }
        player_snapshot_t snapshot;
        player_control_get_snapshot(&snapshot);
        ui_autoplay_step(&snapshot);
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
    ui_click_gesture_init(&s_player_click);
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
    ui_create_feed_screen();
    ui_create_settings_screen();
    ui_create_source_screen();
    ui_create_station_list_screen();
    ui_settings_model_init(&s_settings_model);
    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
    } else {
        (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                         s_device_settings.flip_horizontal);
    }
    s_autoplay_pending = ui_autoplay_decide(&s_device_settings,
                                            USB_BROWSER_MEDIA_READY, true) !=
                         UI_AUTOPLAY_HOME;
    s_autoplay_started_ms = ui_tick_get_ms();
    if (s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED) {
        lv_screen_load(s_feed_screen);
    } else {
        lv_screen_load(s_menu_screen);
    }

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
