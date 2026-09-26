idf_component_register(
    SRCS "audio_pcm_convert.c" "audio_volume.c" "board.c" "board_amplifier.c"
         "board_config.c" "board_config_compiled.c" "board_config_load.c"
         "board_audio_health.c"
         "board_audio_startup.c" "board_input.c" "boot_splash_image.c"
         "ir_decode.c" "ir_receiver.c" "ir_wake_stub.c" "pcm_diagnostics.c" "remote_control.c" "remote_map.c"
         # One per display controller; the unselected ones compile to nothing.
         "display/ili9341.c" "display/st7789.c" "display/ili9488.c"
         "display/st7796s.c"
    # The project root carries board_options.h, the single place every external
    # device's wiring is declared. It is public here so the components that
    # REQUIRE board (ui, internet_radio, file_player) see it too.
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}"
    REQUIRES esp_partition esp_lcd esp_lcd_ili9341 esp_lcd_ili9488 esp_lcd_st7796 esp_driver_gpio esp_driver_i2s esp_driver_ledc esp_driver_rmt esp_timer
)
