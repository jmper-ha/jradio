idf_component_register(SRCS "jbt_proto.c" "bt_link_model.c" "bt_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board audio_tags esp_driver_uart esp_timer)
