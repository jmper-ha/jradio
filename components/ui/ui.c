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
#include "internet_radio.h"
#include "ui_deferred_start.h"
#include "ui_font_cyrillic_14.h"
#include "ui_menu.h"
#include "ui_radio_text.h"
#include "ui_station_list.h"
#include "wifi_provisioning.h"

#define UI_DRAW_BUFFER_LINES 20
#define UI_DRAW_BUFFER_SIZE (BOARD_TFT_WIDTH * UI_DRAW_BUFFER_LINES * sizeof(lv_color_t))
#define UI_INPUT_QUEUE_LENGTH 16
#define UI_TASK_STACK_SIZE 6144
#define UI_TASK_PRIORITY 4
#define UI_WIFI_RSSI_REFRESH_MS 1000U
#define UI_STATION_LIST_MAX_ROWS 7U
#define UI_STATION_START_SCREEN_DELAY_MS 100U

static const char *TAG = "ui";
static QueueHandle_t s_input_queue;
static audio_source_manager_t *s_source_manager;
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
static ui_deferred_start_t s_deferred_station_start;
static bool s_showing_source;
static bool s_showing_radio;
static bool s_showing_station_list;
static int8_t s_wifi_rssi;
static uint32_t s_wifi_rssi_updated_ms;
static bool s_wifi_rssi_valid;
static bool s_wifi_rssi_seen;

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

static const char *ui_radio_state_text(internet_radio_state_t state)
{
    switch (state) {
    case INTERNET_RADIO_STATE_CONNECTING: return "Connecting...";
    case INTERNET_RADIO_STATE_PLAYING: return "Playing";
    case INTERNET_RADIO_STATE_PAUSED: return "Paused";
    case INTERNET_RADIO_STATE_RECONNECTING: return "Reconnecting...";
    case INTERNET_RADIO_STATE_ERROR: return "Connection error";
    case INTERNET_RADIO_STATE_STOPPED: return "Stopped";
    }
    return "Unknown";
}

