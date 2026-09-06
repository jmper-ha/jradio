#include "dlna_client.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "dlna_soap.h"
#include "dlna_xml.h"

static const char *TAG = "dlna_client";

/* Room for a page of a listing.
 *
 * Sized from the real library on this LAN rather than from a guess: the most
 * expensive rows there are artist containers, and they are not uniform - four
 * of them came to 14.9 KB and twelve to 38.6 KB, with the later ones costing
 * more than the earlier. A page of DLNA_SOAP_BROWSE_PAGE of those is around
 * 55 KB, and this leaves room above it for a server that says more about an
 * object than this one does.
 *
 * It is still only a buffer, not a guarantee - which is why filling it is
 * reported rather than passed off as an empty container. See `truncated`
 * below. */
#define DLNA_CLIENT_RESPONSE_MAX (64U * 1024U)

/* A description is a few kilobytes at most; this one is Plex's at about 3 KB
 * with its icon list. Read into the same buffer as a listing, since the two
 * never overlap - a description is fetched before anything is browsed. */

/* Long enough for a server that is asleep on a spinning disk and short enough
 * that a server which has gone away does not hold the browser for a minute. */
#define DLNA_CLIENT_TIMEOUT_MS 10000

static char *s_response;
static SemaphoreHandle_t s_lock;

