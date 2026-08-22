idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board diagnostics internet_radio jradio_wifi_provisioning file_player player_control sd_storage settings ui usb_storage web_server
)
