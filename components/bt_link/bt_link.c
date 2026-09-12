#include "bt_link.h"

#include <string.h>

#include "album_art.h"
#include "board_features.h"
#include "board_options.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "bt_link";

#if BOARD_HAS_BLUETOOTH

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* UART1: UART0 is the console, and the other peripherals on this board
 * are SPI and I2S. Any pins - the matrix routes them. */
#define BT_LINK_UART UART_NUM_1
#define BT_LINK_BAUD 921600
#define BT_LINK_RX_RING 4096
#define BT_LINK_TX_RING 2048
/* A PING every two seconds of silence, and three missed answers before the
 * module counts as gone: a reboot of the module is about four seconds, so a
 * brief absence is not reported as a failure. */
#define BT_LINK_PING_MS 2000U
#define BT_LINK_DEAD_MS 6500U
/* The name the module announces. */
#define BT_LINK_DEVICE_NAME "jRadio"

static bt_link_state_t s_state;
static SemaphoreHandle_t s_state_lock;
static SemaphoreHandle_t s_send_lock;
static SemaphoreHandle_t s_mode_ack;
static jbt_decoder_t s_decoder;
static uint8_t s_next_seq;
static bool s_started;
static volatile bool s_alive;
static int64_t s_last_heard_us;
static int64_t s_last_ping_us;
/* The mode the host last asked for. A module that reboots - flashed, power
 * glitch - comes up off, while the host still has the bus released for it;
 * so the mode is asked for again the moment it answers, from the link task,
 * with no one waiting on the ack. */
static volatile uint8_t s_wanted_mode = JBT_MODE_OFF;
/* The cover being fetched: PSRAM, allocated on the first cover and kept.
 * One request in flight; asked again when no piece has come for a while,
 * because a frame lost to noise would otherwise stall the picture until the
 * next track. */
static uint8_t *s_cover;
static int64_t s_cover_asked_us;
static uint32_t s_cover_shown_hash;
#define BT_LINK_COVER_RETRY_MS 800U

static esp_err_t bt_link_send(uint8_t type, uint8_t flags, const uint8_t *payload, size_t length)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    uint8_t wire[JBT_WIRE_MAX];
    xSemaphoreTake(s_send_lock, portMAX_DELAY);
    const jbt_frame_t frame = {
        .type = type, .flags = flags, .seq = s_next_seq++, .len = (uint16_t)length,
        .payload = payload,
    };
    const size_t n = jbt_frame_encode(&frame, wire, sizeof(wire));
    esp_err_t err = ESP_ERR_INVALID_SIZE;
    if (n > 0U) err = uart_write_bytes(BT_LINK_UART, wire, n) == (int)n ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(s_send_lock);
    return err;
}

/* What a module that has just appeared is told: its name, the mode the host
 * wants, and a request for its status. Once when it first answers, and again
 * on its BOOTED event - a reboot takes it under two seconds, shorter than
 * the silence that would otherwise mark it gone, so the event is the only
 * sign of the reboot the host gets. */
static void bt_link_greet(void)
{
    (void)bt_link_set_name(BT_LINK_DEVICE_NAME);
    if (s_wanted_mode != JBT_MODE_OFF) {
        const uint8_t payload[1] = {s_wanted_mode};
        (void)bt_link_send(JBT_MSG_SET_MODE, 0U, payload, sizeof(payload));
        ESP_LOGW(TAG, "asking the module for mode %u again", s_wanted_mode);
    }
    (void)bt_link_send(JBT_MSG_GET_STATUS, 0U, NULL, 0U);
}

static void bt_link_cover_request(void)
{
    uint32_t offset;
    uint16_t length;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool wanted = bt_link_model_cover_wanted(&s_state, &offset, &length);
    xSemaphoreGive(s_state_lock);
    if (!wanted) return;
    if (s_cover == NULL) {
        s_cover = heap_caps_malloc(BT_LINK_COVER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_cover == NULL) {
            ESP_LOGW(TAG, "no PSRAM for a cover");
            return;
        }
    }
    uint8_t payload[6];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_u32(&writer, offset);
    jbt_put_u16(&writer, length);
    s_cover_asked_us = esp_timer_get_time();
    (void)bt_link_send(JBT_MSG_COVER_GET, 0U, payload, writer.length);
}

/* A piece arrived: into the buffer, and when it was the last, to the
 * screen. The decode runs here, on this task, which is why it has the
 * stack it has - tens of milliseconds nobody else is waiting on. */
