idf_component_register(SRCS "web_server.c" "web_protocol.c" "web_view_model.c"
                       INCLUDE_DIRS "include"
                       REQUIRES settings player_control
                       PRIV_REQUIRES esp_http_server esp_timer jradio_wifi_provisioning
                                     internet_radio json)
