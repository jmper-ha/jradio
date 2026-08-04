idf_component_register(
    SRCS "icy_metadata.c" "internet_radio_state.c" "mp3_stream_info.c" "station_catalog.c"
         "station_resume.c" "internet_radio.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_http_client espressif__esp_audio_simple_player settings
)
