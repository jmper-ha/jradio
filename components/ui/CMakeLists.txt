idf_component_register(
    SRCS "ui.c" "ui_deferred_start.c" "ui_draw_buffer.c" "ui_font_cyrillic_14.c" "ui_menu.c" "ui_radio_text.c" "ui_station_list.c"
    INCLUDE_DIRS "include"
    REQUIRES board player_control lvgl esp_timer
)
