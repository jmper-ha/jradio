idf_component_register(SRCS "yandex_auth.c" "yandex_auth_flow.c" "yandex_auth_json.c"
                            "yandex_token_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES settings
                       PRIV_REQUIRES esp_http_client esp-tls esp_timer json)
