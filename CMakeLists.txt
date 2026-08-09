cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(jradio)

# Static Web UI is flashed explicitly with `idf.py littlefs-flash`; normal firmware
# flashes must preserve /littlefs/config/wifi.json.
#
# Stage a copy of data/ with a pre-gzipped sibling next to each www/*.html,
# *.js and *.css file, so web_server.c can serve the compressed variant to
# browsers that accept it without adding a runtime gzip dependency on the
# device. The originals are kept too, for clients without gzip support.
# This target has no OUTPUT file, so (like littlefs_create_partition_image's
# own target below) it always reruns on every build instead of relying on
# CMake to detect changes under data/ - a stamp-file/OUTPUT-based version was
# tried first and went stale after edits to data/ (e.g. an on-device
# playlist edit synced back into data/config/stations.csv), silently baking
# an outdated copy into littlefs.bin.
set(littlefs_stage_dir "${CMAKE_BINARY_DIR}/littlefs_data")
add_custom_target(littlefs_stage
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${littlefs_stage_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${littlefs_stage_dir}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/gzip_www_assets.sh" "${littlefs_stage_dir}/www"
    VERBATIM
)

littlefs_create_partition_image(littlefs "${littlefs_stage_dir}" DEPENDS littlefs_stage)
