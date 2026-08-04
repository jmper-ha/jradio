idf_component_register(SRCS "wifi_provisioning.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event esp_netif esp_wifi nvs_flash settings)
