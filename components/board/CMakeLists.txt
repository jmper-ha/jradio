idf_component_register(
    SRCS "board.c" "board_input.c" "pcm_diagnostics.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_ili9341 esp_driver_gpio esp_driver_i2s esp_driver_ledc
)
