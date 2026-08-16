idf_component_register(SRCS "wifi_settings.c" "settings_csv.c" "device_settings.c" INCLUDE_DIRS "include"
                       PRIV_REQUIRES joltwallet__littlefs json)
