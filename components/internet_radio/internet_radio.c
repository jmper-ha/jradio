#include "internet_radio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "esp_audio_simple_player.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "board.h"
#include "icy_metadata.h"
#include "mp3_stream_info.h"
#include "pcm_diagnostics.h"
#include "radio_http_status.h"
#include "radio_stream_format.h"
#include "station_resume.h"

#define RADIO_HTTP_BUFFER_SIZE 2048
#define RADIO_CATALOG_BUFFER_SIZE 16384
#define RADIO_HTTP_MAX_REDIRECTS 5U

static const char *TAG = "internet_radio";

typedef struct {
    esp_asp_handle_t player;
    esp_http_client_handle_t http;
    icy_metadata_t icy;
    internet_radio_status_t status;
    size_t icy_interval;
    size_t compressed_bytes;
    size_t pcm_bytes;
    bool input_logged;
    bool output_logged;
    bool output_started;
    bool initialized;
    radio_stream_format_t stream_format;
    station_catalog_t catalog;
    size_t current_station_index;
} internet_radio_context_t;

static internet_radio_context_t s_radio;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

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
    for (;;) {
        const int received = esp_http_client_read(radio->http, (char *)buffer, length);
        if (received != 0) {
            return received;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void radio_set_title(void *context, const char *title)
{
    internet_radio_context_t *radio = context;
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(radio->status.title, sizeof(radio->status.title), "%s", title);
    taskEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "ICY title: %s", title);
}

static int radio_input(uint8_t *data, int data_size, void *context)
{
    internet_radio_context_t *radio = context;
    uint8_t input[RADIO_HTTP_BUFFER_SIZE];
    int filled = 0;

    while (filled < data_size) {
        const int remaining = data_size - filled;
        const int requested = remaining < (int)sizeof(input) ? remaining : (int)sizeof(input);
        const int received = radio_http_read_blocking(radio, input, requested);
        if (received <= 0) {
            return filled > 0 ? filled : received;
        }
        size_t audio_length = 0;
        if (icy_metadata_feed(&radio->icy, input, (size_t)received, data + filled, (size_t)remaining,
                              &audio_length) != ICY_METADATA_OK) {
            return -1;
        }
        filled += (int)audio_length;
        bool need_bitrate = false;
        taskENTER_CRITICAL(&s_status_lock);
        need_bitrate = radio->status.bitrate_kbps == 0U;
        taskEXIT_CRITICAL(&s_status_lock);
        if (need_bitrate && radio->stream_format == RADIO_STREAM_FORMAT_MP3) {
            const uint16_t bitrate = mp3_stream_bitrate_kbps(data + filled - audio_length,
                                                               audio_length);
            if (bitrate > 0U) {
                taskENTER_CRITICAL(&s_status_lock);
                if (radio->status.bitrate_kbps == 0U) {
                    radio->status.bitrate_kbps = bitrate;
                    snprintf(radio->status.codec, sizeof(radio->status.codec), "MP3");
                }
                taskEXIT_CRITICAL(&s_status_lock);
                ESP_LOGI(TAG, "stream format: MP3 %u kbps", (unsigned int)bitrate);
            }
        }
    }
    radio->compressed_bytes += (size_t)filled;
    if (!radio->input_logged && filled >= 4) {
        ESP_LOGI(TAG, "%s input started: bytes=%u first=%02x %02x %02x %02x",
                 radio_stream_format_codec_name(radio->stream_format),
                 (unsigned int)radio->compressed_bytes, data[0], data[1], data[2], data[3]);
        radio->input_logged = true;
    }
    return filled;
}

static int radio_output(uint8_t *data, int data_size, void *context)
{
    internet_radio_context_t *radio = context;
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
    if (!radio->output_logged && written > 0U) {
        ESP_LOGI(TAG, "PCM output started: block=%d total=%u peak=%u", data_size,
                 (unsigned int)radio->pcm_bytes, peak);
        radio->output_logged = true;
    }
    return result == ESP_OK && written == (size_t)data_size ? ESP_OK : ESP_FAIL;
}

static int radio_player_event(esp_asp_event_pkt_t *packet, void *context)
{
    internet_radio_context_t *radio = context;
    if (packet->type == ESP_ASP_EVENT_TYPE_STATE && packet->payload != NULL) {
        const esp_asp_state_t state = *(const esp_asp_state_t *)packet->payload;
        if (state == ESP_ASP_STATE_RUNNING) {
            (void)radio_status_apply(radio, INTERNET_RADIO_EVENT_PLAYER_RUNNING);
        } else if (state == ESP_ASP_STATE_ERROR) {
            (void)radio_status_apply(radio, INTERNET_RADIO_EVENT_FATAL);
            (void)radio_sync_output(radio);
        }
    } else if (packet->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO && packet->payload != NULL) {
        const esp_asp_music_info_t *info = packet->payload;
        ESP_LOGI(TAG, "decoded format: rate=%d channels=%u bits=%u bitrate=%d", info->sample_rate,
                 info->channels, info->bits, info->bitrate);
    }
    return 0;
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
        .keep_alive_enable = true,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = radio_http_event,
        .user_data = radio,
    };
    radio->icy_interval = 0;
    radio->compressed_bytes = 0;
    radio->pcm_bytes = 0;
    radio->input_logged = false;
    radio->output_logged = false;
    radio->output_started = false;
    taskENTER_CRITICAL(&s_status_lock);
    radio->status.bitrate_kbps = 0U;
    radio->status.codec[0] = '\0';
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
    const esp_asp_cfg_t config = {
        .in = {.cb = radio_input, .user_ctx = &s_radio},
        .out = {.cb = radio_output, .user_ctx = &s_radio},
        .task_prio = 5,
        .task_stack = 6144,
        .task_core = 1,
        .task_stack_in_ext = true,
    };
    ESP_RETURN_ON_ERROR(esp_audio_simple_player_new((esp_asp_cfg_t *)&config, &s_radio.player), TAG,
                        "create ADF player failed");
    ESP_RETURN_ON_ERROR(esp_audio_simple_player_set_event(s_radio.player, radio_player_event, &s_radio), TAG,
                        "set ADF player event failed");
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
    char url[STATION_CATALOG_URL_MAX_LEN];
    size_t index;
    if (!s_radio.initialized || s_radio.catalog.count == 0U ||
        !station_resume_load_last_url(STATION_SETTINGS_PATH, url, sizeof(url)) ||
        !station_catalog_find_by_url(&s_radio.catalog, url, &index)) {
        return false;
    }
    return internet_radio_start_station_index(index);
}

