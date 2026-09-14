# Keeps the build's copy of board_options.local.h in step with the file
# beside board_options.h - or a placeholder when there is none. Run before
# every component is built (see the root CMakeLists.txt), and it rewrites the
# copy only when the content differs, so an unchanged file costs no rebuild.
#
# The copy exists so that the compiler always has *some* file to record as a
# dependency: with the include resolved straight from the source tree, a
# board_options.local.h created after the first build changed nothing the
# build could see, and a bench board was flashed without its module.
if(EXISTS "${SOURCE}")
    file(READ "${SOURCE}" wanted)
else()
    set(wanted "/* No board_options.local.h beside board_options.h. */\n")
endif()
set(current "")
if(EXISTS "${COPY}")
    file(READ "${COPY}" current)
endif()
if(NOT current STREQUAL wanted)
    file(WRITE "${COPY}" "${wanted}")
endif()
