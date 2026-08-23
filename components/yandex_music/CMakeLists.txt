idf_component_register(SRCS "yandex_api.c" "yandex_auth.c" "yandex_auth_flow.c"
                            "yandex_auth_json.c" "yandex_catalog.c"
                            "yandex_catalog_parse.c" "yandex_json_reader.c"
                            "yandex_link.c" "yandex_md5.c" "yandex_rotor.c"
                            "yandex_token_store.c" "yandex_tracks_parse.c"
                       INCLUDE_DIRS "include"
                       REQUIRES settings internet_radio audio_tags
                       PRIV_REQUIRES esp_http_client esp-tls esp_timer json)
