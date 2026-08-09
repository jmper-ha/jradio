#include "internet_radio.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "board.h"
#include "icy_metadata.h"
#include "pcm_diagnostics.h"
#include "radio_http_status.h"
#include "radio_decoder.h"
#include "radio_stream_format.h"
#include "station_resume.h"

#define RADIO_HTTP_BUFFER_SIZE 2048
#define RADIO_CATALOG_BUFFER_SIZE 16384
#define RADIO_HTTP_MAX_REDIRECTS 5U
#define RADIO_HTTP_CONNECT_RETRIES 3U
#define RADIO_HTTP_IDLE_TIMEOUT_MS 30000U
#define RADIO_HTTP_RECONNECT_DELAY_MS 500U
#define RADIO_DIRECT_INPUT_SIZE 16384U
#define RADIO_DIRECT_NETWORK_CHUNK 2048U
#define RADIO_DIRECT_PCM_SIZE 16384U
#define RADIO_DIRECT_STOP_TIMEOUT_MS 12000U

static const char *TAG = "internet_radio";

typedef struct {
    esp_http_client_handle_t http;
    icy_metadata_t icy;
    internet_radio_status_t status;
    size_t icy_interval;
    size_t pcm_bytes;
    size_t pcm_blocks;
    bool output_logged;
    bool output_started;
    bool initialized;
    radio_stream_format_t stream_format;
    station_catalog_t catalog;
    size_t current_station_index;
    radio_decoder_t *decoder;
    TaskHandle_t direct_task;
    SemaphoreHandle_t direct_done;
    SemaphoreHandle_t direct_io_mutex;
    atomic_bool direct_stop_requested;
    atomic_bool direct_paused;
    uint32_t output_sample_rate;
} internet_radio_context_t;

static internet_radio_context_t s_radio;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static bool radio_direct_reconnect(internet_radio_context_t *radio);

static const station_catalog_entry_t s_europa_plus_station = {
    .name = "Европа плюс",
    .url = "http://online-1.gkvr.ru:8000/europa_krd_128.aac",
    .flag = 0,
};

static esp_err_t radio_sync_output(internet_radio_context_t *radio)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    return board_audio_set_enabled(internet_radio_state_output_enabled(status.state));
}

static bool radio_status_apply(internet_radio_context_t *radio, internet_radio_event_t event)
{
    bool applied;
    taskENTER_CRITICAL(&s_status_lock);
    applied = internet_radio_state_apply(&radio->status.state, event);
    taskEXIT_CRITICAL(&s_status_lock);
    return applied;
}

static void radio_http_close(internet_radio_context_t *radio)
{
    if (radio->http == NULL) {
        return;
    }
    esp_http_client_close(radio->http);
    esp_http_client_cleanup(radio->http);
    radio->http = NULL;
}

static int radio_http_read_blocking(internet_radio_context_t *radio, uint8_t *buffer, int length)
{
    const TickType_t idle_started = xTaskGetTickCount();
    for (;;) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return -1;
        }
        const int received = esp_http_client_read(radio->http, (char *)buffer, length);
        const internet_radio_read_action_t action =
            internet_radio_read_classify(received, -ESP_ERR_HTTP_EAGAIN);
        if (action == INTERNET_RADIO_READ_DATA) {
            return received;
        }
        if (action == INTERNET_RADIO_READ_CLOSED) {
            ESP_LOGW(TAG, "HTTP stream closed by server");
            return 0;
        }
        if (action == INTERNET_RADIO_READ_ERROR) {
            ESP_LOGE(TAG, "HTTP stream read failed: result=%d", received);
            return received;
        }
        if ((uint32_t)((xTaskGetTickCount() - idle_started) * portTICK_PERIOD_MS) >=
            RADIO_HTTP_IDLE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "HTTP stream idle for %u ms", RADIO_HTTP_IDLE_TIMEOUT_MS);
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool radio_delay_interruptible(internet_radio_context_t *radio, uint32_t delay_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

static void radio_set_title(void *context, const char *title)
{
    internet_radio_context_t *radio = context;
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(radio->status.title, sizeof(radio->status.title), "%s", title);
    taskEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "ICY title: %s", title);
}

