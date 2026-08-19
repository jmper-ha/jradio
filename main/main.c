#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_input.h"
#include "device_clock.h"
#include "internet_radio.h"
#include "player_control.h"
#include "settings_csv.h"
#include "system_report.h"
#include "ui.h"
#include "usb_player.h"
#include "usb_storage.h"
#include "web_server.h"
#include "wifi_provisioning.h"

static const char *TAG = "jradio";

/* A subsystem the device is still usable without.
 *
 * ESP_ERROR_CHECK aborts, and an abort in app_main is a reboot into the same
 * failure: the device loops instead of running. That is the right trade for
 * the display or the player core, which leave nothing behind them, and the
 * wrong one for a drive that did not enumerate or a web server that could not
 * take its socket - losing those costs a feature, while looping costs
 * everything, including the half that still worked. */
static void start_optional(const char *what, esp_err_t result)
{
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "%s unavailable: %s", what, esp_err_to_name(result));
    }
}

static void input_log_task(void *arg)
{
    (void)arg;
    board_input_action_t action;
    while (true) {
        if (board_input_read(&action, portMAX_DELAY)) {
            (void)ui_post_input(action);
        }
    }
}

void app_main(void)
{
    // First, so the reset reason is the first thing in the log after a crash,
    // and before anything allocates, so the boot heap figure means something.
    system_report_boot();
    // Before anything that could write settings: the lock it creates has to
    // exist by the time player_control and ui are running, and app_main is
    // still the only task at this point.
    settings_csv_init();
    // Fatal on purpose: without the board there is no screen, no sound and no
    // controls, and without player_control and the UI there is nothing to
    // drive them with. A reboot loop is at least an honest signal there.
    ESP_ERROR_CHECK(board_init());
    start_optional("Wi-Fi", wifi_provisioning_init());
    start_optional("Wi-Fi", wifi_provisioning_start());
    // After Wi-Fi is up so the first query has somewhere to go, but it does
    // not depend on being connected - SNTP retries on its own and the clock
    // reads unset until an answer arrives.
    device_clock_init();
    start_optional("USB storage", usb_storage_init());
    start_optional("USB player", usb_player_init());
    ESP_ERROR_CHECK(internet_radio_init());
    ESP_ERROR_CHECK(player_control_init());
    ESP_ERROR_CHECK(ui_init());
    start_optional("web server", web_server_start());
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "jradio booted; active audio source=%d",
             (int)snapshot.active_source);
    ESP_ERROR_CHECK(xTaskCreate(input_log_task, "input_log", 3072, NULL, 4, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
