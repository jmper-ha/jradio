idf_component_register(
    SRCS "audio_tags_text.c" "id3v2.c" "flac_tags.c" "audio_tags_reader.c" "image_scale.c" "image_decode.c" "album_art.c" "cover_file.c" "tjpgd/tjpgd.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "tjpgd" "stb"
)
