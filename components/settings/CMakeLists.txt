idf_component_register(SRCS "wifi_settings.c" "settings_csv.c" "device_clock.c" "device_settings.c" INCLUDE_DIRS "include"
                       REQUIRES esp_netif lwip
                       PRIV_REQUIRES joltwallet__littlefs json)
