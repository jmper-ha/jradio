#include "yandex_likes.h"

#include <stdio.h>
#include <string.h>

#include "yandex_json_reader.h"

/* Track and account ids are decimal and nothing else. Checked rather than
 * escaped for the reason the feedback builder checks its own: these strings
 * come out of the API's own answers, so anything else means the answer was not
 * the one we think we are reading, and sending a request built from it would
 * only ask the server to interpret our mistake. */
static bool yandex_likes_digits(const char *text)
{
    if (text == NULL || text[0] == '\0') return false;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
    }
    return true;
}

size_t yandex_likes_path(const char *uid, const char *track_id, bool liked, char *out,
                         size_t out_size)
{
    if (out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    if (!yandex_likes_digits(uid) || !yandex_likes_digits(track_id)) return 0U;

    /* Singular to add, plural to remove. Measured, and each endpoint refuses
     * the other's spelling with HTTP 400 - so this asymmetry is the contract
     * and not a slip to be tidied away. */
    const int written = liked
        ? snprintf(out, out_size, "/users/%s/likes/tracks/add?track-id=%s", uid, track_id)
        : snprintf(out, out_size, "/users/%s/likes/tracks/remove?track-ids=%s", uid, track_id);
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

bool yandex_account_parse_uid(const char *json, char *out, size_t out_size)
{
    if (json == NULL || out == NULL || out_size == 0U) return false;
    out[0] = '\0';

    json_reader_t reader = {.cursor = json};
    /* The envelope is optional so a fixture need not carry one, the way the
     * other parsers here accept both shapes. */
    json_reader_t inner = reader;
    if (json_enter_object_key(&inner, "result")) reader = inner;
    if (!json_enter_object_key(&reader, "account")) return false;
    if (!json_enter_object_key(&reader, "uid")) return false;

    /* Read as text rather than through json_read_uint: the uid is a JSON
     * number, but a sixteen-digit one does not fit a uint32_t, and it is only
     * ever put back into a URL. */
    json_skip_whitespace(&reader);
    size_t length = 0U;
    while (reader.cursor[length] >= '0' && reader.cursor[length] <= '9') ++length;
    if (length == 0U || length >= out_size) return false;
    memcpy(out, reader.cursor, length);
    out[length] = '\0';
    return true;
}

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "yandex_api.h"

static const char *TAG = "yandex_likes";

/* Measured at 1825 bytes, and it is the whole account: subscription, plus,
 * e-mail. Read once per boot for the one number in it, and freed straight
 * after - which is why it is not a static buffer. */
#define YANDEX_ACCOUNT_RESPONSE_SIZE 4096U
/* The like answer is 190 bytes and a refusal 280. */
#define YANDEX_LIKES_RESPONSE_SIZE 512U

static char s_uid[YANDEX_UID_MAX + 1U];

static bool yandex_likes_fetch_uid(void)
{
    if (s_uid[0] != '\0') return true;

    char *response = heap_caps_malloc(YANDEX_ACCOUNT_RESPONSE_SIZE,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        response = heap_caps_malloc(YANDEX_ACCOUNT_RESPONSE_SIZE,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (response == NULL) return false;

    int status = 0;
    bool parsed = false;
    const esp_err_t err = yandex_api_get("/account/status", response,
                                         YANDEX_ACCOUNT_RESPONSE_SIZE, &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "account status request failed: %s", esp_err_to_name(err));
    } else if (status != 200) {
        ESP_LOGW(TAG, "account status returned HTTP %d", status);
    } else if (!yandex_account_parse_uid(response, s_uid, sizeof(s_uid))) {
        ESP_LOGW(TAG, "account status carried no account id");
    } else {
        parsed = true;
    }
    heap_caps_free(response);
    return parsed;
}

esp_err_t yandex_likes_set(const char *track_id, bool liked)
{
    if (!yandex_likes_digits(track_id)) return ESP_ERR_INVALID_ARG;
    if (!yandex_likes_fetch_uid()) return ESP_ERR_INVALID_STATE;

    /* uid, track id and the fixed parts, with room to spare: a truncated path
     * would mark a track nobody asked about. */
    char path[96];
    if (yandex_likes_path(s_uid, track_id, liked, path, sizeof(path)) == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[YANDEX_LIKES_RESPONSE_SIZE];
    int status = 0;
    const esp_err_t err = yandex_api_post(path, response, sizeof(response), &status);
    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGW(TAG, "like %s for %s returned HTTP %d", liked ? "add" : "remove", track_id,
                 status);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "track %s %s", track_id, liked ? "liked" : "unliked");
    return ESP_OK;
}

void yandex_likes_forget(void)
{
    s_uid[0] = '\0';
}
#endif
