idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES audio board internet_radio jradio_wifi_provisioning ui usb_storage web_server
)
