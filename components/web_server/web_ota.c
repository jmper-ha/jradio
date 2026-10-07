#include "web_ota.h"

#ifdef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ota_update.h"

static const char *TAG = "web_ota";

/* One flash sector: esp_ota_write erases as it goes, so a piece this size
 * is one erase and one write. In PSRAM - the HTTP worker's stack and internal
 * RAM are the scarce things, and the flash driver bounces PSRAM data itself. */
#define WEB_OTA_PIECE 4096U

static const char *state_name(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_RECEIVING: return "receiving";
    case OTA_STATE_CONFIRM: return "confirm";
    case OTA_STATE_RESTARTING: return "restarting";
    case OTA_STATE_FAILED: return "failed";
    case OTA_STATE_IDLE: break;
    }
    return "idle";
}

/* Polled by the page while it uploads and while it waits for the press -
 * the same reason the track position is polled rather than pushed. */
esp_err_t web_ota_get(httpd_req_t *request)
{
    ota_status_t status;
    ota_update_get_status(&status);
    const esp_app_desc_t *running = esp_app_get_description();
    char reply[256];
    /* The versions are git describe output and the codes are our own words:
     * nothing in them needs escaping. */
    snprintf(reply, sizeof(reply),
             "{\"state\":\"%s\",\"done\":%u,\"total\":%u,\"version\":\"%s\","
             "\"error\":\"%s\",\"running\":\"%s\",\"slot\":\"%s\"}",
             state_name(status.state), (unsigned)status.done, (unsigned)status.total,
             status.version, status.error != NULL ? status.error : "", running->version,
             ota_update_running_slot());
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, reply);
}

static esp_err_t send_refusal(httpd_req_t *request, const char *status, const char *code)
{
    char reply[64];
    snprintf(reply, sizeof(reply), "{\"error\":\"%s\"}", code);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, reply);
}

/* Reads exactly `want` bytes unless the body ends first. */
static int receive(httpd_req_t *request, uint8_t *into, size_t want)
{
    size_t got = 0U;
    while (got < want) {
        const int read = httpd_req_recv(request, (char *)into + got, want - got);
        if (read == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (read <= 0) return -1;
        got += (size_t)read;
    }
    return (int)got;
}

esp_err_t web_ota_app_post(httpd_req_t *request)
{
    const size_t total = request->content_len;
    if (total < OTA_IMAGE_HEAD_SIZE) return send_refusal(request, "400 Bad Request", "short");
    uint8_t *piece = heap_caps_malloc(WEB_OTA_PIECE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (piece == NULL) piece = malloc(WEB_OTA_PIECE);
    if (piece == NULL) return send_refusal(request, "500 Internal Server Error", "memory");

    /* The head first and on its own: it decides whether anything is written
     * at all, and a refused file costs no erase. */
    esp_err_t result = ESP_OK;
    const char *failure = NULL;
    if (receive(request, piece, OTA_IMAGE_HEAD_SIZE) < 0) {
        failure = "upload";
    } else {
        ota_image_result_t why = OTA_IMAGE_OK;
        result = ota_update_begin(piece, OTA_IMAGE_HEAD_SIZE, total, &why);
        if (why != OTA_IMAGE_OK) {
            free(piece);
            return send_refusal(request, "400 Bad Request", ota_image_result_code(why));
        }
        if (result == ESP_ERR_INVALID_SIZE) {
            free(piece);
            return send_refusal(request, "400 Bad Request", "too_big");
        }
        if (result != ESP_OK) failure = "flash";
    }

    size_t received = OTA_IMAGE_HEAD_SIZE;
    while (failure == NULL && received < total) {
        const size_t want = total - received < WEB_OTA_PIECE ? total - received : WEB_OTA_PIECE;
        if (receive(request, piece, want) < 0) {
            failure = "upload";
        } else if (ota_update_write(piece, want) != ESP_OK) {
            failure = "flash";
        }
        received += want;
    }
    free(piece);
    if (failure != NULL) {
        ESP_LOGW(TAG, "upload stopped at %u of %u bytes: %s", (unsigned)received,
                 (unsigned)total, failure);
        /* begin() already failed on its own and said so. */
        if (result == ESP_OK) ota_update_abort(failure);
        return send_refusal(request, "500 Internal Server Error", failure);
    }
    if (ota_update_finish() != ESP_OK) {
        return send_refusal(request, "400 Bad Request", "verify");
    }
    ota_status_t status;
    ota_update_get_status(&status);
    char reply[64];
    snprintf(reply, sizeof(reply), "{\"version\":\"%s\"}", status.version);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, reply);
}

#endif
