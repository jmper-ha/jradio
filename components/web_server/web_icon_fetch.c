#include "web_icon_fetch.h"

#include <string.h>

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool web_icon_fetch_decode(const char *in, char *out, size_t out_size)
{
    if (in == NULL || out == NULL || out_size == 0U) return false;
    size_t used = 0U;
    for (size_t at = 0U; in[at] != '\0'; ++at) {
        char c = in[at];
        if (c == '%') {
            const int high = hex_value(in[at + 1U]);
            const int low = high < 0 ? -1 : hex_value(in[at + 2U]);
            if (low < 0) return false;
            c = (char)((high << 4) | low);
            at += 2U;
        } else if (c == '+') {
            c = ' ';
        }
        if (used + 1U >= out_size) return false;
        out[used++] = c;
    }
    out[used] = '\0';
    return true;
}

bool web_icon_fetch_url_ok(const char *url)
{
    if (url == NULL) return false;
    const size_t length = strlen(url);
    if (length >= WEB_ICON_FETCH_URL_MAX) return false;
    size_t host_at;
    if (strncmp(url, "https://", 8U) == 0) {
        host_at = 8U;
    } else if (strncmp(url, "http://", 7U) == 0) {
        host_at = 7U;
    } else {
        return false;
    }
    /* A host, however short: "http:///x" and "http://?" name nothing. */
    if (url[host_at] == '\0' || url[host_at] == '/' || url[host_at] == '?' ||
        url[host_at] == '#') {
        return false;
    }
    /* Printable ASCII only: a space or a control byte in an address is either
     * a decoding mistake or a request line being smuggled into the client's. */
    for (size_t at = 0U; at < length; ++at) {
        const unsigned char c = (unsigned char)url[at];
        if (c <= 0x20U || c >= 0x7fU) return false;
    }
    return true;
}

static bool starts_with(const uint8_t *bytes, size_t length, const char *prefix, size_t size)
{
    return length >= size && memcmp(bytes, prefix, size) == 0;
}

const char *web_icon_fetch_sniff(const uint8_t *bytes, size_t length)
{
    if (bytes == NULL) return NULL;
    if (starts_with(bytes, length, "\x89PNG\r\n\x1a\n", 8U)) return "image/png";
    if (starts_with(bytes, length, "\xff\xd8\xff", 3U)) return "image/jpeg";
    if (starts_with(bytes, length, "GIF87a", 6U) || starts_with(bytes, length, "GIF89a", 6U)) {
        return "image/gif";
    }
    if (length >= 12U && memcmp(bytes, "RIFF", 4U) == 0 && memcmp(bytes + 8, "WEBP", 4U) == 0) {
        return "image/webp";
    }
    // ICONDIR: reserved 0, type 1, and at least one image.
    if (length >= 6U && bytes[0] == 0U && bytes[1] == 0U && bytes[2] == 1U && bytes[3] == 0U &&
        (bytes[4] != 0U || bytes[5] != 0U)) {
        return "image/x-icon";
    }
    /* SVG is text, so it is looked for past any byte-order mark, white space
     * and XML declaration or comment - in the first kilobyte, where a logo
     * says what it is. A page of HTML has no <svg before its own content. */
    size_t at = 0U;
    if (starts_with(bytes, length, "\xef\xbb\xbf", 3U)) at = 3U;
    while (at < length && (bytes[at] == ' ' || bytes[at] == '\t' || bytes[at] == '\r' ||
                           bytes[at] == '\n')) {
        ++at;
    }
    if (at < length && bytes[at] == '<') {
        const size_t window = length - at < 1024U ? length - at : 1024U;
        for (size_t offset = 0U; offset + 4U <= window; ++offset) {
            const uint8_t *here = bytes + at + offset;
            if (memcmp(here, "<svg", 4U) == 0) return "image/svg+xml";
            if (offset + 5U <= window && (memcmp(here, "<html", 5U) == 0 ||
                                          memcmp(here, "<HTML", 5U) == 0)) {
                return NULL;
            }
        }
    }
    return NULL;
}

#ifdef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_icon_fetch";

/* The TLS handshake cannot run on the HTTP worker's 8 KB stack beside a
 * handler's frames, so the fetch has a task of its own - with its stack in
 * PSRAM, as the update check's has: it reads no flash through the SPI driver,
 * only the certificate bundle through the cache. Measured: 3.8 KB of the 8
 * used. */
#define WEB_ICON_FETCH_STACK 8192
#define WEB_ICON_FETCH_TIMEOUT_MS 10000
#define WEB_ICON_FETCH_REDIRECTS 5

typedef enum {
    FETCH_OK,
    FETCH_FAILED,
    FETCH_TOO_LARGE,
} fetch_result_t;

typedef struct {
    char url[WEB_ICON_FETCH_URL_MAX];
    uint8_t *body;
    size_t length;
    fetch_result_t result;
    int http_status;
    bool done;
} fetch_job_t;

