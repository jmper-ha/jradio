#include "bt_link.h"

#include <stdio.h>
#include <string.h>

#include "album_art.h"
#include "board.h"
#include "board_config.h"
#include "board_options.h"
#include "device_settings.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "bt_link";


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
/* No transmit ring: a frame is at most 520 bytes, six milliseconds on the
 * wire, and uart_write_bytes() simply waits them out. Two kilobytes of
 * internal RAM matter more - see the stack below. */
#define BT_LINK_TX_RING 0
#define BT_LINK_TASK_STACK 8192
/* A PING every two seconds of silence, and three missed answers before the
 * module counts as gone: a reboot of the module is about four seconds, so a
 * brief absence is not reported as a failure. */
#define BT_LINK_PING_MS 2000U
#define BT_LINK_DEAD_MS 6500U
/* The name the module announces. */

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
/* The output: on/off and the speaker, from the settings; the scan list;
 * and the source-mode bookkeeping the tick runs - asking for the mode
 * again when the module lost it, and calling the speaker when it is not
 * connected. Both are retried on a slow beat, since a speaker that is off
 * answers nothing and asking every second would only fill the log. */
static bool s_output_enabled;
static uint8_t s_output_speaker[6];
static bool s_output_has_speaker;
static bt_link_scan_t s_scan;
static volatile bool s_scanning;
/* A scan asked for while the module was still changing role: started on
 * the ack that says it is in source mode. */
static volatile bool s_scan_pending;
static bt_link_key_listener_t s_key_listener;
/* The board's volume last sent to the speaker, as a percent, so a wheel
 * turn on the speaker (which sets the board) is not echoed back to it. */
static uint8_t s_output_volume_percent = 0xFFU;
static int64_t s_output_tried_us;
#define BT_LINK_OUTPUT_RETRY_MS 5000U
/* Calling a speaker that does not answer is five seconds of paging on every
 * channel, and with the module's antenna beside the board's Wi-Fi one, a
 * call every five seconds is a stream that stutters all day. A speaker is
 * called three times - at once, ten and twenty seconds later - after it is
 * chosen, the output switched on, or the module back in source mode; then
 * the module keeps quiet and waits for the speaker to call, which a paired
 * one does when it is switched on. */
#define BT_LINK_OUTPUT_CALLS_MAX 3U
static uint32_t s_output_call_gap_ms = BT_LINK_OUTPUT_RETRY_MS;
static uint32_t s_output_calls;
static bool s_output_calling;
static bool s_dac_muted;
static volatile bool s_speaker_connected;
static int64_t s_output_called_us;

static void bt_link_output_calls_reset(void)
{
    s_output_call_gap_ms = BT_LINK_OUTPUT_RETRY_MS;
    s_output_calls = 0U;
    s_output_calling = false;
    s_output_called_us = 0;
}
/* SET_MODE without waiting: the tick asks, the ack arrives on this task
 * like any frame, and the model records the mode. Used for the output,
 * whose caller has nothing to wait for. */

static esp_err_t bt_link_send(uint8_t type, uint8_t flags, const uint8_t *payload, size_t length);
esp_err_t bt_link_set_volume(uint8_t volume);

static esp_err_t bt_link_ask_mode(jbt_mode_t mode)
{
    const uint8_t payload[1] = {(uint8_t)mode};
    return bt_link_send(JBT_MSG_SET_MODE, 0U, payload, sizeof(payload));
}

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
static void bt_link_on_rate(uint32_t sample_rate)
{
    uint8_t payload[6];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_u32(&writer, sample_rate);
    jbt_put_u8(&writer, 16U);
    jbt_put_u8(&writer, 2U);
    (void)bt_link_send(JBT_MSG_I2S_FORMAT, 0U, payload, writer.length);
}

/* The name the module shows a phone or a speaker. Kept here so the greeting
 * can repeat it after the module reboots, and so a settings change that
 * leaves it as it was costs the module nothing. */
static char s_name[DEVICE_NAME_MAX];

static esp_err_t bt_link_send_name(void)
{
    uint8_t payload[2U + DEVICE_NAME_MAX];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    if (!jbt_put_tlv_string(&writer, JBT_TAG_NAME, s_name)) return ESP_ERR_INVALID_SIZE;
    return bt_link_send(JBT_MSG_SET_NAME, 0U, payload, writer.length);
}

