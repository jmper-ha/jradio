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
#include "ui_font_cyrillic_20.h"
#include "ui_menu.h"
#include "ui_feed_icons.h"
#include "ui_feed_model.h"
#include "ui_player_state.h"
#include "ui_radio_text.h"
#include "ui_settings_model.h"
#include "ui_station_list.h"
#include "ui_status_bar.h"

#include "audio_volume.h"
#include "device_clock.h"
#include "ui_usb_notice.h"
#include "ui_vu_meter.h"

#define UI_DRAW_BUFFER_LINES 20
#define UI_DRAW_BUFFER_SIZE ui_rgb565_draw_buffer_size(TFT_WIDTH, UI_DRAW_BUFFER_LINES)
#define UI_INPUT_QUEUE_LENGTH 16
#define UI_TASK_STACK_SIZE 6144
#define UI_TASK_PRIORITY 4
/* Six rows, not seven: the bottom strip belongs to the scroll bar. */
#define UI_STATION_LIST_MAX_ROWS 6U
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
static lv_obj_t *s_source_buffer;
static lv_obj_t *s_source_artist;
static lv_obj_t *s_source_clock;
static lv_obj_t *s_source_art;
static lv_obj_t *s_source_volume;
static lv_obj_t *s_source_volume_bar;
/* Four rising bars. Drawn rather than taken from a font: LVGL's symbol set has
 * one Wi-Fi glyph with no strength in it, and the strength is the half worth
 * showing. */
/* One encoder click. Five percent is 2 dB on this curve - a step you can hear
 * without hunting, and 20 clicks from silence to full. */
#define UI_VOLUME_STEP_PERCENT 5
/* How long the knob has to be still before the setting is written to flash.
 * Long enough to cover a whole gesture, short enough that a power cut right
 * after adjusting is unlikely to lose it. */
#define UI_VOLUME_SETTLE_MS 1500U
#define UI_WIFI_BARS 4
static lv_obj_t *s_source_wifi_bars[UI_WIFI_BARS];
/* 20 blocks per channel: 20*11 + 19*3 = 277 px starting at x=30, so the row
 * ends at 307 on a 320 px panel. */
#define UI_VU_SEGMENTS 20U
#define UI_VU_SEGMENT_W 11
#define UI_VU_SEGMENT_GAP 3
static lv_obj_t *s_source_vu[2][UI_VU_SEGMENTS];
static ui_vu_meter_t s_vu_state[2];
static uint32_t s_vu_updated_ms;
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
static lv_obj_t *s_station_list_progress;
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
/* Set when the volume has moved but not yet been saved; see
 * ui_volume_commit_due() for why the two are separate. */
static bool s_volume_save_pending;
static uint32_t s_volume_changed_ms;

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

/* The status strip and the footer are the same for both sources, so they are
 * filled from one place rather than duplicated per source. */
static void ui_update_status_strip(const player_snapshot_t *snapshot)
{
    int hour = 0;
    int minute = 0;
    const bool have_time = device_clock_now(&hour, &minute);
    char clock_text[8];
    ui_status_clock_text(clock_text, sizeof(clock_text), have_time, hour, minute);
    ui_set_label_text_if_changed(s_source_clock, clock_text);

    const uint8_t bars = ui_status_wifi_bars(snapshot->wifi_rssi_valid,
                                             snapshot->wifi_rssi_dbm);
    for (int bar = 0; bar < UI_WIFI_BARS; ++bar) {
        // Unlit bars stay visible in a dim shade rather than hiding, so the
        // shape reads as a scale at rest instead of a missing icon.
        lv_obj_set_style_bg_color(s_source_wifi_bars[bar],
                                  lv_color_hex(bar < (int)bars ? 0xB0BEC5 : 0x2D3F4D), 0);
    }
    char wifi_text[8];
    if (snapshot->wifi_rssi_valid) {
        snprintf(wifi_text, sizeof(wifi_text), "%d", (int)snapshot->wifi_rssi_dbm);
    } else {
        snprintf(wifi_text, sizeof(wifi_text), "--");
    }
    ui_set_label_text_if_changed(s_source_wifi, wifi_text);
}

/* Runs every poll rather than only on a snapshot change: both readings here
 * come straight from the owning subsystem and move far too often to belong in
 * a structure the web diffs against. */
