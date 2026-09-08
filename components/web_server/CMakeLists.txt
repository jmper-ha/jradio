idf_component_register(SRCS "web_server.c" "web_socket.c" "web_protocol.c"
                            "web_view_model.c" "web_json.c" "web_settings.c"
                            "web_cover.c" "web_backup.c"
                       INCLUDE_DIRS "include"
                       REQUIRES settings player_control file_storage dlna yandex_music version_info
                       PRIV_REQUIRES esp_http_server esp_timer jradio_wifi_provisioning
                                     internet_radio json audio_tags ui)
