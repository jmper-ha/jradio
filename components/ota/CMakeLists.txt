idf_component_register(
    SRCS "ota_image.c" "ota_update.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update board bootloader_support esp_partition esp_timer player_control
)
