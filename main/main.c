#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_features.h"
#include "board_input.h"
#include "device_clock.h"
#include "internet_radio.h"
#include "player_control.h"
#include "sd_storage.h"
#include "settings_csv.h"
#include "system_report.h"
#include "ui.h"
#include "file_player.h"
#include "usb_storage.h"
#include "web_server.h"
#include "wifi_provisioning.h"
#include "yandex_auth.h"
#include "yandex_catalog.h"
#include "yandex_feedback.h"
#include "yandex_rotor.h"

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
    /* Each of these belongs to a part or a feature board_options.h can leave
     * out, so each is asked for only when this build has it. A plain `if` on
     * a constant rather than #if: the call still has to compile, which is what
     * catches a signature drifting away from a subsystem nobody currently
     * builds. */
    if (BOARD_HAS_YANDEX_MUSIC) {
        // After Wi-Fi, because that is what mounts LittleFS, and before the UI
        // so the menu already knows whether an account is linked when it first
        // draws.
        start_optional("Yandex Music", yandex_auth_init());
        start_optional("Yandex Music catalog", yandex_catalog_init());
        /* Costs a mutex until a station is actually played; what it buys is
         * that the rotor learns what was listened to, so a station opened
         * tomorrow does not start where it started today. */
        start_optional("Yandex Music feedback", yandex_feedback_init());
    }
    if (BOARD_HAS_SD_CARD) {
        // Before the UI and the radio, deliberately: sd_storage_init()
        // explains what the internal-SRAM heap does if the mount lands after
        // them.
        start_optional("SD card", sd_storage_init());
    }
    if (BOARD_HAS_USB) {
        start_optional("USB storage", usb_storage_init());
    }
    if (BOARD_HAS_USB || BOARD_HAS_SD_CARD) {
        // One player for both volumes, so it is wanted if either is fitted.
        start_optional("file player", file_player_init());
    }
    ESP_ERROR_CHECK(internet_radio_init());
    /* After both exist, and before anything can play: a Yandex station is an
     * HTTP stream that arrives one track at a time, and this is the seam
     * between the two - the audio path asks for the next link, and the rotor
     * is what answers, without either component naming the other. */
    if (BOARD_HAS_YANDEX_MUSIC) {
        internet_radio_set_track_source(yandex_rotor_next_url);
        // The other half of that seam: a pause closes the connection, and this
        // is what signs the same track again so it can be reopened where it
        // stopped rather than abandoned for the next one.
        internet_radio_set_track_reopen(yandex_rotor_current_url);
    }
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
