idf_component_register(SRCS "file_player.c" "file_track_progress.c" "file_wav.c"
                       INCLUDE_DIRS "include"
                       REQUIRES audio_tags board file_storage internet_radio)
