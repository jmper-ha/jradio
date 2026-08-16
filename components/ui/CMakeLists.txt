idf_component_register(
    SRCS "ui.c" "ui_deferred_start.c" "ui_draw_buffer.c" "ui_font_cyrillic_14.c" "ui_menu.c" "ui_player_state.c" "ui_radio_text.c" "ui_settings_model.c" "ui_settings_view.c" "ui_station_list.c" "ui_usb_notice.c"
    INCLUDE_DIRS "include"
    REQUIRES board player_control lvgl esp_timer usb_storage jradio_wifi_provisioning settings
)
