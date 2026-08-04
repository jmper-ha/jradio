cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(jradio)

# Static Web UI is flashed explicitly with `idf.py littlefs-flash`; normal firmware
# flashes must preserve /littlefs/config/wifi.json.
littlefs_create_partition_image(littlefs components/web_server/web_assets)
