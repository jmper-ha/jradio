#pragma once

/* Private to the component: the authenticated side of api.music.yandex.net.
 * Everything here needs a token, so nothing here works before yandex_auth has
 * one. */

#include <stddef.h>

#include "esp_err.h"

#define YANDEX_API_HOST "api.music.yandex.net"

/* One GET against the API host, with the OAuth token and the client header
 * attached. `response` receives the body whatever the status - the API says
 * what went wrong in JSON, and a bare status code is not enough to act on.
 *
 * Blocking, and it performs a TLS handshake, so the caller must be a task with
 * the stack for it. Returns ESP_ERR_INVALID_STATE when no account is linked,
 * which is a different thing from the request failing. */
esp_err_t yandex_api_get(const char *path, char *response, size_t response_size,
                         int *status_code);

/* The same, for an absolute URL the API itself handed out - a track's
 * downloadInfoUrl. No token is attached: that URL carries its own signature,
 * and it is over a kilobyte long, which is why it is passed whole instead of
 * being assembled from a path. */
esp_err_t yandex_api_get_url(const char *url, char *response, size_t response_size,
                             int *status_code);
