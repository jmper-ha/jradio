idf_component_register(
    SRCS "ui.c" "ui_deferred_start.c" "ui_font_cyrillic_14.c" "ui_menu.c" "ui_radio_text.c" "ui_station_list.c"
    INCLUDE_DIRS "include"
    REQUIRES audio board internet_radio jradio_wifi_provisioning lvgl esp_timer
)
