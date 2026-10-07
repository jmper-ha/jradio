#include "web_ota.h"

#ifdef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ota_check.h"
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

/* The change list goes out in both languages: the page switches language
 * without asking again. Each is a few hundred bytes in a release. */
#define WEB_OTA_NOTES_MAX 4096U

static const char *check_state_name(ota_check_state_t state)
{
    switch (state) {
    case OTA_CHECK_RUNNING: return "checking";
    case OTA_CHECK_DONE: return "done";
    case OTA_CHECK_FAILED: return "failed";
    case OTA_CHECK_IDLE: break;
    }
    return "idle";
}

/* Polled by the page while it uploads and while it waits for the press -
 * the same reason the track position is polled rather than pushed - and
 * read once by every page for the notice of a new release. */
esp_err_t web_ota_get(httpd_req_t *request)
{
    ota_status_t status;
    ota_update_get_status(&status);
    ota_check_status_t check;
    ota_check_get(&check);
    cJSON *root = cJSON_CreateObject();
    cJSON *offer = root == NULL ? NULL : cJSON_AddObjectToObject(root, "check");
    char *notes = heap_caps_malloc(WEB_OTA_NOTES_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (root == NULL || offer == NULL || notes == NULL) {
        cJSON_Delete(root);
        free(notes);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "state", state_name(status.state));
    cJSON_AddNumberToObject(root, "done", (double)status.done);
    cJSON_AddNumberToObject(root, "total", (double)status.total);
    cJSON_AddStringToObject(root, "version", status.version);
    cJSON_AddStringToObject(root, "error", status.error != NULL ? status.error : "");
    cJSON_AddStringToObject(root, "running", esp_app_get_description()->version);
    cJSON_AddStringToObject(root, "slot", ota_update_running_slot());

    cJSON_AddBoolToObject(offer, "enabled", check.enabled);
    cJSON_AddStringToObject(offer, "state", check_state_name(check.state));
    cJSON_AddStringToObject(offer, "error", check.error != NULL ? check.error : "");
    cJSON_AddBoolToObject(offer, "installing", check.installing);
    cJSON_AddStringToObject(offer, "latest", check.latest);
    cJSON_AddBoolToObject(offer, "available", check.available);
    cJSON_AddStringToObject(offer, "skipped", check.skipped);
    cJSON_AddNumberToObject(offer, "checked_at", (double)check.checked_at);
    (void)ota_check_copy_notes(false, notes, WEB_OTA_NOTES_MAX);
    cJSON_AddStringToObject(offer, "notes_ru", notes);
    (void)ota_check_copy_notes(true, notes, WEB_OTA_NOTES_MAX);
    cJSON_AddStringToObject(offer, "notes_en", notes);
    free(notes);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t sent = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return sent;
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
    if (ota_update_finish(true) != ESP_OK) {
        return send_refusal(request, "400 Bad Request", "verify");
    }
    ota_status_t status;
    ota_update_get_status(&status);
    char reply[64];
    snprintf(reply, sizeof(reply), "{\"version\":\"%s\"}", status.version);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, reply);
}

/* www.tar, before the firmware. "?app=1" says a firmware follows, so the
 * panel asks once, after both. */
esp_err_t web_ota_www_post(httpd_req_t *request)
{
    const size_t total = request->content_len;
    char query[16];
    char value[4] = "";
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        (void)httpd_query_key_value(query, "app", value, sizeof(value));
    }
    const bool app_follows = strcmp(value, "1") == 0;
    if (total == 0U) return send_refusal(request, "400 Bad Request", "not_web");
    uint8_t *piece = heap_caps_malloc(WEB_OTA_PIECE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (piece == NULL) piece = malloc(WEB_OTA_PIECE);
    if (piece == NULL) return send_refusal(request, "500 Internal Server Error", "memory");
    if (ota_update_www_begin(total) != ESP_OK) {
        free(piece);
        return send_refusal(request, "500 Internal Server Error", "write");
    }
    size_t received = 0U;
    ota_tar_result_t result = OTA_TAR_OK;
    bool cut = false;
    while (result == OTA_TAR_OK && received < total) {
        const size_t want = total - received < WEB_OTA_PIECE ? total - received : WEB_OTA_PIECE;
        if (receive(request, piece, want) < 0) {
            cut = true;
            break;
        }
        result = ota_update_www_write(piece, want);
        received += want;
    }
    free(piece);
    if (cut) {
        ESP_LOGW(TAG, "web files stopped at %u of %u bytes", (unsigned)received, (unsigned)total);
        ota_update_www_abort();
        return send_refusal(request, "500 Internal Server Error", "upload");
    }
    if (result == OTA_TAR_OK) result = ota_update_www_finish(app_follows);
    else ota_update_www_abort();
    if (result != OTA_TAR_OK) {
        return send_refusal(request, "400 Bad Request", ota_tar_result_code(result));
    }
    ota_status_t status;
    ota_update_get_status(&status);
    char reply[64];
    snprintf(reply, sizeof(reply), "{\"version\":\"%s\"}", status.version);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, reply);
}

static esp_err_t send_ok(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

/* The page's buttons for the release: check now, install it, stay quiet
 * about it, and whether to look at all. One handler, the action in the
 * query, so the four cost one of the server's handler slots. */
esp_err_t web_ota_action_post(httpd_req_t *request)
{
    char query[48];
    char action[16] = "";
    char value[4] = "";
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        (void)httpd_query_key_value(query, "do", action, sizeof(action));
        (void)httpd_query_key_value(query, "on", value, sizeof(value));
    }
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (strcmp(action, "check") == 0) {
        result = ota_check_now();
    } else if (strcmp(action, "install") == 0) {
        result = ota_check_install();
    } else if (strcmp(action, "skip") == 0) {
        result = ota_check_skip();
    } else if (strcmp(action, "auto") == 0 && (value[0] == '0' || value[0] == '1')) {
        result = ota_check_set_enabled(value[0] == '1');
    }
    if (result == ESP_OK) return send_ok(request);
    if (result == ESP_ERR_INVALID_ARG) return send_refusal(request, "400 Bad Request", "action");
    if (result == ESP_ERR_INVALID_STATE) return send_refusal(request, "409 Conflict", "busy");
    if (result == ESP_ERR_NO_MEM) return send_refusal(request, "503 Service Unavailable", "memory");
    return send_refusal(request, "500 Internal Server Error", "write");
}

#endif
