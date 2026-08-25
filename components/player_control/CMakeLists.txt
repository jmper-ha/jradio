idf_component_register(
    SRCS "player_control_logic.c" "player_control.c"
    INCLUDE_DIRS "include"
    REQUIRES audio audio_tags board internet_radio jradio_wifi_provisioning esp_timer file_player file_storage sd_storage usb_storage yandex_music
)