esp_err_t dlna_client_open(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }
    if (s_response != NULL) return ESP_OK;

    /* PSRAM by preference like every other buffer this size. The internal
     * fallback is kept for consistency with the rest of the firmware, but a
     * board without PSRAM has no 32 KB of internal SRAM to spare and will fail
     * here rather than somewhere less obvious. */
    s_response = heap_caps_malloc(DLNA_CLIENT_RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (s_response == NULL) {
        s_response = heap_caps_malloc(DLNA_CLIENT_RESPONSE_MAX, MALLOC_CAP_INTERNAL);
    }
    if (s_response == NULL) {
        ESP_LOGE(TAG, "no memory for a %u byte response buffer",
                 (unsigned)DLNA_CLIENT_RESPONSE_MAX);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void dlna_client_close(void)
{
    if (s_lock != NULL) xSemaphoreTake(s_lock, portMAX_DELAY);
    free(s_response);
    s_response = NULL;
    if (s_lock != NULL) xSemaphoreGive(s_lock);
}

/* One request, into the shared buffer. `length` receives what was read.
 *
 * The buffer is shared and the callers are not: the browser screen asks from
 * the UI task while the player asks for the next track from the radio task, so
 * a request holds the lock for its whole duration. That serialises two browses
 * onto one server, which is what a server would do with them anyway. */
static esp_err_t request(const char *url, const char *soap_action, const char *body,
                         char *out, size_t out_size, size_t *length, bool *truncated)
{
    *length = 0U;
    if (truncated != NULL) *truncated = false;

    const esp_http_client_config_t config = {
        .url = url,
        .method = body != NULL ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = DLNA_CLIENT_TIMEOUT_MS,
        .user_agent = "jRadio/1.0 UPnP/1.0 DLNADOC/1.50",
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = ESP_OK;
    if (soap_action != NULL) {
        err = esp_http_client_set_header(client, "SOAPAction", soap_action);
        if (err == ESP_OK) {
            err = esp_http_client_set_header(client, "Content-Type", DLNA_SOAP_CONTENT_TYPE);
        }
    }

    const size_t body_length = body != NULL ? strlen(body) : 0U;
    if (err == ESP_OK) err = esp_http_client_open(client, (int)body_length);
    if (err == ESP_OK && body_length > 0U &&
        esp_http_client_write(client, body, body_length) != (int)body_length) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) err = ESP_FAIL;

    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        const int read = esp_http_client_read_response(client, out, (int)out_size - 1);
        if (read < 0) {
            err = ESP_FAIL;
        } else {
            out[read] = '\0';
            *length = (size_t)read;
            /* Filled to the brim, so the end of it is missing. Worth telling
             * apart from a short answer: the document then has no closing tag,
             * which would otherwise be indistinguishable from a server that
             * sent nothing usable - and the two need opposite responses. */
            if (truncated != NULL && *length + 1U >= out_size) {
                *truncated = true;
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request to %s failed: %s", url, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        /* A SOAP fault comes back as 500 with a body worth reading, so the
         * body is kept and the caller decides. Anything else is not something
         * this device can act on. */
        ESP_LOGW(TAG, "%s answered %d", url, status);
        if (status != 500) return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t dlna_client_fetch_description(const char *location, dlna_device_t *out)
{
    if (location == NULL || out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_response == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t length = 0U;
    esp_err_t err = request(location, NULL, NULL, s_response, DLNA_CLIENT_RESPONSE_MAX,
                            &length, NULL);
    if (err == ESP_OK && !dlna_device_parse_description(s_response, length, location, out)) {
        /* It answered, and it is not a server this device can browse. Its own
         * name comes back inside `out`, so the log can say which one. */
        ESP_LOGW(TAG, "%s has no content directory (%s)", location,
                 out->friendly_name[0] != '\0' ? out->friendly_name : "unnamed");
        err = ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "server '%s' browses at %s", out->friendly_name, out->control_url);
    }
    return err;
}

esp_err_t dlna_client_browse(const char *control_url, const char *object_id,
                             size_t starting_index, size_t requested_count,
                             dlna_entry_t *entries, size_t capacity,
                             dlna_client_page_t *page)
{
    if (control_url == NULL || object_id == NULL || page == NULL) return ESP_ERR_INVALID_ARG;
    if (s_response == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    if (requested_count == 0U) return ESP_ERR_INVALID_ARG;
    memset(page, 0, sizeof(*page));

    char body[1024];
    if (dlna_soap_build_browse(body, sizeof(body), object_id, starting_index,
                               requested_count) == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t length = 0U;
    bool truncated = false;
    /* Timed because the wait is the thing being complained about and a guess
     * about where it goes is worthless: the server answers a page in about
     * 100 ms from a PC on the same network, so anything much above that is
     * ours - the connection, the Wi-Fi, the parse. */
    const int64_t started_us = esp_timer_get_time();
    esp_err_t err = request(control_url, DLNA_SOAP_ACTION_BROWSE, body, s_response,
                            DLNA_CLIENT_RESPONSE_MAX, &length, &truncated);
    const int64_t fetched_us = esp_timer_get_time();

    if (err == ESP_OK && truncated) {
        ESP_LOGI(TAG, "'%s' x%u filled the %u byte buffer; asking for fewer",
                 object_id, (unsigned)requested_count,
                 (unsigned)DLNA_CLIENT_RESPONSE_MAX);
        err = ESP_ERR_INVALID_SIZE;
    }

    if (err == ESP_OK) {
        dlna_soap_browse_t envelope;
        if (!dlna_soap_parse_browse(s_response, length, &envelope)) {
            const int fault = dlna_soap_fault_code(s_response, length);
            if (fault > 0) {
                /* 701 is a stale id after the server re-scanned its library,
                 * and the way out of it is to browse from the root again. */
                ESP_LOGW(TAG, "browse of '%s' refused, UPnP error %d", object_id, fault);
                err = fault == 701 ? ESP_ERR_NOT_FOUND : ESP_FAIL;
            } else {
                ESP_LOGW(TAG, "browse of '%s' answered %u bytes with no listing in them",
                         object_id, (unsigned)length);
                err = ESP_FAIL;
            }
        } else {
            /* The listing arrives escaped inside a field of the reply, and is
             * unescaped where it lies: an entity never expands to more bytes
             * than it occupies, so this needs no second 22 KB buffer. */
            char *payload = s_response + envelope.result_offset;
            const size_t unescaped = dlna_xml_unescape(payload, envelope.result_length,
                                                       payload, envelope.result_length + 1U);
            size_t seen = 0U;
            page->count = dlna_didl_parse(payload, unescaped, entries, capacity, &seen);
            page->total_matches = envelope.total_matches;
            ESP_LOGI(TAG, "browse x%u: %u bytes in %lld ms, parsed in %lld ms, %u rows",
                     (unsigned)requested_count, (unsigned)length,
                     (long long)((fetched_us - started_us) / 1000),
                     (long long)((esp_timer_get_time() - fetched_us) / 1000),
                     (unsigned)page->count);
            if (seen > page->count) {
                /* More in the answer than the caller had room for. Not an
                 * error - the next page starts where this one stopped - but
                 * worth saying, because it is also what a capacity chosen
                 * smaller than the request looks like. */
                ESP_LOGD(TAG, "'%s' returned %u rows, kept %u", object_id,
                         (unsigned)seen, (unsigned)page->count);
            }
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t dlna_client_fetch_image(const char *url, uint8_t *buffer, size_t capacity,
                                  size_t *length)
{
    if (url == NULL || buffer == NULL || capacity == 0U || length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *length = 0U;

    /* The same lock as a browse, though the buffer is the caller's: it keeps
     * the device to one conversation with the server at a time, which is one
     * socket and one set of buffers rather than two. A cover is small and the
     * wait is short. */
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool truncated = false;
    const esp_err_t err = request(url, NULL, NULL, (char *)buffer, capacity, length,
                                  &truncated);
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) return err;
    if (truncated) {
        /* A picture bigger than the room for it. Refused whole rather than
         * handed over cut: half a JPEG decodes to nothing useful, and the tile
         * is better empty than wrong. */
        ESP_LOGI(TAG, "cover at %s is larger than %u bytes", url, (unsigned)capacity);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}