static void ui_update_radio_status(void)
{
    if (!s_showing_radio) return;
    if (ui_deferred_start_is_pending(&s_deferred_station_start)) return;
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    ui_set_label_text_if_changed(s_source_status, ui_radio_state_text(status.state));
    const station_catalog_entry_t *entry = internet_radio_station_at(internet_radio_current_station_index());
    const char *display_title = (entry != NULL && entry->flag == 0)
                                    ? entry->name
                                    : (status.title[0] == '\0' ? status.station : status.title);
    ui_set_label_text_if_changed(s_source_detail, display_title);
    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), status.codec, status.bitrate_kbps);
    ui_set_label_text_if_changed(s_source_stream, stream_text);

    const uint32_t now = ui_tick_get_ms();
    if (!s_wifi_rssi_seen || (uint32_t)(now - s_wifi_rssi_updated_ms) >= UI_WIFI_RSSI_REFRESH_MS) {
        s_wifi_rssi_seen = true;
        s_wifi_rssi_updated_ms = now;
        s_wifi_rssi_valid = wifi_provisioning_get_rssi(&s_wifi_rssi);
    }
    char wifi_text[32];
    if (s_wifi_rssi_valid) {
        snprintf(wifi_text, sizeof(wifi_text), "Wi-Fi  %d dBm", (int)s_wifi_rssi);
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
    const size_t count = internet_radio_station_count();
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        if (row >= count) {
            lv_obj_add_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
        const station_catalog_entry_t *entry = internet_radio_station_at(row);
        const bool selected = row == station_list_selected_index(&s_station_list);
        const bool active = row == station_list_active_index(&s_station_list);
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

static void ui_show_source(void)
{
    const uint8_t index = ui_menu_selected_index(&s_menu);
    const audio_source_t selected_source = ui_menu_activate(&s_menu);
    if (s_source_manager != NULL && selected_source != AUDIO_SOURCE_NONE) {
        const audio_source_t active_source = audio_source_manager_active(s_source_manager);
        if (active_source != AUDIO_SOURCE_NONE && active_source != selected_source) {
            (void)audio_source_manager_stop(s_source_manager, active_source);
        }
        if (audio_source_manager_active(s_source_manager) == AUDIO_SOURCE_NONE) {
            const audio_source_result_t result = audio_source_manager_start(s_source_manager, selected_source);
            if (result != AUDIO_SOURCE_OK) ESP_LOGW(TAG, "source state change failed: %d", (int)result);
        }
    }
    lv_label_set_text(s_source_title, ui_menu_item_label((ui_menu_item_t)index));
    lv_screen_load(s_source_screen);
    s_showing_source = true;
    s_showing_station_list = false;
    s_showing_radio = selected_source == AUDIO_SOURCE_INTERNET_RADIO;
    if (s_showing_radio) {
        s_wifi_rssi_seen = false;
        s_wifi_rssi_valid = false;
        if (!internet_radio_start_saved_station()) {
            ui_show_station_list();
        } else {
            const station_catalog_entry_t *entry = internet_radio_station_at(internet_radio_current_station_index());
            if (entry != NULL) lv_label_set_text(s_source_title, entry->name);
            ui_update_radio_status();
        }
    } else {
        ui_set_label_text_if_changed(s_source_status, "Not implemented");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        ui_set_label_text_if_changed(s_source_wifi, "");
    }
}

static void ui_show_menu(void)
{
    ui_deferred_start_cancel(&s_deferred_station_start);
    if (s_showing_radio) (void)internet_radio_stop();
    if (s_source_manager != NULL) {
        const audio_source_t active_source = audio_source_manager_active(s_source_manager);
        if (active_source != AUDIO_SOURCE_NONE) {
            (void)audio_source_manager_stop(s_source_manager, active_source);
        }
    }
    ui_update_menu_highlight();
    lv_screen_load(s_menu_screen);
    s_showing_source = false;
    s_showing_radio = false;
    s_showing_station_list = false;
}

static void ui_show_station_list(void)
{
    if (!s_showing_radio) return;
    ui_deferred_start_cancel(&s_deferred_station_start);
    const size_t station_count = internet_radio_station_count();
    const size_t active_station_index = internet_radio_current_station_index();
    const size_t initial_index = active_station_index < station_count ? active_station_index : 0U;
    station_list_init(&s_station_list, station_count, initial_index,
                      active_station_index);
    ui_update_station_list();
    lv_screen_load(s_station_list_screen);
    s_showing_station_list = true;
}

static void ui_handle_input(board_input_action_t action)
{
    if (s_showing_station_list) {
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            if (station_list_handle_input(&s_station_list, action)) ui_update_station_list();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const size_t index = station_list_selected_index(&s_station_list);
            const station_catalog_entry_t *entry = internet_radio_station_at(index);
            if (entry != NULL) lv_label_set_text(s_source_title, entry->name);
            if (!station_list_selection_requires_switch(&s_station_list)) {
                s_showing_station_list = false;
                lv_screen_load(s_source_screen);
                ui_update_radio_status();
                return;
            }
            (void)internet_radio_stop();
            ui_set_label_text_if_changed(s_source_status, "Connecting");
            ui_set_label_text_if_changed(s_source_detail, entry == NULL ? "" : entry->name);
            ui_set_label_text_if_changed(s_source_stream, "MP3  |  -- kbps");
            ui_set_label_text_if_changed(s_source_wifi, "");
            s_showing_station_list = false;
            lv_screen_load(s_source_screen);
            ui_deferred_start_schedule(&s_deferred_station_start, index, ui_tick_get_ms(),
                                       UI_STATION_START_SCREEN_DELAY_MS);
        }
        return;
    }
    if (s_showing_source) {
        if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        } else if (s_showing_radio && action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            ui_show_station_list();
        } else if (s_showing_radio && action == BOARD_INPUT_ACTION_F3) {
            internet_radio_status_t status;
            internet_radio_get_status(&status);
            if (status.state == INTERNET_RADIO_STATE_PLAYING) (void)internet_radio_pause();
            else if (status.state == INTERNET_RADIO_STATE_PAUSED) (void)internet_radio_resume();
            ui_update_radio_status();
        }
        return;
    }
    if (ui_menu_handle_input(&s_menu, action)) {
        ui_update_menu_highlight();
    } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
        ui_show_source();
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
        ui_update_radio_status();
        lv_timer_handler();
        size_t station_index;
        if (ui_deferred_start_take_if_due(&s_deferred_station_start, ui_tick_get_ms(),
                                          &station_index)) {
            if (!internet_radio_start_station_index(station_index)) {
                ESP_LOGW(TAG, "station start failed; index=%u", (unsigned int)station_index);
            }
            ui_update_radio_status();
        }
    }
}

esp_err_t ui_init(audio_source_manager_t *source_manager)
{
    if (source_manager == NULL || s_input_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_input_queue = xQueueCreate(UI_INPUT_QUEUE_LENGTH, sizeof(board_input_action_t));
    if (s_input_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_source_manager = source_manager;
    ui_menu_init(&s_menu);
    ui_deferred_start_init(&s_deferred_station_start);
    lv_init();
    lv_tick_set_cb(ui_tick_get_ms);

    lv_color_t *buffer1 = heap_caps_malloc(UI_DRAW_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buffer2 = heap_caps_malloc(UI_DRAW_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer1 == NULL || buffer2 == NULL) {
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(BOARD_TFT_WIDTH, BOARD_TFT_HEIGHT);
    if (s_display == NULL) {
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
