#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_build_dir=$(mktemp -d /tmp/jradio-host-tests.XXXXXX)
trap 'rm -rf -- "${test_build_dir}"' EXIT

host_cc=${CC:-gcc}
common_flags=(
    -std=c17
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
)
include_flags=(
    -I"${project_dir}"
    -I"${project_dir}/components/audio/include"
    -I"${project_dir}/components/board/include"
    -I"${project_dir}/components/internet_radio/include"
    -I"${project_dir}/components/jradio_wifi_provisioning/include"
    -I"${project_dir}/components/player_control/include"
    -I"${project_dir}/components/settings/include"
    -I"${project_dir}/components/ui/include"
    -I"${project_dir}/components/usb_player/include"
    -I"${project_dir}/components/usb_storage/include"
    -I"${project_dir}/components/web_server"
    -I"${project_dir}/components/web_server/include"
)

find_idf_path() {
    if [[ -n ${IDF_PATH:-} && -f ${IDF_PATH}/components/json/cJSON/cJSON.c ]]; then
        printf '%s\n' "${IDF_PATH}"
        return 0
    fi

    local cache_path="${project_dir}/build/CMakeCache.txt"
    if [[ -f ${cache_path} ]]; then
        local cached_path
        cached_path=$(sed -n 's/^IDF_PATH:PATH=//p' "${cache_path}" | head -n 1)
        if [[ -n ${cached_path} && -f ${cached_path}/components/json/cJSON/cJSON.c ]]; then
            printf '%s\n' "${cached_path}"
            return 0
        fi
    fi

    if command -v idf.py >/dev/null 2>&1; then
        local idf_tool idf_from_path
        idf_tool=$(command -v idf.py)
        idf_from_path=$(cd "$(dirname "${idf_tool}")/.." && pwd)
        if [[ -f ${idf_from_path}/components/json/cJSON/cJSON.c ]]; then
            printf '%s\n' "${idf_from_path}"
            return 0
        fi
    fi

    local tools_root=${IDF_TOOLS_PATH:-${HOME}/.espressif}
    local candidate discovered=""
    for candidate in "${tools_root}"/v*/esp-idf; do
        if [[ -f ${candidate}/components/json/cJSON/cJSON.c ]]; then
            discovered=${candidate}
        fi
    done
    if [[ -n ${discovered} ]]; then
        printf '%s\n' "${discovered}"
        return 0
    fi

    return 1
}

idf_path=$(find_idf_path) || {
    printf 'ESP-IDF cJSON source not found; set IDF_PATH and retry.\n' >&2
    exit 1
}
cjson_source="${idf_path}/components/json/cJSON/cJSON.c"
cjson_include="${idf_path}/components/json/cJSON"

run_test() {
    local name=$1
    shift
    "${host_cc}" "${common_flags[@]}" "${include_flags[@]}" "$@" \
        -o "${test_build_dir}/${name}"
    ASAN_OPTIONS=detect_leaks=1 "${test_build_dir}/${name}"
}

cd "${project_dir}"
grep -qx 'CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192' sdkconfig.defaults
grep -Fq 'lv_display_set_buffers(s_display, buffer1, NULL,' components/ui/ui.c
run_test audio_source tests/test_audio_source.c components/audio/audio_source_manager.c
run_test board_audio_health tests/test_board_audio_health.c \
    components/board/board_audio_health.c
run_test board_audio_startup tests/test_board_audio_startup.c \
    components/board/board_audio_startup.c
run_test board_options tests/test_board_options.c
run_test board_input tests/test_board_input.c components/board/board_input.c
run_test icy_metadata tests/test_icy_metadata.c components/internet_radio/icy_metadata.c
run_test internet_radio_state tests/test_internet_radio_state.c \
    components/internet_radio/internet_radio_state.c
run_test mp3_stream_info tests/test_mp3_stream_info.c components/internet_radio/mp3_stream_info.c
run_test pcm_diagnostics tests/test_pcm_diagnostics.c components/board/pcm_diagnostics.c
run_test player_control_logic tests/test_player_control_logic.c \
    components/player_control/player_control_logic.c
run_test radio_http_status tests/test_radio_http_status.c \
    components/internet_radio/radio_http_status.c
run_test radio_prebuffer tests/test_radio_prebuffer.c \
    components/internet_radio/radio_prebuffer.c
run_test radio_stream_format tests/test_radio_stream_format.c \
    components/internet_radio/radio_stream_format.c components/internet_radio/station_catalog.c
run_test settings_csv tests/test_settings_csv.c components/settings/settings_csv.c
run_test device_settings tests/test_device_settings.c components/settings/device_settings.c \
    components/settings/settings_csv.c
run_test station_catalog tests/test_station_catalog.c components/internet_radio/station_catalog.c
run_test station_resume tests/test_station_resume.c components/internet_radio/station_resume.c \
    components/settings/settings_csv.c
run_test ui_deferred_start tests/test_ui_deferred_start.c components/ui/ui_deferred_start.c
run_test ui_draw_buffer tests/test_ui_draw_buffer.c components/ui/ui_draw_buffer.c
run_test ui_menu tests/test_ui_menu.c components/ui/ui_menu.c
run_test ui_player_state tests/test_ui_player_state.c components/ui/ui_player_state.c
run_test ui_radio_text tests/test_ui_radio_text.c components/ui/ui_radio_text.c \
    components/internet_radio/radio_stream_format.c
run_test ui_settings_view tests/test_ui_settings_view.c components/ui/ui_settings_view.c
run_test ui_settings_model tests/test_ui_settings_model.c components/ui/ui_settings_model.c
run_test ui_station_list tests/test_ui_station_list.c components/ui/ui_station_list.c
run_test ui_usb_notice tests/test_ui_usb_notice.c components/ui/ui_usb_notice.c
run_test usb_browser tests/test_usb_browser.c components/usb_storage/usb_browser.c
run_test usb_wav tests/test_usb_wav.c components/usb_player/usb_wav.c
run_test wifi_provisioning tests/test_wifi_provisioning.c \
    components/jradio_wifi_provisioning/wifi_provisioning.c
run_test wifi_settings tests/test_wifi_settings.c components/settings/wifi_settings.c
run_test web_server -I"${cjson_include}" tests/test_web_server.c \
    components/web_server/web_server.c components/web_server/web_socket.c \
    components/web_server/web_view_model.c components/web_server/web_json.c \
    components/settings/wifi_settings.c \
    components/usb_storage/usb_browser.c "${cjson_source}"
run_test web_json tests/test_web_json.c components/web_server/web_json.c
run_test web_protocol -I"${cjson_include}" tests/test_web_protocol.c \
    components/web_server/web_protocol.c "${cjson_source}"
run_test web_view_model tests/test_web_view_model.c \
    components/web_server/web_view_model.c

if ! command -v node >/dev/null 2>&1; then
    printf 'node is required for web player regression tests.\n' >&2
    exit 127
fi
node tests/test_web_player.js
node tests/test_web_settings.js
node tests/test_web_playlist.js
node tests/test_web_usb.js

printf 'All host tests passed.\n'