static esp_err_t radio_pcm_output(internet_radio_context_t *radio, const uint8_t *data,
                                  size_t data_size)
{
    const uint16_t peak = pcm_s16le_peak(data, (size_t)data_size);
    size_t written = 0;
    esp_err_t result;
    if (internet_radio_output_start_once(&radio->output_started)) {
        result = board_audio_start(data, (size_t)data_size, &written);
        if (result == ESP_OK && written < (size_t)data_size) {
            size_t remainder_written = 0;
            result = board_audio_write(data + written, (size_t)data_size - written,
                                       &remainder_written, 1000);
            written += remainder_written;
        }
    } else {
        result = board_audio_write(data, (size_t)data_size, &written, 1000);
    }
    radio->pcm_bytes += written;
    radio->pcm_blocks++;
    if (radio->pcm_blocks <= 4U) {
        ESP_LOGI(TAG, "PCM progress: block=%u bytes=%u written=%u total=%u peak=%u",
                 (unsigned int)radio->pcm_blocks, (unsigned int)data_size,
                 (unsigned int)written, (unsigned int)radio->pcm_bytes, peak);
    }
    if (!radio->output_logged && written > 0U) {
        ESP_LOGI(TAG, "PCM output started: block=%d total=%u peak=%u", data_size,
                 (unsigned int)radio->pcm_bytes, peak);
        radio->output_logged = true;
        if (radio_status_apply(radio, INTERNET_RADIO_EVENT_PLAYER_RUNNING)) {
            (void)radio_sync_output(radio);
        }
    }
    if (result != ESP_OK || written != data_size) {
        ESP_LOGE(TAG, "PCM output failed: err=%s written=%u expected=%u",
                 esp_err_to_name(result), (unsigned int)written, (unsigned int)data_size);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t radio_direct_update_info(internet_radio_context_t *radio,
                                          const radio_decoder_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t sample_rate = info->sample_rate;
    bool sample_rate_changed = false;
    bool restart_output = false;
    xSemaphoreTake(radio->direct_io_mutex, portMAX_DELAY);
    taskENTER_CRITICAL(&s_status_lock);
    if (info->bitrate_kbps > 0U && radio->status.bitrate_kbps == 0U) {
        radio->status.bitrate_kbps = (uint16_t)info->bitrate_kbps;
    }
    if (sample_rate > 0U && sample_rate != radio->output_sample_rate) {
        restart_output = internet_radio_output_needs_restart(
            radio->output_sample_rate, sample_rate, radio->output_started);
        radio->output_sample_rate = sample_rate;
        sample_rate_changed = true;
    }
    taskEXIT_CRITICAL(&s_status_lock);
    esp_err_t result = ESP_OK;
    if (restart_output) {
        ESP_LOGI(TAG, "audio format changed; restarting I2S with silent pre-roll");
        result = board_audio_set_enabled(false);
        if (result == ESP_OK) {
            radio->output_started = false;
        }
    }
    if (sample_rate_changed) {
        if (result == ESP_OK) {
            result = board_audio_set_sample_rate(sample_rate);
        }
    }
    xSemaphoreGive(radio->direct_io_mutex);
    return result;
}

static void radio_direct_task(void *arg)
{
    internet_radio_context_t *radio = arg;
    uint8_t *compressed = heap_caps_malloc(RADIO_DIRECT_INPUT_SIZE,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (compressed == NULL) {
        compressed = heap_caps_malloc(RADIO_DIRECT_INPUT_SIZE,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    uint8_t *network = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (network == NULL) {
        network = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    uint8_t *audio = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (audio == NULL) {
        audio = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    uint8_t *pcm = heap_caps_malloc(RADIO_DIRECT_PCM_SIZE,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(RADIO_DIRECT_PCM_SIZE,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    size_t available = 0U;
    unsigned int decode_error_retries = 0U;
    bool fatal = false;
    if (compressed == NULL || network == NULL || audio == NULL || pcm == NULL || radio->decoder == NULL) {
        ESP_LOGE(TAG, "direct decoder buffer allocation failed");
        fatal = true;
    }

    while (!fatal &&
           !atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
        if (atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bool need_input = available == 0U;
        if (!need_input) {
            size_t consumed = 0U;
            size_t pcm_bytes = 0U;
            radio_decoder_info_t info = {0};
            const radio_decoder_result_t result = radio_decoder_decode(
                radio->decoder, compressed, available, pcm, RADIO_DIRECT_PCM_SIZE, &consumed,
                &pcm_bytes, &info);
            if (consumed > available) {
                fatal = true;
                break;
            }
            if (consumed > 0U) {
                available -= consumed;
                if (available > 0U) {
                    memmove(compressed, compressed + consumed, available);
                }
            }
            if (result == RADIO_DECODER_HEADER_READY) {
                decode_error_retries = 0U;
                if (radio_direct_update_info(radio, &info) != ESP_OK) {
                    ESP_LOGE(TAG, "failed to configure audio output for %u Hz",
                             (unsigned int)info.sample_rate);
                    fatal = true;
                    break;
                }
                ESP_LOGI(TAG, "direct decoder ready: codec=%s rate=%u channels=%u bits=%u",
                         radio_stream_format_codec_name(radio->stream_format),
                         (unsigned int)info.sample_rate, (unsigned int)info.channels,
                         (unsigned int)info.bits_per_sample);
            } else if (result == RADIO_DECODER_PCM_READY) {
                decode_error_retries = 0U;
                if (radio_direct_update_info(radio, &info) != ESP_OK) {
                    ESP_LOGE(TAG, "failed to configure audio output for %u Hz",
                             (unsigned int)info.sample_rate);
                    fatal = true;
                    break;
                }
                xSemaphoreTake(radio->direct_io_mutex, portMAX_DELAY);
                const bool output_allowed =
                    !atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) &&
                    !atomic_load_explicit(&radio->direct_paused, memory_order_acquire) &&
                    pcm_bytes > 0U;
                const esp_err_t output_result = output_allowed
                                                    ? radio_pcm_output(radio, pcm, pcm_bytes)
                                                    : ESP_OK;
                xSemaphoreGive(radio->direct_io_mutex);
                if (output_result != ESP_OK) {
                    fatal = true;
                    break;
                }
                need_input = false;
            } else if (result == RADIO_DECODER_NEED_MORE_DATA) {
                need_input = true;
            } else {
                if (radio->stream_format == RADIO_STREAM_FORMAT_MP3 && consumed == 0U &&
                    pcm_bytes == 0U && decode_error_retries < 100U) {
                    ++decode_error_retries;
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
                ESP_LOGE(TAG, "direct decoder error: codec=%s result=%d available=%u consumed=%u pcm=%u",
                         radio_stream_format_codec_name(radio->stream_format), (int)result,
                         (unsigned int)available, (unsigned int)consumed,
                         (unsigned int)pcm_bytes);
                fatal = true;
                break;
            }
        }

        if (need_input && available < RADIO_DIRECT_INPUT_SIZE) {
            const size_t room = RADIO_DIRECT_INPUT_SIZE - available;
            const int request_length = (int)(room < RADIO_DIRECT_NETWORK_CHUNK
                                                 ? room
                                                 : RADIO_DIRECT_NETWORK_CHUNK);
            const int received = radio_http_read_blocking(radio, network,
                                                          request_length);
            if (received <= 0) {
                if (atomic_load_explicit(&radio->direct_stop_requested,
                                         memory_order_acquire)) {
                    break;
                }
                if (atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
                    continue;
                }
                if (radio_direct_reconnect(radio)) {
                    available = 0U;
                    decode_error_retries = 0U;
                    continue;
                }
                if (atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
                    continue;
                }
                fatal = !atomic_load_explicit(&radio->direct_stop_requested,
                                              memory_order_acquire);
                break;
            }
            size_t audio_length = 0U;
            const icy_metadata_result_t icy_result = icy_metadata_feed(
                &radio->icy, network, (size_t)received, audio, RADIO_DIRECT_NETWORK_CHUNK,
                &audio_length);
            if (icy_result != ICY_METADATA_OK) {
                ESP_LOGE(TAG, "ICY metadata processing failed: result=%d received=%d",
                         (int)icy_result, received);
                fatal = true;
                break;
            }
            if (audio_length > RADIO_DIRECT_INPUT_SIZE - available) {
                ESP_LOGE(TAG, "direct compressed buffer overflow: audio=%u available=%u",
                         (unsigned int)audio_length, (unsigned int)available);
                fatal = true;
                break;
            }
            if (audio_length > 0U) {
                memcpy(compressed + available, audio, audio_length);
                available += audio_length;
            }
        } else if (internet_radio_input_buffer_stalled(need_input, available,
                                                       RADIO_DIRECT_INPUT_SIZE)) {
            ESP_LOGE(TAG, "decoder needs more data but compressed buffer is full");
            fatal = true;
        }
    }

    if (fatal &&
        !atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
        ESP_LOGE(TAG, "direct decoder stopped with an error");
        (void)radio_status_apply(radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(radio);
    }
    (void)board_audio_set_enabled(false);
    radio_http_close(radio);
    radio_decoder_destroy(radio->decoder);
    radio->decoder = NULL;
    heap_caps_free(compressed);
    heap_caps_free(network);
    heap_caps_free(audio);
    heap_caps_free(pcm);
    radio->direct_task = NULL;
    if (radio->direct_done != NULL) {
        xSemaphoreGive(radio->direct_done);
    }
    vTaskDelete(NULL);
}

static esp_err_t radio_http_event(esp_http_client_event_t *event)
{
    internet_radio_context_t *radio = event->user_data;
    if (radio == NULL || event->event_id != HTTP_EVENT_ON_HEADER || event->header_key == NULL ||
        event->header_value == NULL) {
        return ESP_OK;
    }
    if (strcasecmp(event->header_key, "icy-metaint") == 0) {
        radio->icy_interval = (size_t)strtoul(event->header_value, NULL, 10);
    } else if (strcasecmp(event->header_key, "icy-br") == 0) {
        const uint16_t bitrate = radio_stream_bitrate_kbps_from_icy_header(event->header_value);
        if (bitrate > 0U) {
            taskENTER_CRITICAL(&s_status_lock);
            radio->status.bitrate_kbps = bitrate;
            taskEXIT_CRITICAL(&s_status_lock);
        }
    } else if (strcasecmp(event->header_key, "icy-name") == 0) {
        taskENTER_CRITICAL(&s_status_lock);
        snprintf(radio->status.station, sizeof(radio->status.station), "%s", event->header_value);
        taskEXIT_CRITICAL(&s_status_lock);
    }
    return ESP_OK;
}

static esp_err_t radio_http_open(internet_radio_context_t *radio, const char *url)
{
    if (url == NULL || url[0] == '\0') return ESP_ERR_INVALID_ARG;
    const esp_http_client_config_t config = {
        .url = url,
        .buffer_size = RADIO_HTTP_BUFFER_SIZE,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
        .user_agent = "VLC/3.0",
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = radio_http_event,
        .user_data = radio,
    };
    radio->icy_interval = 0;
    radio->pcm_bytes = 0;
    radio->pcm_blocks = 0;
    radio->output_logged = false;
    radio->output_started = false;
    radio->output_sample_rate = 0U;
    taskENTER_CRITICAL(&s_status_lock);
    radio->status.bitrate_kbps = 0U;
    snprintf(radio->status.codec, sizeof(radio->status.codec), "%s",
             radio_stream_format_codec_name(radio->stream_format));
    taskEXIT_CRITICAL(&s_status_lock);
    radio->http = esp_http_client_init(&config);
    if (radio->http == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(radio->http, "Icy-MetaData", "1");
    unsigned int redirects = 0U;
    while (err == ESP_OK) {
        const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        ESP_LOGI(TAG, "HTTP open heap: internal_free=%u largest_block=%u",
                 (unsigned int)heap_caps_get_free_size(internal_caps),
                 (unsigned int)heap_caps_get_largest_free_block(internal_caps));
        err = esp_http_client_open(radio->http, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open failed after %u redirects: %s", redirects,
                     esp_err_to_name(err));
            int tls_error = 0;
            int tls_flags = 0;
            if (esp_http_client_get_and_clear_last_tls_error(radio->http, &tls_error,
                                                              &tls_flags) == ESP_OK &&
                (tls_error != 0 || tls_flags != 0)) {
                ESP_LOGE(TAG, "TLS details: error=%d flags=0x%x", tls_error, tls_flags);
            }
            break;
        }
        if (esp_http_client_fetch_headers(radio->http) < 0) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "HTTP header fetch failed after %u redirects", redirects);
            break;
        }

        const int status_code = esp_http_client_get_status_code(radio->http);
        if (!radio_http_status_is_redirect(status_code)) {
            if (status_code != 200) {
                err = ESP_FAIL;
                ESP_LOGE(TAG, "HTTP status %d", status_code);
            }
            break;
        }
        if (redirects >= RADIO_HTTP_MAX_REDIRECTS) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "HTTP redirect limit reached; status=%d", status_code);
            break;
        }
        ESP_LOGI(TAG, "HTTP redirect %u: status=%d", redirects + 1U, status_code);
        err = esp_http_client_set_redirection(radio->http);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP redirect target unavailable: %s", esp_err_to_name(err));
            break;
        }
        err = esp_http_client_close(radio->http);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP redirect close failed: %s", esp_err_to_name(err));
            break;
        }
        ++redirects;
    }
    if (err != ESP_OK) {
        radio_http_close(radio);
        return err;
    }

    icy_metadata_init(&radio->icy, radio->icy_interval, radio_set_title, radio);
    ESP_LOGI(TAG, "HTTP connected: status=%d icy-metaint=%u",
             esp_http_client_get_status_code(radio->http), (unsigned int)radio->icy_interval);
    return ESP_OK;
}

static bool radio_direct_reconnect(internet_radio_context_t *radio)
{
    if (radio->current_station_index >= radio->catalog.count ||
        atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
        return false;
    }

    xSemaphoreTake(radio->direct_io_mutex, portMAX_DELAY);
    if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
        xSemaphoreGive(radio->direct_io_mutex);
        return false;
    }
    (void)radio_status_apply(radio, INTERNET_RADIO_EVENT_FAILURE);
    (void)radio_sync_output(radio);
    radio->output_started = false;
    xSemaphoreGive(radio->direct_io_mutex);

    radio_http_close(radio);
    radio_decoder_reset(radio->decoder);
    const char *url = radio->catalog.entries[radio->current_station_index].url;
    for (unsigned int attempt = 0U; attempt < RADIO_HTTP_CONNECT_RETRIES; ++attempt) {
        if (!radio_delay_interruptible(radio, RADIO_HTTP_RECONNECT_DELAY_MS)) {
            return false;
        }
        if (!radio_status_apply(radio, INTERNET_RADIO_EVENT_RETRY)) {
            return false;
        }
        const esp_err_t err = radio_http_open(radio, url);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP stream reconnected on attempt %u/%u", attempt + 1U,
                     RADIO_HTTP_CONNECT_RETRIES);
            return true;
        }
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) ||
            atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
            return false;
        }
        if (!radio_status_apply(radio, INTERNET_RADIO_EVENT_FAILURE)) {
            return false;
        }
        ESP_LOGW(TAG, "HTTP reconnect attempt %u/%u failed: %s", attempt + 1U,
                 RADIO_HTTP_CONNECT_RETRIES, esp_err_to_name(err));
    }
    return false;
}

