#include <string.h>

#include "cJSON.h"
#include "yandex_auth.h"

/* Rejects rather than truncates. A clipped device_code would poll forever
 * against a code the server does not know, and a clipped token would be stored
 * as a credential that never works - both failures would look like "Yandex is
 * broken" instead of "the buffer was too small". */
static bool yandex_auth_copy_string(const cJSON *object, const char *name, char *value,
                                    size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const size_t length = strlen(item->valuestring);
    if (length == 0U || length >= capacity) {
        return false;
    }
    memcpy(value, item->valuestring, length + 1U);
    return true;
}

static uint32_t yandex_auth_read_seconds(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 || item->valuedouble > 4294967295.0) {
        /* Zero means "unset" to the clamp helpers, which substitute a default.
         * A missing interval is not worth failing the whole exchange over. */
        return 0U;
    }
    return (uint32_t)item->valuedouble;
}

bool yandex_auth_parse_device_code(const char *json, yandex_auth_device_code_t *code)
{
    if (json == NULL || code == NULL) {
        return false;
    }
    *code = (yandex_auth_device_code_t){0};

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    const bool ok =
        yandex_auth_copy_string(root, "device_code", code->device_code,
                                sizeof(code->device_code)) &&
        yandex_auth_copy_string(root, "user_code", code->user_code, sizeof(code->user_code)) &&
        yandex_auth_copy_string(root, "verification_url", code->verification_url,
                                sizeof(code->verification_url));
    if (ok) {
        code->expires_in_s = yandex_auth_read_seconds(root, "expires_in");
        code->interval_s = yandex_auth_read_seconds(root, "interval");
    } else {
        *code = (yandex_auth_device_code_t){0};
    }
    cJSON_Delete(root);
    return ok;
}

yandex_auth_token_result_t yandex_auth_parse_token(const char *json, char *token,
                                                   size_t token_size)
{
    if (token == NULL || token_size == 0U) {
        return YANDEX_AUTH_TOKEN_INVALID;
    }
    token[0] = '\0';
    if (json == NULL) {
        return YANDEX_AUTH_TOKEN_INVALID;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return YANDEX_AUTH_TOKEN_INVALID;
    }

    yandex_auth_token_result_t result = YANDEX_AUTH_TOKEN_INVALID;
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsString(error) && error->valuestring != NULL) {
        /* The pending answer arrives as HTTP 400 on every poll until the user
         * confirms, so it is the normal case, not a failure. */
        if (strcmp(error->valuestring, "authorization_pending") == 0) {
            result = YANDEX_AUTH_TOKEN_PENDING;
        } else if (strcmp(error->valuestring, "slow_down") == 0) {
            result = YANDEX_AUTH_TOKEN_SLOW_DOWN;
        } else if (strcmp(error->valuestring, "access_denied") == 0) {
            result = YANDEX_AUTH_TOKEN_DENIED;
        } else if (strcmp(error->valuestring, "expired_token") == 0 ||
                   strcmp(error->valuestring, "invalid_grant") == 0) {
            result = YANDEX_AUTH_TOKEN_EXPIRED;
        }
    } else if (yandex_auth_copy_string(root, "access_token", token, token_size)) {
        result = YANDEX_AUTH_TOKEN_OK;
    }

    if (result != YANDEX_AUTH_TOKEN_OK) {
        token[0] = '\0';
    }
    cJSON_Delete(root);
    return result;
}