bool internet_radio_start_station_index(size_t index)
{
    if (!s_radio.initialized) return false;
    if (index >= s_radio.catalog.count) return false;
    const radio_stream_format_t stream_format =
        radio_stream_format_from_url(s_radio.catalog.entries[index].url);
    if (!radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_START)) return false;
    s_radio.current_station_index = index;
    s_radio.stream_format = stream_format;
    esp_err_t err = radio_http_open(&s_radio, s_radio.catalog.entries[index].url);
    if (err != ESP_OK) {
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        return false;
    }
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(s_radio.status.codec, sizeof(s_radio.status.codec), "%s",
             radio_stream_format_codec_name(stream_format));
    taskEXIT_CRITICAL(&s_status_lock);
    if (esp_audio_simple_player_run(s_radio.player, radio_stream_format_raw_uri(stream_format), NULL) !=
        ESP_GMF_ERR_OK) {
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
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (status.state != INTERNET_RADIO_STATE_STOPPED) (void)esp_audio_simple_player_stop(s_radio.player);
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
    if (esp_audio_simple_player_pause(s_radio.player) != ESP_GMF_ERR_OK) {
        return ESP_FAIL;
    }
    (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_PAUSE);
    return radio_sync_output(&s_radio);
}

esp_err_t internet_radio_resume(void)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (!s_radio.initialized || status.state != INTERNET_RADIO_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_radio.output_started = false;
    if (esp_audio_simple_player_resume(s_radio.player) != ESP_GMF_ERR_OK) {
        return ESP_FAIL;
    }
    (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_RESUME);
    return ESP_OK;
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
