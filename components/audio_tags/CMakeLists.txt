idf_component_register(
    SRCS "audio_tags_text.c" "id3v2.c" "flac_tags.c" "audio_tags_reader.c" "image_scale.c" "album_art.c" "jpeg_progressive.c" "tjpgd/tjpgd.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "tjpgd" "stb"
)
