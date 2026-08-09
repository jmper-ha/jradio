idf_component_register(
    SRCS "icy_metadata.c" "internet_radio_state.c" "mp3_stream_info.c" "radio_decoder.cpp" "radio_http_status.c" "radio_stream_format.c" "station_catalog.c"
         "station_resume.c" "internet_radio.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_http_client esphome__micro-flac esphome__micro-mp3
             espressif__esp_audio_codec mbedtls settings
)

# esp_audio_codec's prebuilt OpenCore MP3 decoder exports the same five global symbols as
# micro-mp3. Namespace only micro-mp3 so both implementations can coexist in
# the final image while AAC and live Ogg/FLAC use esp_audio_codec directly.
idf_component_get_property(MICRO_MP3_LIB esphome__micro-mp3 COMPONENT_LIB)
if(MICRO_MP3_LIB)
    target_compile_definitions(${MICRO_MP3_LIB} PRIVATE
        fillMainDataBuf=micro_mp3_fillMainDataBuf
        pvmp3_framedecoder=micro_mp3_framedecoder
        pvmp3_decoderMemRequirements=micro_mp3_decoderMemRequirements
        pvmp3_resetDecoder=micro_mp3_resetDecoder
        pvmp3_InitDecoder=micro_mp3_InitDecoder
    )
endif()
