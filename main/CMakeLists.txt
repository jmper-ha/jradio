idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board internet_radio jradio_wifi_provisioning player_control ui usb_storage web_server
)