static void radio_load_catalog(internet_radio_context_t *radio)
{
    FILE *file = fopen(STATION_CATALOG_PATH, "r");
    if (file == NULL) {
        radio->catalog.count = 0;
        ESP_LOGW(TAG, "station catalog unavailable: %s", STATION_CATALOG_PATH);
        return;
    }
    char *text = malloc(RADIO_CATALOG_BUFFER_SIZE);
    if (text == NULL) {
        fclose(file);
        radio->catalog.count = 0;
        ESP_LOGE(TAG, "station catalog allocation failed");
        return;
    }
    const size_t bytes_read = fread(text, 1, RADIO_CATALOG_BUFFER_SIZE - 1, file);
    const bool complete = feof(file) != 0;
    fclose(file);
    if (!complete) {
        free(text);
        radio->catalog.count = 0;
        ESP_LOGE(TAG, "station catalog is too large or unreadable");
        return;
    }
    text[bytes_read] = '\0';
    (void)station_catalog_load_text(text, &radio->catalog);
    if (station_catalog_append_if_missing(&radio->catalog, &s_europa_plus_station)) {
        ESP_LOGI(TAG, "added built-in AAC station to catalog");
    }
    free(text);
    ESP_LOGI(TAG, "loaded %u internet radio stations", (unsigned int)radio->catalog.count);
}

