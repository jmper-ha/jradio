#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yandex_auth.h"

/* The OAuth token is a password equivalent: it grants full access to the
 * account's music library until the user revokes the device. It lives in the
 * same LittleFS partition as wifi.json, is gitignored the same way, and is
 * never logged or sent over the LAN interface. */
typedef struct {
    char token[YANDEX_AUTH_TOKEN_MAX + 1];
    /* Stable across re-authorisations on purpose: reusing it makes Yandex
     * update the existing "jRadio" entry in the account's device list instead
     * of adding a new one every time. */
    char device_id[YANDEX_AUTH_DEVICE_ID_MAX + 1];
} yandex_token_t;

bool yandex_token_is_valid(const char *token);
bool yandex_token_device_id_is_valid(const char *device_id);

/* Takes the random source as an argument so the host test can supply a
 * deterministic one; on the device this is esp_random. */
void yandex_token_generate_device_id(char *device_id, size_t capacity,
                                     uint32_t (*next_random)(void));

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t yandex_token_store_init(void);
/* An absent file is not an error: it is the normal state before the first
 * authorisation, and the caller gets an empty token. */
esp_err_t yandex_token_store_load(yandex_token_t *token);
esp_err_t yandex_token_store_save(const yandex_token_t *token);
esp_err_t yandex_token_store_clear(void);
#endif