/* One job, asked after rather than waited for.
 *
 * The first version held the request open until the picture was in, and the
 * HTTP server has a single worker: a 1.37 MB favicon took fourteen seconds at
 * the hundred-odd kilobytes a second TLS gets beside a playing stream, and for
 * all of them no other page was answered and no WebSocket frame went out -
 * "ctrl socket queue full" in the log, the player page frozen on every
 * browser. Now the first request starts the fetch and is answered 202 at once,
 * the page asks again every half second, and the answer that finds the job
 * done carries the picture and frees it. A finished job nobody came back for
 * is freed by the next request for another address. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static fetch_job_t *s_job;

static void job_free(fetch_job_t *job)
{
    if (job == NULL) return;
    heap_caps_free(job->body);
    heap_caps_free(job);
}

static fetch_result_t fetch(fetch_job_t *job)
{
    const esp_http_client_config_t config = {
        .url = job->url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = WEB_ICON_FETCH_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* Some sites answer an unnamed client with a 403 and a browser with
         * the picture; this is neither, and says what it is. */
        .user_agent = "jRadio (station logo)",
        .keep_alive_enable = false,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return FETCH_FAILED;
    fetch_result_t result = FETCH_FAILED;
    /* Redirects by hand: the open/read calls that let the body be read in
     * pieces do not follow them, and a favicon behind http -> https or a CDN
     * is the usual case rather than the odd one. */
    for (int hop = 0; hop <= WEB_ICON_FETCH_REDIRECTS; ++hop) {
        if (esp_http_client_open(client, 0) != ESP_OK) break;
        const int64_t declared = esp_http_client_fetch_headers(client);
        job->http_status = esp_http_client_get_status_code(client);
        const int status = job->http_status;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            (void)esp_http_client_flush_response(client, NULL);
            esp_http_client_close(client);
            if (esp_http_client_set_redirection(client) != ESP_OK) break;
            continue;
        }
        if (status != 200) break;
        if (declared > (int64_t)WEB_ICON_FETCH_BODY_MAX) {
            result = FETCH_TOO_LARGE;
            break;
        }
        result = FETCH_OK;
        while (true) {
            if (job->length == WEB_ICON_FETCH_BODY_MAX) {
                // Full; one byte more says whether it was the whole picture.
                char probe;
                if (esp_http_client_read(client, &probe, 1) > 0) result = FETCH_TOO_LARGE;
                break;
            }
            const int read = esp_http_client_read(client, (char *)job->body + job->length,
                                                  (int)(WEB_ICON_FETCH_BODY_MAX - job->length));
            if (read < 0) {
                result = FETCH_FAILED;
                break;
            }
            if (read == 0) break;
            job->length += (size_t)read;
        }
        break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

static void fetch_task(void *argument)
{
    fetch_job_t *job = argument;
    const fetch_result_t result = fetch(job);
    ESP_LOGI(TAG, "%s: HTTP %d, %u bytes, stack least free %u", job->url, job->http_status,
             (unsigned)job->length, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    /* Down to what came, so a job left waiting for its page holds the
     * picture rather than the two megabytes it might have been. */
    if (result != FETCH_OK || job->length == 0U) {
        heap_caps_free(job->body);
        job->body = NULL;
        job->length = 0U;
    } else {
        uint8_t *smaller = heap_caps_realloc(job->body, job->length,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (smaller != NULL) job->body = smaller;
    }
    portENTER_CRITICAL(&s_lock);
    job->result = result;
    job->done = true;
    portEXIT_CRITICAL(&s_lock);
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t reply(httpd_req_t *request, const char *status, const char *text)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, text);
}

static esp_err_t send_result(httpd_req_t *request, fetch_job_t *job)
{
    const char *type = job->result == FETCH_OK ? web_icon_fetch_sniff(job->body, job->length) : NULL;
    if (job->result == FETCH_TOO_LARGE) return reply(request, "413 Payload Too Large", "too large");
    if (job->result != FETCH_OK) {
        char text[32];
        snprintf(text, sizeof(text), "fetch failed (%d)", job->http_status);
        return reply(request, "502 Bad Gateway", text);
    }
    if (type == NULL) return reply(request, "415 Unsupported Media Type", "not a picture");
    httpd_resp_set_type(request, type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, (const char *)job->body, (ssize_t)job->length);
}

esp_err_t web_icon_fetch_get(httpd_req_t *request)
{
    /* Static: an escaped address can be three times the length of the
     * decoded one, and the HTTP worker is single, so nothing else uses it. */
    static char query[WEB_ICON_FETCH_URL_MAX * 3U + 8U];
    static char raw[WEB_ICON_FETCH_URL_MAX * 3U];
    static char url[WEB_ICON_FETCH_URL_MAX];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "url", raw, sizeof(raw)) != ESP_OK ||
        !web_icon_fetch_decode(raw, url, sizeof(url)) || !web_icon_fetch_url_ok(url)) {
        return reply(request, "400 Bad Request", "bad url");
    }

    portENTER_CRITICAL(&s_lock);
    fetch_job_t *job = s_job;
    const bool same = job != NULL && strcmp(job->url, url) == 0;
    const bool done = job != NULL && job->done;
    // A finished job is handed over here, so only this request frees it.
    if (done) s_job = NULL;
    portEXIT_CRITICAL(&s_lock);

    if (same && !done) return reply(request, "202 Accepted", "pending");
    if (same && done) {
        const esp_err_t sent = send_result(request, job);
        job_free(job);
        return sent;
    }
    if (job != NULL && !done) return reply(request, "409 Conflict", "busy");
    // Another address's leftover, finished and never collected.
    job_free(job);

    job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (job != NULL) {
        snprintf(job->url, sizeof(job->url), "%s", url);
        job->body = heap_caps_malloc(WEB_ICON_FETCH_BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (job == NULL || job->body == NULL) {
        job_free(job);
        return reply(request, "503 Service Unavailable", "no memory");
    }
    portENTER_CRITICAL(&s_lock);
    s_job = job;
    portEXIT_CRITICAL(&s_lock);
    if (xTaskCreatePinnedToCoreWithCaps(fetch_task, "icon_fetch", WEB_ICON_FETCH_STACK, job, 3,
                                        NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_job = NULL;
        portEXIT_CRITICAL(&s_lock);
        job_free(job);
        return reply(request, "503 Service Unavailable", "no memory");
    }
    return reply(request, "202 Accepted", "pending");
}

#endif