static void ui_update_footer(void)
{
    if (lv_screen_active() != s_source_screen) return;
    uint8_t fill = 0U;
    char buffer_text[20];
    if (player_control_input_fill(&fill)) {
        snprintf(buffer_text, sizeof(buffer_text), "Буфер %u%%", (unsigned int)fill);
    } else {
        // Nothing playing, or a source with no backlog at all. A dash says
        // that; a zero would claim the buffer had run dry.
        snprintf(buffer_text, sizeof(buffer_text), "Буфер --");
    }
    ui_set_label_text_if_changed(s_source_buffer, buffer_text);

    const uint8_t volume = board_audio_volume();
    char volume_text[8];
    snprintf(volume_text, sizeof(volume_text), "%u", (unsigned int)volume);
    ui_set_label_text_if_changed(s_source_volume, volume_text);
    lv_obj_t *fill_bar = lv_obj_get_child(s_source_volume_bar, 0);
    if (fill_bar != NULL) {
        const int32_t width = (int32_t)((60U * volume) / 100U);
        lv_obj_set_width(fill_bar, width < 1 ? 1 : width);
    }
}

static void ui_update_usb_status(const player_snapshot_t *snapshot)
{
    // The directory takes the place the radio gives the station name.
    ui_set_label_text_if_changed(s_source_title, snapshot->context);
    ui_set_label_text_if_changed(s_source_detail, snapshot->stream_title);
    // No performer to show for a file; the state line takes the row instead.
    ui_set_label_text_if_changed(s_source_artist, "");
    ui_set_label_text_if_changed(s_source_status,
                                 snapshot->playback_state == PLAYER_PLAYBACK_STOPPED
                                     ? "Выберите файл"
                                     : "");
    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), snapshot->codec,
                         snapshot->bitrate_kbps, snapshot->sample_rate_hz);
    ui_set_label_text_if_changed(s_source_stream, stream_text);
}

