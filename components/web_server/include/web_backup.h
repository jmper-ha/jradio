#pragma once

/* Download and restore of the three files that make a device this device -
 * wifi.json, settings.csv and yandex.json - as one zip or one file at a time.
 * See config_archive.h for the archive itself; everything here is the device
 * side of it: the filesystem, the HTTP body, and the reboot that makes a
 * restored configuration take effect.
 *
 * Device-only: the pure half is in config_archive.c, where the host tests
 * reach it. */

#ifdef ESP_PLATFORM

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_backup_get(httpd_req_t *request);
esp_err_t web_backup_restore_post(httpd_req_t *request);

#endif
