idf_component_register(SRCS "web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_timer jradio_wifi_provisioning settings internet_radio json)