static void ui_update_radio_status(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    ui_update_status_strip(snapshot);
    if (ui_player_state_source(&s_player_ui) == AUDIO_SOURCE_USB) {
        ui_update_usb_status(snapshot);
        return;
    }
    if (ui_player_state_source(&s_player_ui) != AUDIO_SOURCE_INTERNET_RADIO) return;

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
    if (entry != NULL) {
        ui_set_label_text_if_changed(s_source_title, entry->name);
    }

    const char *icy = (entry != NULL && entry->flag == 0)
                          ? entry->name
                          : (snapshot->stream_title[0] == '\0' ? snapshot->context
                                                               : snapshot->stream_title);
    // Stations send one string; splitting it gives the track its own wide line
    // instead of burying it in the middle of a run-on.
    char artist[PLAYER_TITLE_MAX_LEN];
    char track[PLAYER_TITLE_MAX_LEN];
    (void)ui_radio_split_title(icy, artist, sizeof(artist), track, sizeof(track));
    ui_set_label_text_if_changed(s_source_detail, track);
    ui_set_label_text_if_changed(s_source_artist, artist);

    // The performer row carries the state instead whenever there is no
    // performer to show - which is exactly when the state matters.
    const bool playing = snapshot->playback_state == PLAYER_PLAYBACK_PLAYING;
    ui_set_label_text_if_changed(s_source_status,
                                 playing ? "" : ui_radio_state_text(snapshot->playback_state));

    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), snapshot->codec,
                         snapshot->bitrate_kbps, snapshot->sample_rate_hz);
    ui_set_label_text_if_changed(s_source_stream, stream_text);
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
        // Ordinal, so the first station reads 001 rather than 000.
        snprintf(text, text_size, "%03u %s", (unsigned)(list_index + 1U),
                 entry == NULL ? "" : entry->name);
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
    // you what the encoder click will do before you press it. The glyph comes
    // from Montserrat through the Cyrillic font's fallback, so it can sit
    // inline here instead of needing a label of its own.
    if (entry.kind == USB_BROWSER_ENTRY_DIRECTORY) {
        snprintf(text, text_size, LV_SYMBOL_DIRECTORY " %s", entry.name);
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
        // Outline, not border: a border is drawn inside and takes 2 px off each
        // edge, which left 18 px of content for a 19 px line - so the playing
        // row overflowed vertically and LV_LABEL_LONG_MODE_SCROLL rolled it
        // up and down, even for names that fitted across. An outline is drawn
        // outside the object and costs the content nothing.
        lv_obj_set_style_outline_width(s_station_list_rows[row], active ? 2 : 0, 0);
        lv_obj_set_style_outline_color(s_station_list_rows[row], lv_color_hex(0xFFD54F), 0);
        lv_obj_set_style_outline_opa(s_station_list_rows[row], LV_OPA_COVER, 0);
        // Hug the row: the 2 px gap between rows is exactly where it goes.
        lv_obj_set_style_outline_pad(s_station_list_rows[row], 0, 0);
        // Only the row under the cursor scrolls: a screen of six marquees at
        // once is unreadable, and the row being pointed at is the one whose
        // full name the user is actually after. The others keep the ellipsis.
        //
        // Set before the text, since changing either restarts the animation -
        // which is why this function is only called when something really
        // changed, never from the poll loop.
        lv_label_set_long_mode(s_station_list_rows[row],
                               selected ? LV_LABEL_LONG_MODE_SCROLL
                                        : LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text_fmt(s_station_list_rows[row], "%c %s", selected ? '>' : ' ', text);
    }
    // Hidden when there is nothing to scroll through at all - on the "no
    // drive" screen a full bar under an empty list would be nonsense.
    if (s_station_list.count == 0U) {
        lv_obj_add_flag(s_station_list_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_station_list_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_station_list_progress,
                         station_list_progress_percent(&s_station_list), LV_ANIM_OFF);
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
    s_station_list_progress = lv_bar_create(s_station_list_screen);
    lv_obj_set_pos(s_station_list_progress, 12, 200);
    lv_obj_set_size(s_station_list_progress, 296, 8);
    lv_bar_set_range(s_station_list_progress, 0, 100);
    lv_obj_set_style_bg_color(s_station_list_progress, lv_color_hex(0x263746), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_station_list_progress, lv_color_hex(0x1769AA),
                              LV_PART_INDICATOR);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        s_station_list_rows[row] = lv_label_create(s_station_list_screen);
        lv_obj_set_pos(s_station_list_rows[row], 10, 34 + (int)row * 26);
        lv_obj_set_size(s_station_list_rows[row], 300, 24);
        lv_obj_set_style_pad_left(s_station_list_rows[row], 6, 0);
        lv_obj_set_style_pad_top(s_station_list_rows[row], 2, 0);
        lv_obj_set_style_radius(s_station_list_rows[row], 3, 0);
        lv_obj_set_style_text_color(s_station_list_rows[row], lv_color_hex(0xFFFFFF), 0);
        lv_label_set_long_mode(s_station_list_rows[row], LV_LABEL_LONG_MODE_DOTS);
    }
}

static void ui_show_station_list(void);

/* Player screen layout, in device pixels.
 *
 * 320x240 leaves no room for guessing, so the numbers live together here
 * rather than scattered through the builder. A status strip carries what is
 * not about the music - clock, signal - and everything below it is the
 * playing item. The bottom row pairs the two things that answer "is it going
 * to keep playing, and how loud": buffer on the left, volume on the right.
 */
#define UI_SRC_STATUS_H 26
#define UI_SRC_ART_X 10
#define UI_SRC_ART_Y 36
#define UI_SRC_ART_SIZE 96
#define UI_SRC_TEXT_X 118
#define UI_SRC_TEXT_W 192
/* One line of each face, from the generated fonts' own line_height. Rows are
 * spaced by these rather than by eye, so nothing can overlap its neighbour. */
