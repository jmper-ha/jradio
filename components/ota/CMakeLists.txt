idf_component_register(
    SRCS "ota_check.c" "ota_image.c" "ota_offer.c" "ota_tar.c" "ota_update.c" "ota_www.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update board bootloader_support esp_http_client esp_partition esp_timer json
             jradio_wifi_provisioning mbedtls player_control settings
)
