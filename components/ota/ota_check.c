#include "ota_check.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "board_config.h"
#include "board_display_profile.h"
#include "device_settings.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "ota_offer.h"
#include "ota_update.h"
#include "player_control.h"
#include "settings_csv.h"
#include "wifi_provisioning.h"

static const char *TAG = "ota_check";

/* A bench serves its own release from a laptop by defining this in
 * board_options.local.h; every other build asks GitHub. */
#ifndef OTA_MANIFEST_URL
#define OTA_MANIFEST_URL "https://github.com/jmper-ha/jradio/releases/latest/download/ota.json"
#endif

#define OTA_CHECK_FIRST_DELAY_US (5LL * 60 * 1000 * 1000)
#define OTA_CHECK_PERIOD_US (24LL * 60 * 60 * 1000 * 1000)
/* Not on the network when the time came: try again in a while rather than
 * wait a whole day. */
#define OTA_CHECK_RETRY_US (10LL * 60 * 1000 * 1000)
/* ota.json is a few kilobytes; this is room for a release with many more
 * displays and longer notes, and a bound on what a wrong answer can cost. */
#define OTA_MANIFEST_MAX (24U * 1024U)
#define OTA_PIECE 4096U
/* The install downloads over TLS and writes flash, which a task whose stack
 * is in PSRAM may not do - so internal, and only for as long as it runs. A
 * check writes nothing and runs on a PSRAM stack, as the radio's own HTTPS
 * task does: right after a boot, with a station starting, the 8 KB internal
 * piece was not there and the first check said "memory". */
#define OTA_WORKER_STACK 8192
/* GitHub answers a release download with two redirects, the second to a
 * signed URL of several hundred characters - which goes out in the request
 * line, hence the transmit buffer. */
#define OTA_HTTP_BUFFER 2048
#define OTA_HTTP_TX_BUFFER 2048
#define OTA_MAX_REDIRECTS 5

#define KEY_ENABLED "update_check"
#define KEY_SKIPPED "update_skip"

typedef enum { JOB_CHECK, JOB_INSTALL } job_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_check_status_t s_status;
static ota_offer_t s_offer;
static bool s_busy;
static esp_timer_handle_t s_timer;

static void set_error(ota_check_state_t state, const char *error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.error = error;
    portEXIT_CRITICAL(&s_lock);
}

