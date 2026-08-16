#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_input.h"
#include "internet_radio.h"
#include "player_control.h"
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
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(wifi_provisioning_init());
    ESP_ERROR_CHECK(wifi_provisioning_start());
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
