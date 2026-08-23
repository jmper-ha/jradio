idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board diagnostics internet_radio jradio_wifi_provisioning file_player lvgl_pool player_control sd_storage settings ui usb_storage web_server yandex_music
)
