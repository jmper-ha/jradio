idf_component_register(
    SRCS "ui.c" "ui_autoplay.c" "ui_click_gesture.c" "ui_deferred_start.c" "ui_draw_buffer.c" "ui_feed_icon_bitmaps.c" "ui_feed_icons.c" "ui_feed_model.c" "ui_font_cyrillic_14.c" "ui_font_cyrillic_20.c" "ui_menu.c" "ui_player_state.c" "ui_radio_text.c" "ui_seek.c" "ui_settings_model.c" "ui_station_list.c" "ui_text_scroll.c" "ui_status_bar.c" "ui_files_notice.c" "ui_vu_meter.c" "ui_web_address.c" "ui_yandex_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_tags board player_control lvgl esp_timer file_storage jradio_wifi_provisioning settings diagnostics yandex_music
)