esp_err_t internet_radio_init(void)
{
    if (s_radio.initialized) return ESP_OK;
    s_radio.direct_done = xSemaphoreCreateBinary();
    if (s_radio.direct_done == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_radio.direct_io_mutex = xSemaphoreCreateMutex();
    if (s_radio.direct_io_mutex == NULL) {
        vSemaphoreDelete(s_radio.direct_done);
        s_radio.direct_done = NULL;
        return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_status_lock);
    s_radio.status.state = INTERNET_RADIO_STATE_STOPPED;
    snprintf(s_radio.status.station, sizeof(s_radio.status.station), "DB91-TX");
    taskEXIT_CRITICAL(&s_status_lock);
    s_radio.current_station_index = SIZE_MAX;
    radio_load_catalog(&s_radio);
    ESP_RETURN_ON_ERROR(radio_sync_output(&s_radio), TAG, "disable idle I2S output failed");
    s_radio.initialized = true;
    return ESP_OK;
}

esp_err_t internet_radio_start(void)
{
    if (s_radio.catalog.count == 0U) return ESP_ERR_NOT_FOUND;
    return internet_radio_start_station_index(0U) ? ESP_OK : ESP_FAIL;
}

bool internet_radio_start_saved_station(void)
{
    size_t index;
    if (!internet_radio_saved_station_index(&index)) {
        return false;
    }
    return internet_radio_start_station_index(index);
}

bool internet_radio_saved_station_index(size_t *index)
{
    char url[STATION_CATALOG_URL_MAX_LEN];
    return index != NULL && s_radio.initialized && s_radio.catalog.count > 0U &&
           station_resume_load_last_url(STATION_SETTINGS_PATH, url, sizeof(url)) &&
           station_catalog_find_by_url(&s_radio.catalog, url, index);
}

bool internet_radio_start_station_index(size_t index)
{
    if (!s_radio.initialized) return false;
    if (index >= s_radio.catalog.count) return false;
    if (s_radio.direct_task != NULL) {
        const esp_err_t stop_result = internet_radio_stop();
        if (stop_result != ESP_OK) {
            ESP_LOGE(TAG, "cannot start station %u: previous decoder did not stop (%s)",
                     (unsigned int)index, esp_err_to_name(stop_result));
            return false;
        }
    } else {
        internet_radio_status_t previous_status;
        internet_radio_get_status(&previous_status);
        if (previous_status.state != INTERNET_RADIO_STATE_STOPPED) {
            if (internet_radio_stop() != ESP_OK) {
                ESP_LOGE(TAG, "cannot start station %u: previous stream did not stop",
                         (unsigned int)index);
                return false;
            }
        }
    }
    const radio_stream_format_t stream_format =
        radio_stream_format_from_url(s_radio.catalog.entries[index].url);
    if (!radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_START)) return false;
    s_radio.current_station_index = index;
    s_radio.stream_format = stream_format;
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(s_radio.status.station, sizeof(s_radio.status.station), "%.*s",
             (int)sizeof(s_radio.status.station) - 1,
             s_radio.catalog.entries[index].name);
    s_radio.status.title[0] = '\0';
    taskEXIT_CRITICAL(&s_status_lock);
    esp_err_t err = ESP_FAIL;
    for (unsigned int attempt = 0U; attempt < RADIO_HTTP_CONNECT_RETRIES; ++attempt) {
        err = radio_http_open(&s_radio, s_radio.catalog.entries[index].url);
        if (err == ESP_OK) break;
        if (attempt + 1U < RADIO_HTTP_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "HTTP connect retry %u/%u after %s",
                     attempt + 1U, RADIO_HTTP_CONNECT_RETRIES, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (err != ESP_OK) {
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        return false;
    }
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(s_radio.status.codec, sizeof(s_radio.status.codec), "%s",
             radio_stream_format_codec_name(stream_format));
    taskEXIT_CRITICAL(&s_status_lock);
    atomic_store_explicit(&s_radio.direct_stop_requested, false, memory_order_release);
    atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
    if (s_radio.direct_done != NULL) {
        (void)xSemaphoreTake(s_radio.direct_done, 0);
    }
    s_radio.decoder = radio_decoder_create(stream_format);
    if (s_radio.decoder == NULL) {
        ESP_LOGE(TAG, "failed to create %s decoder",
                 radio_stream_format_codec_name(stream_format));
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        radio_http_close(&s_radio);
        return false;
    }
    ESP_LOGI(TAG, "decoder created: internal_free=%u largest_block=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                            MALLOC_CAP_8BIT));
    if (xTaskCreatePinnedToCore(radio_direct_task, "radio_decode", 8192, &s_radio, 6,
                                &s_radio.direct_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "failed to create direct decoder task");
        radio_decoder_destroy(s_radio.decoder);
        s_radio.decoder = NULL;
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        radio_http_close(&s_radio);
        return false;
    }
    (void)station_resume_save_last_url(STATION_SETTINGS_PATH,
                                       s_radio.catalog.entries[index].url);
    return true;
}

esp_err_t internet_radio_stop(void)
{
    if (!s_radio.initialized) return ESP_ERR_INVALID_STATE;
    if (s_radio.direct_task != NULL) {
        atomic_store_explicit(&s_radio.direct_stop_requested, true, memory_order_release);
        atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
        if (s_radio.direct_done == NULL ||
            xSemaphoreTake(s_radio.direct_done, pdMS_TO_TICKS(RADIO_DIRECT_STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "timed out waiting for direct decoder task to stop");
            return ESP_ERR_TIMEOUT;
        }
    }
    (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_STOP);
    const esp_err_t output_result = radio_sync_output(&s_radio);
    radio_http_close(&s_radio);
    s_radio.current_station_index = SIZE_MAX;
    return output_result;
}

esp_err_t internet_radio_pause(void)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (!s_radio.initialized || status.state != INTERNET_RADIO_STATE_PLAYING) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool direct_path = s_radio.direct_task != NULL;
    if (direct_path) {
        atomic_store_explicit(&s_radio.direct_paused, true, memory_order_release);
        xSemaphoreTake(s_radio.direct_io_mutex, portMAX_DELAY);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
    const bool applied = radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_PAUSE);
    const esp_err_t result = applied ? radio_sync_output(&s_radio) : ESP_ERR_INVALID_STATE;
    if (!applied) {
        atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
    }
    if (direct_path) {
        xSemaphoreGive(s_radio.direct_io_mutex);
    }
    return result;
}

esp_err_t internet_radio_resume(void)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (!s_radio.initialized || status.state != INTERNET_RADIO_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool direct_path = s_radio.direct_task != NULL;
    if (direct_path) {
        xSemaphoreTake(s_radio.direct_io_mutex, portMAX_DELAY);
        s_radio.output_started = false;
        const bool applied = radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_RESUME);
        if (applied) {
            atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
        }
        xSemaphoreGive(s_radio.direct_io_mutex);
        return applied ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

void internet_radio_get_status(internet_radio_status_t *status)
{
    if (status == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_radio.status;
    taskEXIT_CRITICAL(&s_status_lock);
}

size_t internet_radio_station_count(void)
{
    return s_radio.catalog.count;
}

const station_catalog_entry_t *internet_radio_station_at(size_t index)
{
    return index < s_radio.catalog.count ? &s_radio.catalog.entries[index] : NULL;
}

size_t internet_radio_current_station_index(void)
{
    return s_radio.current_station_index;
}
