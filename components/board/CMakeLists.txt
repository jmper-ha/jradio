idf_component_register(
    SRCS "audio_pcm_convert.c" "audio_volume.c" "board.c" "board_audio_health.c"
         "board_audio_startup.c" "board_input.c" "pcm_diagnostics.c"
    # The project root carries board_options.h, the single place every external
    # device's wiring is declared. It is public here so the components that
    # REQUIRE board (ui, internet_radio, usb_player) see it too.
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}"
    REQUIRES esp_lcd esp_lcd_ili9341 esp_driver_gpio esp_driver_i2s esp_driver_ledc
)
