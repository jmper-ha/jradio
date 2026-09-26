cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(jradio)

# board_options.h ends by including board_options.local.h - a board's own
# wiring, ignored by git. The compiler finds the file beside board_options.h
# when there is one; what the build keeps in its config directory is a copy
# of it, or a placeholder, refreshed by tools/board_options_local.cmake
# before any component is compiled. That copy is what every object records
# as its dependency while the real file is absent, which is the whole point:
# resolved straight from the source tree, a file created after the first
# build changed nothing the build could see (the dependency scan had noted
# nothing for a file that was not there), and a bench board was flashed
# without its module. Now creating, editing or deleting the file changes the
# copy, and the copy's change rebuilds what reads board_options.h.
set(board_options_local "${CMAKE_CURRENT_SOURCE_DIR}/board_options.local.h")
set(board_options_local_copy "${CMAKE_BINARY_DIR}/config/board_options.local.h")
set(board_options_local_sync "${CMAKE_CURRENT_SOURCE_DIR}/tools/board_options_local.cmake")
# Once now, so the copy exists for the first dependency scan.
execute_process(COMMAND ${CMAKE_COMMAND} -DSOURCE=${board_options_local}
                        -DCOPY=${board_options_local_copy} -P ${board_options_local_sync})
# A target rather than a command with an OUTPUT, because a target runs on
# every build, and the copy is its BYPRODUCT: that makes the copy an output
# ninja knows, so everything whose dependency scan named it is rebuilt when
# it changes - and, the command being restat, left alone when the script
# found nothing to rewrite. (A declared output with a never-created "always"
# input was tried: restat saw that input unchanged and skipped the sync.)
add_custom_target(board_options_local ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${board_options_local}
            -DCOPY=${board_options_local_copy} -P ${board_options_local_sync}
    BYPRODUCTS "${board_options_local_copy}"
    COMMENT "Syncing board_options.local.h")
idf_build_get_property(board_options_components BUILD_COMPONENTS)
foreach(component ${board_options_components})
    idf_component_get_property(component_lib ${component} COMPONENT_LIB)
    if(TARGET ${component_lib})
        add_dependencies(${component_lib} board_options_local)
    endif()
endforeach()

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

# The board partition: board_options.h's board, as the firmware reads it at
# boot, written by every `idf.py flash`. For someone who builds the firmware
# the header stays the one description of the board - whatever the partition
# held before, a file from the web flasher included, is replaced by it.
#
# Written by tools/board_bin.py, under ESP-IDF's Python and with the
# firmware's own compiler as the preprocessor, for the reason the littlefs
# staging above runs under Python: there is no host compiler on Windows. A
# target with no OUTPUT, so it runs on every build; the script leaves an
# unchanged image alone.
set(board_bin "${CMAKE_BINARY_DIR}/board.bin")
add_custom_target(board_bin ALL
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/tools/board_bin.py"
            --cc "${CMAKE_C_COMPILER}"
            -I "${CMAKE_SOURCE_DIR}"
            -I "${CMAKE_SOURCE_DIR}/components/board/include"
            --out "${board_bin}"
    BYPRODUCTS "${board_bin}"
    COMMENT "Writing board.bin from board_options.h"
    VERBATIM
)
partition_table_get_partition_info(board_offset "--partition-name board" "offset")
esptool_py_flash_target_image(flash board "${board_offset}" "${board_bin}")

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
