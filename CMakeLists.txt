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
#
# The compression step runs under ESP-IDF's own Python rather than a shell.
# It used to be a bash script, which cost nothing on Linux and made the build
# impossible on Windows: `bash` is not on PATH there, this target failed on
# every build, and the image came out with no web interface in it. Python is
# the one interpreter every host running idf.py is guaranteed to have.
#
# The same staging step stamps the web assets with the version they were built
# from. It is the firmware's own PROJECT_VER - the one ESP-IDF derives with
# `git describe` and writes into the app header - so both halves of the device
# answer with one string, and a mismatch means what it says: the two were
# flashed from different commits. Which happens, because they are flashed by
# two different commands.
set(littlefs_stage_dir "${CMAKE_BINARY_DIR}/littlefs_data")
idf_build_get_property(python PYTHON)
idf_build_get_property(jradio_project_ver PROJECT_VER)
add_custom_target(littlefs_stage
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${littlefs_stage_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${littlefs_stage_dir}"
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/tools/gzip_www_assets.py" "${littlefs_stage_dir}/www"
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/tools/stamp_version.py"
            --version "${jradio_project_ver}"
            --out "${littlefs_stage_dir}/config/version.json"
    VERBATIM
)

littlefs_create_partition_image(littlefs "${littlefs_stage_dir}" DEPENDS littlefs_stage)

# LVGL's heap moves to PSRAM - see components/lvgl_pool/include/lvgl_pool.h for
# why, and for what it costs. Only the pool's *address* changes; LVGL keeps its
# own allocator inside it.
#
# The allocator sits in a component of its own so that lvgl can be linked
# against it without a cycle: lvgl cannot depend on ui, which depends on lvgl.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(lvgl_pool_lib lvgl_pool COMPONENT_LIB)
target_include_directories(${lvgl_lib} PRIVATE "${CMAKE_SOURCE_DIR}/components/lvgl_pool/include")
target_link_libraries(${lvgl_lib} PRIVATE ${lvgl_pool_lib})
target_compile_definitions(${lvgl_lib} PRIVATE
    "LV_MEM_POOL_INCLUDE=\"lvgl_pool.h\""
    LV_MEM_POOL_ALLOC=lvgl_pool_alloc
)
