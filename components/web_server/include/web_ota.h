#pragma once

/* The firmware update from a file: the browser sends the .bin, the device
 * writes it into the slot it is not running from, and the panel asks for the
 * encoder press that installs it. See ota_update.h for the device side.
 *
 * The file goes up as the raw request body, not a multipart form, so the
 * handler can stream it to flash in pieces without parsing boundaries - and
 * the page knows the size before it starts, which the progress needs. */

#ifdef ESP_PLATFORM

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_ota_get(httpd_req_t *request);
esp_err_t web_ota_app_post(httpd_req_t *request);

#endif
