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
#include "ui.h"
#include "usb_player.h"
#include "usb_storage.h"
#include "web_server.h"
#include "wifi_provisioning.h"

static const char *TAG = "jradio";

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
    // Before anything that could write settings: the lock it creates has to
    // exist by the time player_control and ui are running, and app_main is
    // still the only task at this point.
    settings_csv_init();
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(wifi_provisioning_init());
    ESP_ERROR_CHECK(wifi_provisioning_start());
    // After Wi-Fi is up so the first query has somewhere to go, but it does
    // not depend on being connected - SNTP retries on its own and the clock
    // reads unset until an answer arrives.
    device_clock_init();
    ESP_ERROR_CHECK(usb_storage_init());
    ESP_ERROR_CHECK(usb_player_init());
    ESP_ERROR_CHECK(internet_radio_init());
    ESP_ERROR_CHECK(player_control_init());
    ESP_ERROR_CHECK(ui_init());
    ESP_ERROR_CHECK(web_server_start());
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "jradio booted; active audio source=%d",
             (int)snapshot.active_source);
    ESP_ERROR_CHECK(xTaskCreate(input_log_task, "input_log", 3072, NULL, 4, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