#define UI_SRC_LINE_H 19
#define UI_SRC_TRACK_H 23
#define UI_SRC_ROW_TRACK 58
#define UI_SRC_ROW_ARTIST 86
#define UI_SRC_ROW_STREAM 108
#define UI_SRC_RULE_TOP 144
#define UI_SRC_VU_Y 155
#define UI_SRC_RULE_BOTTOM 200
#define UI_SRC_FOOT_Y 210

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

    lv_obj_t *status = lv_obj_create(s_source_screen);
    lv_obj_set_pos(status, 0, 0);
    lv_obj_set_size(status, 320, UI_SRC_STATUS_H);
    lv_obj_set_style_bg_color(status, lv_color_hex(0x16222C), 0);
    lv_obj_set_style_border_width(status, 0, 0);
    lv_obj_set_style_radius(status, 0, 0);
    lv_obj_set_style_pad_all(status, 0, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    // Centred rather than left-aligned: it is the one thing on the screen read
    // from across the room, and the middle is where the eye goes first.
    s_source_clock = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_clock, 130, 5);
    lv_obj_set_width(s_source_clock, 60);
    lv_obj_set_style_text_align(s_source_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_source_clock, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_source_clock, "");

    for (int bar = 0; bar < UI_WIFI_BARS; ++bar) {
        lv_obj_t *block = lv_obj_create(s_source_screen);
        const int height = 3 + bar * 3;
        lv_obj_set_size(block, 3, height);
        // Grown from a common baseline so the four read as one rising shape.
        lv_obj_set_pos(block, 252 + bar * 5, 7 + (12 - height));
        lv_obj_set_style_border_width(block, 0, 0);
        lv_obj_set_style_radius(block, 1, 0);
        lv_obj_set_style_pad_all(block, 0, 0);
        lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
        s_source_wifi_bars[bar] = block;
    }
    s_source_wifi = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_wifi, 275, 6);
    lv_obj_set_style_text_color(s_source_wifi, lv_color_hex(0xB0BEC5), 0);
    lv_label_set_text(s_source_wifi, "");

    // Stands in for the cover art that is not implemented yet. A symbol on a
    // tile keeps the composition; an empty square would read as a fault.
    s_source_art = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_art, UI_SRC_ART_X, UI_SRC_ART_Y);
    lv_obj_set_size(s_source_art, UI_SRC_ART_SIZE, UI_SRC_ART_SIZE);
    lv_obj_set_style_bg_color(s_source_art, lv_color_hex(0x18242E), 0);
    lv_obj_set_style_bg_opa(s_source_art, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_source_art, lv_color_hex(0x26343F), 0);
    lv_obj_set_style_border_width(s_source_art, 1, 0);
    lv_obj_set_style_radius(s_source_art, 3, 0);
    lv_obj_set_style_text_color(s_source_art, lv_color_hex(0x3E5060), 0);
    lv_obj_set_style_text_font(s_source_art, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_source_art, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_source_art, 24, 0);
    lv_label_set_text(s_source_art, LV_SYMBOL_AUDIO);

    // Every one of these gets an explicit height of exactly one line. Without
    // it LV_LABEL_LONG_DOT wraps to a second line before it considers
    // shortening, and a long station name grew downwards over the codec row.
    s_source_title = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_title, UI_SRC_TEXT_X, UI_SRC_ART_Y);
    lv_obj_set_size(s_source_title, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_title, lv_color_hex(0xF2A33C), 0);

    s_source_detail = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_detail, UI_SRC_TEXT_X, UI_SRC_ROW_TRACK);
    lv_obj_set_size(s_source_detail, UI_SRC_TEXT_W, UI_SRC_TRACK_H);
    lv_obj_set_style_text_font(s_source_detail, &ui_font_cyrillic_20, 0);
    lv_label_set_long_mode(s_source_detail, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_color(s_source_detail, lv_color_hex(0xFFFFFF), 0);

    s_source_artist = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_artist, UI_SRC_TEXT_X, UI_SRC_ROW_ARTIST);
    lv_obj_set_size(s_source_artist, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_artist, lv_color_hex(0xB0BEC5), 0);

    // Shares the performer row rather than getting one of its own: the two are
    // never both set, and a separate row would have to come out of the codec
    // line - which is where it used to land, on top of it.
    s_source_status = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_status, UI_SRC_TEXT_X, UI_SRC_ROW_ARTIST);
    lv_obj_set_size(s_source_status, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_status, lv_color_hex(0x78909C), 0);

    s_source_stream = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_stream, UI_SRC_TEXT_X, UI_SRC_ROW_STREAM);
    lv_obj_set_size(s_source_stream, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_stream, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_stream, lv_color_hex(0x78909C), 0);

    lv_obj_t *rule_top = lv_obj_create(s_source_screen);
    lv_obj_set_pos(rule_top, 10, UI_SRC_RULE_TOP);
    lv_obj_set_size(rule_top, 300, 1);
    lv_obj_set_style_bg_color(rule_top, lv_color_hex(0x23303C), 0);
    lv_obj_set_style_border_width(rule_top, 0, 0);
    lv_obj_set_style_pad_all(rule_top, 0, 0);
    lv_obj_clear_flag(rule_top, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t channel = 0; channel < 2U; ++channel) {
        for (size_t segment = 0; segment < UI_VU_SEGMENTS; ++segment) {
            lv_obj_t *block = lv_obj_create(s_source_screen);
            lv_obj_set_size(block, UI_VU_SEGMENT_W, 10);
            lv_obj_set_pos(block,
                           30 + (int)segment * (UI_VU_SEGMENT_W + UI_VU_SEGMENT_GAP),
                           UI_SRC_VU_Y + (int)channel * 18);
            lv_obj_set_style_border_width(block, 0, 0);
            lv_obj_set_style_radius(block, 1, 0);
            lv_obj_set_style_pad_all(block, 0, 0);
            // Scrolling is on by default for lv_obj and would let these tiny
            // blocks take the encoder's input away from the screen.
            lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
            s_source_vu[channel][segment] = block;
        }
        lv_obj_t *mark = lv_label_create(s_source_screen);
        lv_label_set_text(mark, channel == 0U ? "L" : "R");
        lv_obj_set_pos(mark, 12, UI_SRC_VU_Y - 3 + (int)channel * 18);
        lv_obj_set_style_text_color(mark, lv_color_hex(0x78909C), 0);
    }

    lv_obj_t *rule_bottom = lv_obj_create(s_source_screen);
    lv_obj_set_pos(rule_bottom, 10, UI_SRC_RULE_BOTTOM);
    lv_obj_set_size(rule_bottom, 300, 1);
    lv_obj_set_style_bg_color(rule_bottom, lv_color_hex(0x23303C), 0);
    lv_obj_set_style_border_width(rule_bottom, 0, 0);
    lv_obj_set_style_pad_all(rule_bottom, 0, 0);
    lv_obj_clear_flag(rule_bottom, LV_OBJ_FLAG_SCROLLABLE);

    s_source_buffer = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_buffer, 10, UI_SRC_FOOT_Y);
    lv_obj_set_style_text_color(s_source_buffer, lv_color_hex(0x78909C), 0);

    s_source_volume_bar = lv_obj_create(s_source_screen);
    lv_obj_set_pos(s_source_volume_bar, 200, UI_SRC_FOOT_Y + 5);
    lv_obj_set_size(s_source_volume_bar, 60, 8);
    lv_obj_set_style_bg_color(s_source_volume_bar, lv_color_hex(0x23303C), 0);
    lv_obj_set_style_border_width(s_source_volume_bar, 0, 0);
    lv_obj_set_style_radius(s_source_volume_bar, 2, 0);
    lv_obj_set_style_pad_all(s_source_volume_bar, 0, 0);
    lv_obj_clear_flag(s_source_volume_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *fill = lv_obj_create(s_source_volume_bar);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, 60, 8);
    lv_obj_set_style_bg_color(fill, lv_color_hex(0xB0BEC5), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);

    s_source_volume = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_volume, 266, UI_SRC_FOOT_Y);
    lv_obj_set_style_text_color(s_source_volume, lv_color_hex(0xB0BEC5), 0);
    lv_label_set_text(s_source_volume, "");
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
        ui_set_label_text_if_changed(s_source_artist, "");
        s_waiting_for_radio_station = true;
        s_radio_station_wait_started_ms = ui_tick_get_ms();
    } else if (selected_source == AUDIO_SOURCE_USB) {
        s_waiting_for_radio_station = false;
        // Nothing plays until a file is chosen, so this screen opens idle
        // rather than pretending to connect.
        ui_set_label_text_if_changed(s_source_status, "Выберите файл");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        ui_set_label_text_if_changed(s_source_artist, "");
    } else {
        s_waiting_for_radio_station = false;
        ui_set_label_text_if_changed(s_source_status, "Not implemented");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        ui_set_label_text_if_changed(s_source_artist, "");
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
            ui_set_label_text_if_changed(s_source_artist, "");
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
            ui_set_label_text_if_changed(s_source_artist, "");
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
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            // The encoder was unused on this screen, which is why volume gets
            // it: no gesture has to be given up to make room.
            const int step = action == BOARD_INPUT_ACTION_ENCODER_RIGHT
                                 ? UI_VOLUME_STEP_PERCENT
                                 : -UI_VOLUME_STEP_PERCENT;
            const uint8_t volume = audio_volume_step(board_audio_volume(), step);
            // Reaches the audio path at once - it is one atomic store, and the
            // next PCM block is already attenuated. Saving is what has to wait:
            // writing settings.csv per click stalled this task long enough that
            // clicks queued up and then arrived in a burst.
            board_audio_set_volume(volume);
            s_volume_save_pending = true;
            s_volume_changed_ms = ui_tick_get_ms();
            ui_update_footer();
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
    case UI_AUTOPLAY_RADIO: {
        // Not ui_show_source(): that opens the list, which is right when the
        // user is picking something and wrong here - autoplay already knows
        // what plays, so the player screen is the answer.
        (void)ui_menu_select_source(&s_menu, AUDIO_SOURCE_INTERNET_RADIO);
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = AUDIO_SOURCE_INTERNET_RADIO,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (!ui_submit_player_command(&select)) return;
        ui_load_source_screen(AUDIO_SOURCE_INTERNET_RADIO);
        // That screen arms the "no station to play" fallback, which opens the
        // list after a moment. Autoplay has a station, so the only thing that
        // fallback could do here is flip away from the screen just loaded.
        s_waiting_for_radio_station = false;
        return;
    }
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

/* Runs every poll, not only when something changed: the meter is an animation,
 * and its whole job is to keep moving between the ~26 ms PCM blocks. */
static void ui_update_vu(void)
{
    if (lv_screen_active() != s_source_screen) return;
    const uint32_t now = ui_tick_get_ms();
    const uint32_t elapsed = now - s_vu_updated_ms;
    s_vu_updated_ms = now;

    uint16_t peak[2] = {0U, 0U};
    board_audio_level_take(&peak[0], &peak[1]);
    for (size_t channel = 0; channel < 2U; ++channel) {
        const uint8_t target = ui_vu_percent_from_level(peak[channel]);
        const uint8_t value = ui_vu_meter_step(&s_vu_state[channel], target, elapsed);
        const uint8_t lit = ui_vu_lit_segments(value, UI_VU_SEGMENTS);
        for (uint8_t segment = 0U; segment < UI_VU_SEGMENTS; ++segment) {
            // Unlit blocks stay visible in a dim shade rather than hiding, so
            // the meter reads as a scale at rest instead of an empty strip.
            const uint32_t colour =
                segment >= lit ? 0x263746U
                : ui_vu_segment_is_red(segment, UI_VU_SEGMENTS) ? 0xE53935U
                                                                : 0x43A047U;
            lv_obj_set_style_bg_color(s_source_vu[channel][segment],
                                      lv_color_hex(colour), 0);
        }
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
        if (ui_click_gesture_poll(&s_player_click, ui_tick_get_ms()) == UI_CLICK_SINGLE) {
            ui_toggle_playback();
        }
        if (ui_volume_commit_due(s_volume_save_pending, s_volume_changed_ms,
                                 ui_tick_get_ms(), UI_VOLUME_SETTLE_MS)) {
            s_volume_save_pending = false;
            (void)device_settings_set_volume(&s_device_settings, board_audio_volume());
        }
        player_snapshot_t snapshot;
        player_control_get_snapshot(&snapshot);
        ui_autoplay_step(&snapshot);
        ui_update_vu();
        ui_update_footer();
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
    ui_vu_meter_init(&s_vu_state[0]);
    ui_vu_meter_init(&s_vu_state[1]);
    s_vu_updated_ms = ui_tick_get_ms();
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
    // Before anything can play: the board defaults to full volume, and coming
    // back from a power cut at full blast when the user had it at 20 is the
    // kind of surprise a saved setting exists to prevent.
    board_audio_set_volume(s_device_settings.volume);
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