static void bt_link_cover_piece(const jbt_frame_t *frame)
{
    uint32_t offset;
    const uint8_t *bytes;
    size_t length;
    bool complete = false;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool taken = bt_link_model_cover_data(&s_state, frame, &offset, &bytes, &length, &complete);
    if (!taken && frame->len == 4U) {
        /* An empty piece: the module no longer has this cover. Give up
         * rather than ask every retry for a picture that is gone. */
        s_state.cover_size = 0U;
    }
    const uint32_t size = s_state.cover_size;
    const uint32_t hash = s_state.cover_hash;
    xSemaphoreGive(s_state_lock);
    if (!taken || s_cover == NULL) return;
    memcpy(&s_cover[offset], bytes, length);
    s_cover_asked_us = 0;
    if (!complete) {
        bt_link_cover_request();
        return;
    }
    ESP_LOGI(TAG, "cover %u bytes (%08x)", (unsigned)size, (unsigned)hash);
    if (album_art_set_image(s_cover, size)) {
        s_cover_shown_hash = hash;
    } else {
        ESP_LOGW(TAG, "cover did not decode");
    }
}

static void bt_link_on_frame(const jbt_frame_t *frame)
{
    s_last_heard_us = esp_timer_get_time();
    if (!s_alive) {
        s_alive = true;
        ESP_LOGI(TAG, "module answered");
        bt_link_greet();
    }
    uint8_t level;
    char text[JBT_PAYLOAD_MAX];
    if (bt_link_model_log(frame, &level, text, sizeof(text))) {
        /* The module's own lines, under this log with a mark, so a trace of
         * both boards reads as one. */
        switch (level) {
        case ESP_LOG_ERROR: ESP_LOGE(TAG, "bt: %s", text); break;
        case ESP_LOG_WARN: ESP_LOGW(TAG, "bt: %s", text); break;
        default: ESP_LOGI(TAG, "bt: %s", text); break;
        }
        return;
    }
    if (frame->type == JBT_MSG_COVER_DATA) {
        bt_link_cover_piece(frame);
        return;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const uint32_t changed = bt_link_model_apply(&s_state, frame);
    xSemaphoreGive(s_state_lock);
    if (changed & BT_LINK_CHANGED_COVER_INFO) {
        /* Nothing, or something already shown: take it down or leave it.
         * Something new: the first piece is asked for below, on the tick. */
        bt_link_state_t state;
        bt_link_snapshot(&state);
        if (state.cover_size == 0U) {
            album_art_clear();
            s_cover_shown_hash = 0U;
        }
        s_cover_asked_us = 0;
    }
    if (changed & BT_LINK_CHANGED_MODE_ACK) xSemaphoreGive(s_mode_ack);
    if (changed & BT_LINK_CHANGED_EVENT) {
        ESP_LOGI(TAG, "event %u", (unsigned)s_state.event);
        if (s_state.event == JBT_EVENT_BOOTED) bt_link_greet();
        if (s_state.event == JBT_EVENT_DISCONNECTED && s_cover_shown_hash != 0U) {
            /* The picture belonged to the phone that left. */
            album_art_clear();
            s_cover_shown_hash = 0U;
        }
    }
}

static void bt_link_task(void *arg)
{
    (void)arg;
    uint8_t chunk[256];
    while (true) {
        const int n = uart_read_bytes(BT_LINK_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; ++i) {
            jbt_frame_t frame;
            if (jbt_decoder_feed(&s_decoder, chunk[i], &frame)) bt_link_on_frame(&frame);
        }
        const int64_t now = esp_timer_get_time();
        if (now - s_last_heard_us > (int64_t)BT_LINK_DEAD_MS * 1000 && s_alive) {
            s_alive = false;
            ESP_LOGW(TAG, "module silent for %u ms", (unsigned)BT_LINK_DEAD_MS);
            xSemaphoreTake(s_state_lock, portMAX_DELAY);
            bt_link_model_init(&s_state);
            xSemaphoreGive(s_state_lock);
        }
        if (now - s_last_ping_us > (int64_t)BT_LINK_PING_MS * 1000 &&
            now - s_last_heard_us > (int64_t)BT_LINK_PING_MS * 1000) {
            s_last_ping_us = now;
            (void)bt_link_send(JBT_MSG_PING, 0U, NULL, 0U);
        }
        /* The first piece of a new cover, or the one whose answer was lost. */
        if (s_alive && (s_cover_asked_us == 0 ||
                        now - s_cover_asked_us > (int64_t)BT_LINK_COVER_RETRY_MS * 1000)) {
            bt_link_cover_request();
        }
    }
}

esp_err_t bt_link_init(void)
{
    if (s_started) return ESP_OK;
    s_state_lock = xSemaphoreCreateMutex();
    s_send_lock = xSemaphoreCreateMutex();
    s_mode_ack = xSemaphoreCreateBinary();
    if (s_state_lock == NULL || s_send_lock == NULL || s_mode_ack == NULL) return ESP_ERR_NO_MEM;
    bt_link_model_init(&s_state);
    jbt_decoder_init(&s_decoder);

    const uart_config_t config = {
        .baud_rate = BT_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(BT_LINK_UART, BT_LINK_RX_RING, BT_LINK_TX_RING, 0, NULL, 0),
                        TAG, "uart driver");
    ESP_RETURN_ON_ERROR(uart_param_config(BT_LINK_UART, &config), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(BT_LINK_UART, BT_UART_TX_GPIO, BT_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "uart pins");
    s_started = true;
    /* 8 KB: the cover is decoded on this task - see bt_link_cover_piece(). */
    if (xTaskCreate(bt_link_task, "bt_link", 8192, NULL, 6, NULL) != pdPASS) {
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "uart tx %d rx %d at %d", BT_UART_TX_GPIO, BT_UART_RX_GPIO, BT_LINK_BAUD);
    return ESP_OK;
}

bool bt_link_alive(void)
{
    return s_alive;
}

void bt_link_snapshot(bt_link_state_t *out)
{
    if (out == NULL) return;
    if (s_state_lock == NULL) {
        bt_link_model_init(out);
        return;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_state_lock);
}

esp_err_t bt_link_set_mode(jbt_mode_t mode, uint32_t timeout_ms)
{
    s_wanted_mode = (uint8_t)mode;
    if (!s_alive) return ESP_ERR_INVALID_STATE;
    /* A stale ack from an earlier, abandoned wait must not satisfy this one. */
    (void)xSemaphoreTake(s_mode_ack, 0);
    const uint8_t payload[1] = {(uint8_t)mode};
    ESP_RETURN_ON_ERROR(bt_link_send(JBT_MSG_SET_MODE, 0U, payload, sizeof(payload)), TAG, "send");
    if (xSemaphoreTake(s_mode_ack, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "no MODE_ACK for mode %d in %u ms", (int)mode, (unsigned)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    bt_link_state_t state;
    bt_link_snapshot(&state);
    if (state.acked_mode != (uint8_t)mode || state.acked_result != JBT_RESULT_OK) {
        ESP_LOGW(TAG, "mode %d refused: now %u, result %u", (int)mode, state.acked_mode,
                 state.acked_result);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bt_link_pairing(bool on)
{
    const uint8_t payload[1] = {on ? 1U : 0U};
    return bt_link_send(JBT_MSG_PAIRING, 0U, payload, sizeof(payload));
}

esp_err_t bt_link_passthrough(jbt_key_t key)
{
    const uint8_t payload[1] = {(uint8_t)key};
    return bt_link_send(JBT_MSG_PASSTHROUGH, 0U, payload, sizeof(payload));
}

esp_err_t bt_link_set_volume(uint8_t volume)
{
    const uint8_t payload[1] = {volume > 127U ? 127U : volume};
    return bt_link_send(JBT_MSG_SET_VOLUME, 0U, payload, sizeof(payload));
}

esp_err_t bt_link_set_name(const char *name)
{
    uint8_t payload[2U + 32U];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    if (!jbt_put_tlv_string(&writer, JBT_TAG_NAME, name)) return ESP_ERR_INVALID_SIZE;
    return bt_link_send(JBT_MSG_SET_NAME, 0U, payload, writer.length);
}

esp_err_t bt_link_disconnect(void)
{
    return bt_link_send(JBT_MSG_DISCONNECT, 0U, NULL, 0U);
}

#else /* !BOARD_HAS_BLUETOOTH */

esp_err_t bt_link_init(void)
{
    ESP_LOGD(TAG, "no module on this board");
    return ESP_OK;
}

bool bt_link_alive(void)
{
    return false;
}

void bt_link_snapshot(bt_link_state_t *out)
{
    if (out != NULL) bt_link_model_init(out);
}

esp_err_t bt_link_set_mode(jbt_mode_t mode, uint32_t timeout_ms)
{
    (void)mode;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_link_pairing(bool on)
{
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_link_passthrough(jbt_key_t key)
{
    (void)key;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_link_set_volume(uint8_t volume)
{
    (void)volume;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_link_set_name(const char *name)
{
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_link_disconnect(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
