idf_component_register(
    SRCS "improv_proto.c" "improv_serial.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart hal jradio_wifi_provisioning settings version_info
)
