idf_component_register(SRCS "usb_player.c" "usb_track_progress.c" "usb_wav.c" INCLUDE_DIRS "include"
                       REQUIRES audio_tags board internet_radio usb_storage)
