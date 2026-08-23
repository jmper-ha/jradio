#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* OAuth Device Flow against oauth.yandex.ru: the device shows a short code,
 * the user confirms it on a phone, and only then does a token exist. The point
 * of choosing this flow over a password form is that no Yandex password ever
 * reaches the firmware, the LAN web UI (which has no authentication) or the
 * serial log. */

/* Lengths are the buffers the parser will accept, chosen from what the server
 * actually returns plus room to grow: user codes came back 8 characters, the
 * verification URL "https://ya.ru/device", device codes are opaque and long. */
#define YANDEX_AUTH_USER_CODE_MAX 31
#define YANDEX_AUTH_URL_MAX 95
#define YANDEX_AUTH_DEVICE_CODE_MAX 127
#define YANDEX_AUTH_TOKEN_MAX 255
#define YANDEX_AUTH_DEVICE_ID_MAX 15

typedef enum {
    YANDEX_AUTH_IDLE = 0,   /* no token, nothing in progress */
    YANDEX_AUTH_REQUESTING, /* asking the server for a code to show */
    YANDEX_AUTH_WAITING,    /* code on screen, polling for confirmation */
    YANDEX_AUTH_AUTHORIZED, /* a token is stored */
    YANDEX_AUTH_FAILED,     /* the last attempt ended badly; see error */
} yandex_auth_state_t;

typedef enum {
    YANDEX_AUTH_ERROR_NONE = 0,
    YANDEX_AUTH_ERROR_NETWORK, /* could not reach the server at all */
    YANDEX_AUTH_ERROR_TIMEOUT, /* the code expired before it was confirmed */
    YANDEX_AUTH_ERROR_DENIED,  /* the user refused, or the server said no */
    YANDEX_AUTH_ERROR_SERVER,  /* an answer we could not make sense of */
    YANDEX_AUTH_ERROR_STORAGE, /* the token could not be written to flash */
} yandex_auth_error_t;

typedef struct {
    yandex_auth_state_t state;
    yandex_auth_error_t error;
    /* Only meaningful while WAITING; the screen and the web UI show these two
     * together, because the code alone does not say where to type it. */
    char user_code[YANDEX_AUTH_USER_CODE_MAX + 1];
    char verification_url[YANDEX_AUTH_URL_MAX + 1];
    uint32_t seconds_left;
} yandex_auth_status_t;

/* ------------------------------------------------------------------ */
/* Pure flow logic (yandex_auth_flow.c)                                */
/* ------------------------------------------------------------------ */

typedef enum {
    YANDEX_AUTH_ACTION_WAIT = 0,   /* nothing to do yet */
    YANDEX_AUTH_ACTION_POLL_TOKEN, /* ask whether the user confirmed */
    YANDEX_AUTH_ACTION_EXPIRE,     /* the code is dead; stop polling */
} yandex_auth_action_t;

typedef struct {
    yandex_auth_state_t state;
    uint32_t issued_ms;    /* when the server handed out the code */
    uint32_t last_poll_ms; /* when the last poll was started */
    uint32_t interval_ms;  /* server-recommended gap between polls */
    uint32_t lifetime_ms;  /* how long the code stays valid */
    bool poll_in_flight;
} yandex_auth_flow_t;

/* All times are milliseconds from the same monotonic clock and compared
 * wrap-safely, so a device that has been up for 49 days keeps working. */
yandex_auth_action_t yandex_auth_flow_next(const yandex_auth_flow_t *flow, uint32_t now_ms);
uint32_t yandex_auth_flow_seconds_left(const yandex_auth_flow_t *flow, uint32_t now_ms);

/* The server may answer a poll with "slow_down"; OAuth expects the client to
 * widen the gap rather than keep hammering at the old rate. */
uint32_t yandex_auth_flow_backoff(uint32_t interval_ms);

/* Clamps whatever the server suggested into something sane: a zero interval
 * would spin, and an absurd one would make the code expire unused. */
uint32_t yandex_auth_flow_clamp_interval(uint32_t interval_s);
uint32_t yandex_auth_flow_clamp_lifetime(uint32_t lifetime_s);

/* ------------------------------------------------------------------ */
/* Pure response parsing (yandex_auth_json.c)                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char device_code[YANDEX_AUTH_DEVICE_CODE_MAX + 1];
    char user_code[YANDEX_AUTH_USER_CODE_MAX + 1];
    char verification_url[YANDEX_AUTH_URL_MAX + 1];
    uint32_t expires_in_s;
    uint32_t interval_s;
} yandex_auth_device_code_t;

bool yandex_auth_parse_device_code(const char *json, yandex_auth_device_code_t *code);

typedef enum {
    YANDEX_AUTH_TOKEN_OK = 0,
    YANDEX_AUTH_TOKEN_PENDING,   /* the user has not confirmed yet */
    YANDEX_AUTH_TOKEN_SLOW_DOWN, /* polling too fast */
    YANDEX_AUTH_TOKEN_DENIED,
    YANDEX_AUTH_TOKEN_EXPIRED,
    YANDEX_AUTH_TOKEN_INVALID, /* not an answer this client understands */
} yandex_auth_token_result_t;

/* On anything but OK, `token` is left empty - a caller that ignores the result
 * must not end up storing a fragment of an error message as a credential. */
yandex_auth_token_result_t yandex_auth_parse_token(const char *json, char *token,
                                                   size_t token_size);

/* ------------------------------------------------------------------ */
/* Device-side entry points (yandex_auth.c)                            */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t yandex_auth_init(void);

/* Starts a device-flow attempt; returns immediately, progress is visible
 * through yandex_auth_get_status(). Doing nothing if one is already running is
 * deliberate: the screen and the web UI can both ask. */
esp_err_t yandex_auth_begin(void);
esp_err_t yandex_auth_cancel(void);

/* Forgets the stored token. The account keeps the "jRadio" entry in its device
 * list until the user removes it there - we cannot revoke it from here. */
esp_err_t yandex_auth_forget(void);

yandex_auth_status_t yandex_auth_get_status(void);
bool yandex_auth_is_authorized(void);

/* Copies the token out for an API request. Kept out of the status struct so a
 * credential is not carried through every UI poll and every web frame. */
bool yandex_auth_copy_token(char *token, size_t token_size);
#endif
