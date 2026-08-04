#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_source.h"
#include "board.h"
#include "board_input.h"
#include "internet_radio.h"
#include "ui.h"
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
            if (action == BOARD_INPUT_ACTION_F1_LONG) {
                const esp_err_t err = wifi_provisioning_reset();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "F1 long reset failed err=%s", esp_err_to_name(err));
                }
                continue;
            }
            (void)ui_post_input(action);
        }
    }
}

void app_main(void)
{
    static audio_source_manager_t source_manager;

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(wifi_provisioning_init());
    ESP_ERROR_CHECK(wifi_provisioning_start());
    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(usb_storage_init());
    ESP_ERROR_CHECK(internet_radio_init());
    audio_source_manager_init(&source_manager);
    ESP_ERROR_CHECK(ui_init(&source_manager));
    ESP_LOGI(TAG, "jradio booted; active audio source=%d",
             (int)audio_source_manager_active(&source_manager));
    xTaskCreate(input_log_task, "input_log", 3072, NULL, 4, NULL);
}
