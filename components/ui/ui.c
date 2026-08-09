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
#include "board_config.h"
#include "player_control.h"
#include "ui_draw_buffer.h"
#include "ui_font_cyrillic_14.h"
#include "ui_menu.h"
#include "ui_player_state.h"
#include "ui_radio_text.h"
#include "ui_station_list.h"

#define UI_DRAW_BUFFER_LINES 20
#define UI_DRAW_BUFFER_SIZE ui_rgb565_draw_buffer_size(BOARD_TFT_WIDTH, UI_DRAW_BUFFER_LINES)
#define UI_INPUT_QUEUE_LENGTH 16
#define UI_TASK_STACK_SIZE 6144
#define UI_TASK_PRIORITY 4
#define UI_STATION_LIST_MAX_ROWS 7U
#define UI_RADIO_EMPTY_LIST_DELAY_MS 250U

static const char *TAG = "ui";
static QueueHandle_t s_input_queue;
static ui_menu_state_t s_menu;
static lv_display_t *s_display;
static lv_obj_t *s_menu_screen;
static lv_obj_t *s_source_screen;
static lv_obj_t *s_menu_rows[UI_MENU_ITEM_COUNT];
static lv_obj_t *s_source_title;
static lv_obj_t *s_source_status;
static lv_obj_t *s_source_detail;
static lv_obj_t *s_source_stream;
static lv_obj_t *s_source_wifi;
static lv_obj_t *s_station_list_screen;
static lv_obj_t *s_station_list_rows[UI_STATION_LIST_MAX_ROWS];
static station_list_state_t s_station_list;
static ui_player_state_t s_player_ui;
static bool s_waiting_for_radio_station;
static uint32_t s_radio_station_wait_started_ms;

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

static void ui_update_radio_status(const player_snapshot_t *snapshot)
{
    if (ui_player_state_source(&s_player_ui) != AUDIO_SOURCE_INTERNET_RADIO ||
        snapshot == NULL) return;
    ui_set_label_text_if_changed(s_source_status,
                                 ui_radio_state_text(snapshot->playback_state));
    const station_catalog_entry_t *entry =
        player_control_station_at(snapshot->active_item_index);
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
                         snapshot->bitrate_kbps);
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

    lv_obj_t *title = lv_label_create(s_menu_screen);
    lv_label_set_text(title, "jradio  |  Sources");
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

    lv_obj_t *footer = lv_label_create(s_menu_screen);
    lv_label_set_text(footer, "F1 Menu  F2 Back  F3 Play  F4 Options");
    lv_obj_set_pos(footer, 8, 220);
    lv_obj_set_style_text_color(footer, lv_color_hex(0xB0BEC5), 0);
    ui_update_menu_highlight();
}

static void ui_update_station_list(void)
{
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    const size_t count = snapshot.item_count;
    const size_t window_start = station_list_window_start(&s_station_list,
                                                          UI_STATION_LIST_MAX_ROWS);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        const size_t entry_index = window_start + row;
        if (entry_index >= count) {
            lv_obj_add_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
        const station_catalog_entry_t *entry = player_control_station_at(entry_index);
        const bool selected = entry_index == station_list_selected_index(&s_station_list);
        const bool active = entry_index == station_list_active_index(&s_station_list);
        lv_obj_set_style_bg_color(s_station_list_rows[row], lv_color_hex(selected ? 0x1769AA : 0x101820), 0);
        lv_obj_set_style_bg_opa(s_station_list_rows[row], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_station_list_rows[row], active ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_station_list_rows[row], lv_color_hex(0xFFD54F), 0);
        lv_obj_set_style_border_opa(s_station_list_rows[row], LV_OPA_COVER, 0);
        lv_label_set_text_fmt(s_station_list_rows[row], "%c %s", selected ? '>' : ' ',
                              entry == NULL ? "" : entry->name);
    }
}

static void ui_create_station_list_screen(void)
{
    s_station_list_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_station_list_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_station_list_screen, 0, 0);
    lv_obj_set_style_pad_all(s_station_list_screen, 0, 0);
    lv_obj_t *title = lv_label_create(s_station_list_screen);
    lv_label_set_text(title, "Internet radio | Stations");
    lv_obj_set_pos(title, 12, 8);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        s_station_list_rows[row] = lv_label_create(s_station_list_screen);
        lv_obj_set_pos(s_station_list_rows[row], 10, 34 + (int)row * 26);
        lv_obj_set_size(s_station_list_rows[row], 300, 24);
        lv_obj_set_style_pad_left(s_station_list_rows[row], 6, 0);
        lv_obj_set_style_pad_top(s_station_list_rows[row], 2, 0);
        lv_obj_set_style_radius(s_station_list_rows[row], 3, 0);
        lv_obj_set_style_text_font(s_station_list_rows[row], &ui_font_cyrillic_14, 0);
        lv_obj_set_style_text_color(s_station_list_rows[row], lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_t *footer = lv_label_create(s_station_list_screen);
    lv_label_set_text(footer, "Encoder Select  F2 Back");
    lv_obj_set_pos(footer, 8, 220);
    lv_obj_set_style_text_color(footer, lv_color_hex(0xB0BEC5), 0);
}