static void bt_link_greet(void)
{
    if (s_name[0] == '\0') device_settings_device_name(s_name, sizeof(s_name));
    (void)bt_link_send_name();
    bt_link_on_rate(board_audio_sample_rate());
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
    if (frame->type == JBT_MSG_LEVEL) {
        /* The phone's audio goes module -> DAC and never through the board, so
         * this is the only level the meter has in that mode. */
        uint16_t left;
        uint16_t right;
        if (bt_link_model_level(frame, &left, &right)) board_audio_level_put(left, right);
        return;
    }
    if (frame->type == JBT_MSG_SCAN_RESULT) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        (void)bt_link_scan_apply(&s_scan, frame);
        xSemaphoreGive(s_state_lock);
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
    if (changed & BT_LINK_CHANGED_PLAY) {
        /* Stopped - the phone's player closed - takes the picture down;
         * playing again fetches it back (see the model). */
        if (s_state.status.play == JBT_PLAY_STOPPED) {
            if (s_cover_shown_hash != 0U) {
                album_art_clear();
                s_cover_shown_hash = 0U;
            }
        } else {
            s_cover_asked_us = 0;
        }
    }
    if (changed & BT_LINK_CHANGED_STATUS) {
        s_scanning = s_state.status.connection == JBT_CONN_SCANNING;
        s_speaker_connected = s_state.status.mode == JBT_MODE_SOURCE &&
                              s_state.status.connection == JBT_CONN_CONNECTED;
        /* The built-in DAC follows the speaker: muted while the speaker
         * has the sound, back on the moment it has not. */
        const bool to_speaker = bt_link_output_connected();
        if (to_speaker != s_dac_muted) {
            s_dac_muted = to_speaker;
            board_audio_set_dac_muted(to_speaker);
        }
    }
    if ((changed & BT_LINK_CHANGED_KEY) && s_key_listener != NULL) {
        s_key_listener((jbt_key_t)s_state.key);
    }
    if ((changed & BT_LINK_CHANGED_VOLUME) && s_output_enabled && s_state.status.mode == JBT_MODE_SOURCE) {
        /* The speaker's own wheel: the board follows, and the module is not
         * told back - it is the speaker's value already. */
        s_output_volume_percent = bt_link_volume_to_percent(s_state.status.volume);
        board_audio_set_volume(s_output_volume_percent);
    }
    if (changed & BT_LINK_CHANGED_MODE_ACK) {
        /* Back in source mode - after the phone, or a module reboot - the
         * module itself calls the last speaker once; the three calls from
         * here start over too. */
        if (s_state.acked_mode == JBT_MODE_SOURCE) bt_link_output_calls_reset();
        xSemaphoreGive(s_mode_ack);
        if (s_scan_pending && s_state.acked_mode == JBT_MODE_SOURCE) {
            s_scan_pending = false;
            const uint8_t payload[1] = {1U};
            (void)bt_link_send(JBT_MSG_SCAN, 0U, payload, sizeof(payload));
        }
    }
    if (changed & BT_LINK_CHANGED_EVENT) {
        ESP_LOGI(TAG, "event %u", (unsigned)s_state.event);
        if (s_state.event == JBT_EVENT_BOOTED) {
            /* A rebooted module has no phone, no track and no cover, and
             * its STATUS arrives after this; until then the old ones would
             * stand. The cover is taken down with the rest. */
            xSemaphoreTake(s_state_lock, portMAX_DELAY);
            const uint32_t events = s_state.events;
            bt_link_model_init(&s_state);
            s_state.events = events;
            xSemaphoreGive(s_state_lock);
            s_speaker_connected = false;
            if (s_dac_muted) {
                s_dac_muted = false;
                board_audio_set_dac_muted(false);
            }
            if (s_cover_shown_hash != 0U) {
                album_art_clear();
                s_cover_shown_hash = 0U;
            }
            bt_link_greet();
        }
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
        /* Wait for the first byte, then take whatever else has arrived.
         * Asking for the whole chunk in one call waits until 256 bytes are in
         * or the 200 ms are up, and the module's LEVEL frames are twelve
         * bytes twenty times a second: they reached the meter five times a
         * second, in batches, up to 200 ms late - a meter that swayed at a
         * steady height and not with the music. */
        int n = uart_read_bytes(BT_LINK_UART, chunk, 1U, pdMS_TO_TICKS(200));
        if (n > 0) {
            size_t waiting = 0U;
            (void)uart_get_buffered_data_len(BT_LINK_UART, &waiting);
            if (waiting > sizeof(chunk) - 1U) waiting = sizeof(chunk) - 1U;
            if (waiting > 0U) {
                const int more = uart_read_bytes(BT_LINK_UART, chunk + 1, waiting, 0);
                if (more > 0) n += more;
            }
        }
        for (int i = 0; i < n; ++i) {
            jbt_frame_t frame;
            if (jbt_decoder_feed(&s_decoder, chunk[i], &frame)) bt_link_on_frame(&frame);
        }
        const int64_t now = esp_timer_get_time();
        if (now - s_last_heard_us > (int64_t)BT_LINK_DEAD_MS * 1000 && s_alive) {
            s_alive = false;
            s_speaker_connected = false;
            if (s_dac_muted) {
                s_dac_muted = false;
                board_audio_set_dac_muted(false);
            }
            ESP_LOGW(TAG, "module silent for %u ms", (unsigned)BT_LINK_DEAD_MS);
            xSemaphoreTake(s_state_lock, portMAX_DELAY);
            bt_link_model_init(&s_state);
            xSemaphoreGive(s_state_lock);
        }
        if (bt_link_model_ping_due(now, s_last_ping_us, s_last_heard_us, s_state.protocol != 0U,
                                   BT_LINK_PING_MS)) {
            s_last_ping_us = now;
            (void)bt_link_send(JBT_MSG_PING, 0U, NULL, 0U);
        }
        /* The first piece of a new cover, or the one whose answer was lost. */
        if (s_alive && (s_cover_asked_us == 0 ||
                        now - s_cover_asked_us > (int64_t)BT_LINK_COVER_RETRY_MS * 1000)) {
            bt_link_cover_request();
        }
        /* The output: hold the module in source mode and on its speaker,
         * unless the player has it as a sink. */
        if (s_alive && s_output_enabled && s_wanted_mode != JBT_MODE_SINK &&
            now - s_output_tried_us > (int64_t)BT_LINK_OUTPUT_RETRY_MS * 1000) {
            s_output_tried_us = now;
            bt_link_state_t state;
            bt_link_snapshot(&state);
            if (state.status.mode != JBT_MODE_SOURCE) {
                s_wanted_mode = JBT_MODE_SOURCE;
                (void)bt_link_ask_mode(JBT_MODE_SOURCE);
            } else if (state.status.connection == JBT_CONN_CONNECTED) {
                bt_link_output_calls_reset();
            } else if (s_output_has_speaker && state.status.connection == JBT_CONN_NONE && !s_scanning) {
                if (s_output_calling) {
                    /* The last call went unanswered. */
                    s_output_calling = false;
                    s_output_call_gap_ms *= 2U;
                    s_output_called_us = now;
                    if (s_output_calls >= BT_LINK_OUTPUT_CALLS_MAX) {
                        ESP_LOGI(TAG, "the speaker does not answer; waiting for it to call");
                    }
                } else if (s_output_calls < BT_LINK_OUTPUT_CALLS_MAX &&
                           now - s_output_called_us >= (int64_t)s_output_call_gap_ms * 1000) {
                    s_output_calling = true;
                    ++s_output_calls;
                    (void)bt_link_send(JBT_MSG_CONNECT, 0U, s_output_speaker, sizeof(s_output_speaker));
                }
            }
        }
        /* The knob, to the speaker: when the board's volume moves away from
         * what the speaker last had, the speaker is told. Only while the
         * output is up, and never in answer to the speaker's own wheel. */
        if (s_alive && s_output_enabled && s_wanted_mode == JBT_MODE_SOURCE) {
            const uint8_t percent = board_audio_volume();
            if (percent != s_output_volume_percent) {
                s_output_volume_percent = percent;
                (void)bt_link_set_volume(bt_link_volume_to_module(percent));
            }
        }
    }
}

