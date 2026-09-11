idf_component_register(
    SRCS "weather_report.c" "weather.c"
    INCLUDE_DIRS "include"
    REQUIRES settings json
    PRIV_REQUIRES esp_http_client esp_timer jradio_wifi_provisioning
)
