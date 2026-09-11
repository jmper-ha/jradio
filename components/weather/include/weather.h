#pragma once

#include <stdbool.h>

#include "device_settings.h"
#include "weather_report.h"

/* The weather on the device: one task that asks the chosen service every
 * quarter of an hour and keeps the last answer for the strip to draw.
 *
 * Nothing here blocks anyone else. The strip polls weather_current() from the
 * UI task on every pass and gets a copy; the web page reads the same copy and
 * the state beside it. The task is woken early when the settings change - a
 * new service or a moved pin should show within seconds, not at the next
 * quarter - and when a key is saved. */

typedef enum {
    /* The provider is off, or a key is wanted and there is none. */
    WEATHER_STATE_OFF = 0,
    WEATHER_STATE_NO_KEY,
    /* Turned on and nothing answered yet - including before the network. */
    WEATHER_STATE_WAITING,
    WEATHER_STATE_OK,
    /* The last request failed; the previous report, if any, is still shown
     * until it is too old to trust. `http_status` says what the service
     * answered, 0 when the request never got that far. */
    WEATHER_STATE_FAILED,
} weather_state_t;

typedef struct {
    weather_state_t state;
    int http_status;
    weather_report_t report;
} weather_status_t;

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Reads the key file and starts the task. `settings` may be NULL when the
 * card did not read; the task then waits for weather_apply(). */
esp_err_t weather_init(const device_settings_t *settings);

/* The provider and the coordinates out of the settings. Called by whoever
 * re-reads settings.csv - the UI task, on its own changes and on the web's. */
void weather_apply(const device_settings_t *settings);

/* A copy of the last report, or false when there is none worth drawing: the
 * weather is off, nothing has answered, or the last answer is older than the
 * service has been unreachable for. */
bool weather_current(weather_report_t *report);
void weather_status(weather_status_t *status);

/* The OpenWeatherMap key. Kept in its own file, out of git and in the backup,
 * for the reason the Yandex token is: settings.csv is a file that gets synced
 * into the repository. An empty key clears the file. Never handed back out -
 * the page is told only whether one is set. */
bool weather_key_is_set(void);
esp_err_t weather_key_save(const char *key);
#endif