static void ui_show_station_list(void);

static void ui_create_source_screen(void)
{
    s_source_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_source_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_source_screen, 0, 0);
    lv_obj_set_style_pad_all(s_source_screen, 0, 0);

    s_source_title = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_title, 14, 32);
    lv_obj_set_style_text_font(s_source_title, &ui_font_cyrillic_14, 0);
    lv_obj_set_style_text_color(s_source_title, lv_color_hex(0xFFFFFF), 0);

    s_source_status = lv_label_create(s_source_screen);
    lv_label_set_text(s_source_status, "Not implemented");
    lv_obj_set_pos(s_source_status, 14, 64);
    lv_obj_set_style_text_color(s_source_status, lv_color_hex(0xB0BEC5), 0);

    s_source_detail = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_detail, 14, 92);
    lv_obj_set_width(s_source_detail, 290);
    lv_obj_set_height(s_source_detail, 52);
    lv_label_set_long_mode(s_source_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_source_detail, &ui_font_cyrillic_14, 0);
    lv_obj_set_style_text_color(s_source_detail, lv_color_hex(0xFFFFFF), 0);

    s_source_stream = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_stream, 14, 154);
    lv_obj_set_style_text_color(s_source_stream, lv_color_hex(0xB0BEC5), 0);

    s_source_wifi = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_wifi, 14, 180);
    lv_obj_set_style_text_color(s_source_wifi, lv_color_hex(0xB0BEC5), 0);

    lv_obj_t *footer = lv_label_create(s_source_screen);
    lv_label_set_text(footer, "Encoder Stations  F2 Back  F3 Play/Pause");
    lv_obj_set_pos(footer, 8, 220);
    lv_obj_set_style_text_color(footer, lv_color_hex(0xB0BEC5), 0);
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
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    const player_command_t command = {
        .kind = PLAYER_COMMAND_SELECT_SOURCE,
        .source = selected_source,
        .item_index = PLAYER_ITEM_NONE,
    };
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

static void ui_load_station_list_screen(void)
{
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "show station list: player_state=%d active_station=%u",
             (int)snapshot.playback_state, (unsigned int)snapshot.active_item_index);
    s_waiting_for_radio_station = false;
    const size_t station_count = snapshot.item_count;
    const size_t active_station_index = snapshot.active_item_index;
    const size_t initial_index = active_station_index < station_count ? active_station_index : 0U;
    station_list_init(&s_station_list, station_count, initial_index,
                      active_station_index);
    ui_update_station_list();
    lv_screen_load(s_station_list_screen);
}

static void ui_show_station_list(void)
{
    if (!ui_player_state_show_station_list(&s_player_ui)) return;
    s_waiting_for_radio_station = false;
    ui_load_station_list_screen();
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
    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_STATION_LIST) {
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            if (station_list_handle_input(&s_station_list, action)) ui_update_station_list();
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
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        } else if (ui_player_state_source(&s_player_ui) == AUDIO_SOURCE_INTERNET_RADIO &&
                   action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            ui_show_station_list();
        } else if (ui_player_state_source(&s_player_ui) == AUDIO_SOURCE_INTERNET_RADIO &&
                   action == BOARD_INPUT_ACTION_F3) {
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
        ui_update_menu_highlight();
    } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
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

    if (s_waiting_for_radio_station &&
        snapshot->active_source == AUDIO_SOURCE_INTERNET_RADIO &&
        snapshot->active_item_index == PLAYER_ITEM_NONE &&
        snapshot->playback_state == PLAYER_PLAYBACK_STOPPED &&
        (uint32_t)(ui_tick_get_ms() - s_radio_station_wait_started_ms) >=
            UI_RADIO_EMPTY_LIST_DELAY_MS) {
        ui_show_station_list();
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
        ui_sync_player_snapshot(&snapshot);
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

    lv_color_t *buffer1 = heap_caps_malloc(UI_DRAW_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buffer2 = heap_caps_malloc(UI_DRAW_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer1 == NULL || buffer2 == NULL) {
        ESP_LOGE(TAG, "draw buffer allocation failed: buffer1=%p buffer2=%p dma_largest=%u",
                 buffer1, buffer2,
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(BOARD_TFT_WIDTH, BOARD_TFT_HEIGHT);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "LVGL display allocation failed: internal_largest=%u",
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, buffer1, buffer2, UI_DRAW_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, ui_flush);
    ui_create_menu_screen();
    ui_create_source_screen();
    ui_create_station_list_screen();
    lv_screen_load(s_menu_screen);

    if (xTaskCreate(ui_task, "ui", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "UI task allocation failed: internal_free=%u internal_largest=%u",
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        lv_display_delete(s_display);
        s_display = NULL;
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
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