esp_err_t bt_link_init(void)
{
    if (s_started) return ESP_OK;
    /* No module in the wiring: nothing is started, and every call below
     * answers as for a module that is not there - no lock, no UART, never
     * alive. */
    if (!board_has_bluetooth()) {
        ESP_LOGD(TAG, "no module on this board");
        return ESP_OK;
    }
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
    ESP_RETURN_ON_ERROR(uart_set_pin(BT_LINK_UART, board_config_get()->uart1_tx,
                                     board_config_get()->uart1_rx, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "uart pins");
    s_started = true;
    /* 8 KB, and in PSRAM: the cover is decoded on this task (see
     * bt_link_cover_piece()), which is what the size is for, and internal
     * RAM is the pool the radio's decoder task and every TLS handshake
     * draw on. On the ILI9488, whose driver keeps a 14 KB conversion
     * buffer there, the module's 8 KB was the difference between Yandex
     * Music opening and "failed to create direct decoder task" - measured:
     * 26 KB free with the largest block 8 KB. A stack in external memory
     * is allowed for a task that never has the cache turned off under it,
     * and this one touches no flash; the control block stays internal. */
    static StaticTask_t s_task_block;
    void *stack = heap_caps_malloc(BT_LINK_TASK_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stack == NULL ||
        xTaskCreateStatic(bt_link_task, "bt_link", BT_LINK_TASK_STACK, NULL, 6, stack,
                          &s_task_block) == NULL) {
        free(stack);
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    board_audio_set_rate_listener(bt_link_on_rate);
    ESP_LOGI(TAG, "uart tx %d rx %d at %d", board_config_get()->uart1_tx,
             board_config_get()->uart1_rx, BT_LINK_BAUD);
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

void bt_link_brief(bt_link_brief_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (s_state_lock == NULL) return;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    out->status = s_state.status;
    out->position_ms = s_state.position_ms;
    out->duration_ms = s_state.duration_ms;
    out->track_revision = s_state.track_revision;
    xSemaphoreGive(s_state_lock);
}

void bt_link_track_text(char *title, size_t title_size, char *artist, size_t artist_size,
                        char *album, size_t album_size)
{
    if (title != NULL && title_size > 0U) title[0] = '\0';
    if (artist != NULL && artist_size > 0U) artist[0] = '\0';
    if (album != NULL && album_size > 0U) album[0] = '\0';
    if (s_state_lock == NULL) return;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (title != NULL) snprintf(title, title_size, "%s", s_state.title);
    if (artist != NULL) snprintf(artist, artist_size, "%s", s_state.artist);
    if (album != NULL) snprintf(album, album_size, "%s", s_state.album);
    xSemaphoreGive(s_state_lock);
}

void bt_link_peer_name(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (s_state_lock == NULL) return;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    snprintf(out, out_size, "%s", s_state.peer_name);
    xSemaphoreGive(s_state_lock);
}

bool bt_link_output_peer(char *address, size_t address_size, char *name, size_t name_size)
{
    if (address != NULL && address_size > 0U) address[0] = '\0';
    if (name != NULL && name_size > 0U) name[0] = '\0';
    if (!bt_link_output_connected() || s_state_lock == NULL) return false;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (address != NULL) bt_link_address_to_text(s_state.status.peer, address, address_size);
    if (name != NULL) snprintf(name, name_size, "%s", s_state.peer_name);
    xSemaphoreGive(s_state_lock);
    return true;
}

void bt_link_module_version(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (!s_alive || s_state_lock == NULL) return;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (s_state.protocol != 0U) {
        snprintf(out, out_size, "%u.%u.%u", (unsigned)s_state.fw_major, (unsigned)s_state.fw_minor,
                 (unsigned)s_state.fw_build);
    }
    xSemaphoreGive(s_state_lock);
}

esp_err_t bt_link_set_output(bool enabled, const char *address)
{
    if (!board_has_bluetooth()) return ESP_ERR_NOT_SUPPORTED;
    uint8_t speaker[6];
    const bool has_speaker = bt_link_address_from_text(address, speaker);
    const bool changed = enabled != s_output_enabled || has_speaker != s_output_has_speaker ||
                         (has_speaker && memcmp(speaker, s_output_speaker, sizeof(speaker)) != 0);
    s_output_enabled = enabled;
    s_output_has_speaker = has_speaker;
    if (has_speaker) memcpy(s_output_speaker, speaker, sizeof(speaker));
    if (!changed) return ESP_OK;
    ESP_LOGI(TAG, "output %s, speaker %s", enabled ? "on" : "off", has_speaker ? address : "none");
    if (!enabled && s_dac_muted) {
        s_dac_muted = false;
        board_audio_set_dac_muted(false);
    }
    /* The tick does the rest; here only what has to happen now: a speaker
     * that changed is dropped, and an output switched off lets the module
     * go - unless the player has it as a sink, which is its own business. */
    if (s_alive && s_wanted_mode != JBT_MODE_SINK) {
        if (!enabled) {
            s_wanted_mode = JBT_MODE_OFF;
            (void)bt_link_ask_mode(JBT_MODE_OFF);
        } else {
            /* Unless the choice is the device already sending - the UI
             * adopting a speaker that called in by itself - in which case
             * dropping it would be dropping the music. */
            bool already = false;
            if (has_speaker && s_speaker_connected && s_state_lock != NULL) {
                xSemaphoreTake(s_state_lock, portMAX_DELAY);
                already = memcmp(s_state.status.peer, speaker, sizeof(speaker)) == 0;
                xSemaphoreGive(s_state_lock);
            }
            if (!already) (void)bt_link_send(JBT_MSG_DISCONNECT, 0U, NULL, 0U);
            s_output_tried_us = 0;
        }
    }
    /* A speaker just chosen, or the output just switched on, gets its
     * three calls afresh. */
    bt_link_output_calls_reset();
    return ESP_OK;
}

void bt_link_output_call_again(void)
{
    bt_link_output_calls_reset();
    s_output_tried_us = 0;
}

bool bt_link_output_held_by_phone(void)
{
    return s_alive && s_wanted_mode == JBT_MODE_SINK;
}

bool bt_link_output_connected(void)
{
    /* Two flags rather than a snapshot: the UI asks every pass, and a copy
     * of the whole state each 10 ms is not what the answer costs. */
    return s_alive && s_output_enabled && s_speaker_connected;
}

esp_err_t bt_link_scan(bool on)
{
    if (!s_alive) return ESP_ERR_INVALID_STATE;
    bt_link_state_t state;
    bt_link_snapshot(&state);
    if (state.status.mode != JBT_MODE_SOURCE) {
        /* The scan needs the source role; ask for it and let the caller try
         * again once the module reports it. */
        if (s_wanted_mode == JBT_MODE_SINK) return ESP_ERR_INVALID_STATE;
        if (on) {
            xSemaphoreTake(s_state_lock, portMAX_DELAY);
            bt_link_scan_init(&s_scan);
            xSemaphoreGive(s_state_lock);
            s_scan_pending = true;
        }
        s_wanted_mode = JBT_MODE_SOURCE;
        (void)bt_link_ask_mode(JBT_MODE_SOURCE);
        return ESP_ERR_NOT_FINISHED;
    }
    if (on) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        bt_link_scan_init(&s_scan);
        xSemaphoreGive(s_state_lock);
    }
    const uint8_t payload[1] = {on ? 1U : 0U};
    return bt_link_send(JBT_MSG_SCAN, 0U, payload, sizeof(payload));
}

void bt_link_scan_snapshot(bt_link_scan_t *out)
{
    if (out == NULL) return;
    if (s_state_lock == NULL) {
        bt_link_scan_init(out);
        return;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    *out = s_scan;
    xSemaphoreGive(s_state_lock);
}

bool bt_link_scanning(void)
{
    return s_alive && s_scanning;
}

esp_err_t bt_link_i2s_format(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    uint8_t payload[6];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_u32(&writer, sample_rate);
    jbt_put_u8(&writer, bits);
    jbt_put_u8(&writer, channels);
    return bt_link_send(JBT_MSG_I2S_FORMAT, 0U, payload, writer.length);
}

esp_err_t bt_link_set_mode(jbt_mode_t mode, uint32_t timeout_ms)
{
    s_wanted_mode = (uint8_t)mode;
    if (!s_alive) return ESP_ERR_INVALID_STATE;
    /* A stale ack from an earlier, abandoned wait must not satisfy this one. */
    (void)xSemaphoreTake(s_mode_ack, 0);
    const uint8_t payload[1] = {(uint8_t)mode};
    ESP_RETURN_ON_ERROR(bt_link_send(JBT_MSG_SET_MODE, 0U, payload, sizeof(payload)), TAG, "send");
    /* A mode the module did not take is not wanted any more: left standing,
     * a refused sink would keep the output policy from ever putting the
     * speaker back. The module reports off after a failed switch anyway. */
    if (xSemaphoreTake(s_mode_ack, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "no MODE_ACK for mode %d in %u ms", (int)mode, (unsigned)timeout_ms);
        s_wanted_mode = JBT_MODE_OFF;
        return ESP_ERR_TIMEOUT;
    }
    bt_link_state_t state;
    bt_link_snapshot(&state);
    if (state.acked_mode != (uint8_t)mode || state.acked_result != JBT_RESULT_OK) {
        ESP_LOGW(TAG, "mode %d refused: now %u, result %u", (int)mode, state.acked_mode,
                 state.acked_result);
        s_wanted_mode = JBT_MODE_OFF;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void bt_link_set_key_listener(bt_link_key_listener_t listener)
{
    s_key_listener = listener;
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
    if (name == NULL || name[0] == '\0' || strlen(name) >= sizeof(s_name)) return ESP_ERR_INVALID_ARG;
    if (strcmp(s_name, name) == 0) return ESP_OK;
    snprintf(s_name, sizeof(s_name), "%s", name);
    if (!s_alive) return ESP_OK;
    return bt_link_send_name();
}

esp_err_t bt_link_disconnect(void)
{
    return bt_link_send(JBT_MSG_DISCONNECT, 0U, NULL, 0U);
}

esp_err_t bt_link_forget(const uint8_t address[6])
{
    if (address == NULL) return ESP_ERR_INVALID_ARG;
    return bt_link_send(JBT_MSG_FORGET, 0U, address, 6U);
}