void ota_check_get(ota_check_status_t *status)
{
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

size_t ota_check_copy_notes(bool english, char *out, size_t size)
{
    if (size == 0U) return 0U;
    out[0] = '\0';
    /* The notes are only replaced by the worker, which holds s_busy while it
     * does; a reader that finds it busy gets nothing this time rather than a
     * string freed under it. */
    portENTER_CRITICAL(&s_lock);
    const bool busy = s_busy;
    portEXIT_CRITICAL(&s_lock);
    if (busy) return 0U;
    const char *notes = english ? s_offer.notes_en : s_offer.notes_ru;
    if (notes == NULL) return 0U;
    snprintf(out, size, "%s", notes);
    return strlen(out);
}

static void recompute_available(void)
{
    const char *running = esp_app_get_description()->version;
    portENTER_CRITICAL(&s_lock);
    s_status.available = s_status.latest[0] != '\0' &&
                         ota_offer_is_wanted(running, s_status.latest, s_status.skipped);
    portEXIT_CRITICAL(&s_lock);
}

/* Opens `url` and follows redirects to the body. The caller reads it and
 * cleans the client up. */
static esp_http_client_handle_t open_url(const char *url, int64_t *length, int *status_out)
{
    *status_out = 0;
    const esp_http_client_config_t config = {
        .url = url,
        .buffer_size = OTA_HTTP_BUFFER,
        .buffer_size_tx = OTA_HTTP_TX_BUFFER,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "jradio",
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return NULL;
    for (int redirect = 0; redirect <= OTA_MAX_REDIRECTS; ++redirect) {
        if (esp_http_client_open(client, 0) != ESP_OK) break;
        *length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        *status_out = status;
        if (status == 200) return client;
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            ESP_LOGW(TAG, "HTTP %d", status);
            break;
        }
        int ignored = 0;
        (void)esp_http_client_flush_response(client, &ignored);
        if (esp_http_client_set_redirection(client) != ESP_OK) break;
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return NULL;
}

static const char *own_display(void)
{
    return board_config_display_name(DISPLAY);
}

static void run_check(void)
{
    set_error(OTA_CHECK_RUNNING, NULL);
    int64_t length = 0;
    int status = 0;
    esp_http_client_handle_t client = open_url(OTA_MANIFEST_URL, &length, &status);
    if (client == NULL) {
        /* GitHub answered, and the latest release has no ota.json - one from
         * before updates over the network. Not the network's fault. */
        set_error(OTA_CHECK_FAILED, status == 404 ? "missing" : "network");
        return;
    }
    char *body = heap_caps_malloc(OTA_MANIFEST_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t used = 0U;
    bool too_big = false;
    if (body != NULL) {
        while (true) {
            if (used == OTA_MANIFEST_MAX) {
                too_big = true;
                break;
            }
            const int read = esp_http_client_read(client, body + used, OTA_MANIFEST_MAX - used);
            if (read <= 0) break;
            used += (size_t)read;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (body == NULL || too_big || used == 0U) {
        free(body);
        set_error(OTA_CHECK_FAILED, body == NULL ? "memory" : "malformed");
        return;
    }
    ota_offer_t offer;
    const ota_offer_result_t result = ota_offer_parse(body, used, own_display(), &offer);
    free(body);
    if (result != OTA_OFFER_OK) {
        ESP_LOGW(TAG, "ota.json: %s", ota_offer_result_code(result));
        set_error(OTA_CHECK_FAILED, ota_offer_result_code(result));
        return;
    }
    ota_offer_free(&s_offer);
    s_offer = offer;
    struct timeval now;
    gettimeofday(&now, NULL);
    portENTER_CRITICAL(&s_lock);
    memcpy(s_status.latest, offer.version, sizeof(s_status.latest));
    /* Before 2020 the clock has not been set yet: no time is better than 1970. */
    s_status.checked_at = now.tv_sec > 1577836800 ? (int64_t)now.tv_sec : 0;
    portEXIT_CRITICAL(&s_lock);
    recompute_available();
    set_error(OTA_CHECK_DONE, NULL);
    ESP_LOGI(TAG, "latest release %s; running %s", offer.version,
             esp_app_get_description()->version);
}

/* Streams one file of the offer into the update - the web files into
 * www.new, the firmware into the other slot - checking its size and SHA-256
 * against the manifest. The firmware's first OTA_IMAGE_HEAD_SIZE bytes go to
 * ota_update_begin() whole: it decides on them whether to write at all. */
static const char *download(const ota_offer_file_t *file, bool is_app)
{
    int64_t length = 0;
    int status = 0;
    esp_http_client_handle_t client = open_url(file->url, &length, &status);
    if (client == NULL) return "download";
    if (length >= 0 && (size_t)length != file->size) {
        esp_http_client_cleanup(client);
        return "download";
    }
    uint8_t *piece = heap_caps_malloc(OTA_PIECE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (piece == NULL) {
        esp_http_client_cleanup(client);
        return "memory";
    }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    const char *failure = NULL;
    size_t received = 0U;
    bool begun = false;
    if (!is_app && ota_update_www_begin(file->size) != ESP_OK) failure = "write";
    while (failure == NULL && received < file->size) {
        /* The firmware's head is checked before anything is written, so the
         * first piece is exactly that much. */
        const size_t want = is_app && !begun ? OTA_IMAGE_HEAD_SIZE : OTA_PIECE;
        size_t got = 0U;
        while (got < want && received + got < file->size) {
            const int read = esp_http_client_read(client, (char *)piece + got, want - got);
            if (read <= 0) break;
            got += (size_t)read;
        }
        if (got == 0U) {
            failure = "download";
            break;
        }
        mbedtls_sha256_update(&sha, piece, got);
        if (is_app && !begun) {
            ota_image_result_t why = OTA_IMAGE_OK;
            if (ota_update_begin(piece, got, file->size, &why) != ESP_OK) {
                failure = why != OTA_IMAGE_OK ? ota_image_result_code(why) : "flash";
            }
            begun = true;
        } else if (is_app) {
            if (ota_update_write(piece, got) != ESP_OK) failure = "flash";
        } else if (ota_update_www_write(piece, got) != OTA_TAR_OK) {
            failure = "write";
        }
        received += got;
    }
    free(piece);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (failure == NULL && memcmp(digest, file->sha256, sizeof(digest)) != 0) {
        failure = "checksum";
    }
    if (failure == NULL) {
        if (is_app) {
            if (ota_update_finish(false) != ESP_OK) failure = "verify";
        } else if (ota_update_www_finish(true) != OTA_TAR_OK) {
            failure = "write";
        }
    } else if (is_app) {
        if (begun) ota_update_abort(failure);
    } else {
        ota_update_www_abort();
    }
    return failure;
}

static void run_install(void)
{
    /* The offer as the last check left it; a page that pressed "Update"
     * from an older answer installs what is current. */
    const char *failure = s_offer.version[0] == '\0' ? "no_offer" : NULL;
    if (failure == NULL) failure = download(&s_offer.www, false);
    if (failure == NULL) failure = download(&s_offer.app, true);
    if (failure == NULL && ota_update_install() != ESP_OK) failure = "flash";
    if (failure != NULL) {
        ESP_LOGW(TAG, "update to %s failed: %s", s_offer.version, failure);
        set_error(OTA_CHECK_FAILED, failure);
    }
}

static void worker(void *arg)
{
    const job_t job = (job_t)(intptr_t)arg;
    if (job == JOB_CHECK) {
        run_check();
    } else {
        run_install();
    }
    ESP_LOGI(TAG, "worker done; stack left %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    portENTER_CRITICAL(&s_lock);
    s_busy = false;
    s_status.installing = false;
    portEXIT_CRITICAL(&s_lock);
    if (job == JOB_CHECK) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
}

static esp_err_t start_job(job_t job)
{
    portENTER_CRITICAL(&s_lock);
    const bool busy = s_busy;
    s_busy = true;
    if (!busy && job == JOB_INSTALL) s_status.installing = true;
    portEXIT_CRITICAL(&s_lock);
    if (busy) return ESP_ERR_INVALID_STATE;
    /* A check fits beside a playing stream: measured with MP3 at 320 kbps,
     * 21 KB internal left once this stack was taken, 4.4 KB of the stack
     * used. An install also writes flash and keeps two downloads' worth of
     * TLS going, so the sound is stopped first and the stack is asked for
     * again while the stream lets go of its memory. */
    if (job == JOB_INSTALL) {
        const player_command_t stop = {
            .kind = PLAYER_COMMAND_STOP_SOURCE,
            .source = AUDIO_SOURCE_NONE,
            .item_index = PLAYER_ITEM_NONE,
        };
        (void)player_control_post(&stop);
    }
    BaseType_t created = pdFAIL;
    if (job == JOB_CHECK) {
        created = xTaskCreatePinnedToCoreWithCaps(worker, "ota_check", OTA_WORKER_STACK,
                                                  (void *)(intptr_t)job, 3, NULL, 0,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    for (int attempt = 0; job == JOB_INSTALL && attempt < 15 && created != pdPASS; ++attempt) {
        if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(200));
        created = xTaskCreatePinnedToCore(worker, "ota_install", OTA_WORKER_STACK,
                                          (void *)(intptr_t)job, 3, NULL, 0);
    }
    if (created != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_busy = false;
        s_status.installing = false;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGW(TAG, "no RAM for the %u-byte worker stack", OTA_WORKER_STACK);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void timer_fired(void *arg)
{
    (void)arg;
    ota_check_status_t now;
    ota_check_get(&now);
    const bool online = wifi_provisioning_status().mode == WIFI_PROVISIONING_STA_CONNECTED;
    if (now.enabled && online) (void)start_job(JOB_CHECK);
    (void)esp_timer_start_once(s_timer, now.enabled && !online ? OTA_CHECK_RETRY_US
                                                               : OTA_CHECK_PERIOD_US);
}

esp_err_t ota_check_start(void)
{
    char value[32] = "";
    const bool enabled = !settings_csv_get(DEVICE_SETTINGS_PATH, KEY_ENABLED, value,
                                           sizeof(value)) ||
                         strcmp(value, "0") != 0;
    char skipped[32] = "";
    (void)settings_csv_get(DEVICE_SETTINGS_PATH, KEY_SKIPPED, skipped, sizeof(skipped));
    portENTER_CRITICAL(&s_lock);
    s_status.enabled = enabled;
    memcpy(s_status.skipped, skipped, sizeof(s_status.skipped));
    portEXIT_CRITICAL(&s_lock);
    if (own_display() == NULL) return ESP_ERR_NOT_SUPPORTED;
    const esp_timer_create_args_t args = {.callback = timer_fired, .name = "ota_check"};
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err == ESP_OK) err = esp_timer_start_once(s_timer, OTA_CHECK_FIRST_DELAY_US);
    return err;
}

esp_err_t ota_check_now(void)
{
    return start_job(JOB_CHECK);
}

esp_err_t ota_check_install(void)
{
    ota_check_status_t now;
    ota_check_get(&now);
    if (now.latest[0] == '\0') return ESP_ERR_INVALID_STATE;
    return start_job(JOB_INSTALL);
}

esp_err_t ota_check_set_enabled(bool enabled)
{
    if (!settings_csv_set(DEVICE_SETTINGS_PATH, KEY_ENABLED, enabled ? "1" : "0")) {
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_lock);
    s_status.enabled = enabled;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t ota_check_skip(void)
{
    ota_check_status_t now;
    ota_check_get(&now);
    if (now.latest[0] == '\0') return ESP_ERR_INVALID_STATE;
    if (!settings_csv_set(DEVICE_SETTINGS_PATH, KEY_SKIPPED, now.latest)) return ESP_FAIL;
    portENTER_CRITICAL(&s_lock);
    memcpy(s_status.skipped, now.latest, sizeof(s_status.skipped));
    portEXIT_CRITICAL(&s_lock);
    recompute_available();
    return ESP_OK;
}
