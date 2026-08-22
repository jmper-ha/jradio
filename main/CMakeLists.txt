idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board diagnostics internet_radio jradio_wifi_provisioning player_control sd_storage settings ui usb_player usb_storage web_server
)
