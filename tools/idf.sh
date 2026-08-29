#!/usr/bin/env bash
# Run idf.py with ESP-IDF activated, wherever it happens to be installed.
#
# The VS Code tasks call this instead of idf.py directly. A freshly cloned
# project has nothing on PATH, so `idf.py build` answers "command not found" -
# a poor first message for someone who has just been told to press Build. This
# finds the framework the way tests/run_host_tests.sh already finds it for the
# host tests, activates it, and passes everything through:
#
#     bash tools/idf.sh build
#     bash tools/idf.sh flash monitor
#
# The port is left to idf.py, which probes for it; export ESPPORT to pin one
# when several boards are attached.
#
# Every variable here is jradio_-prefixed because export.sh is sourced into
# this shell and unsets names of its own on the way out - a plain idf_path does
# not survive it.
set -euo pipefail

jradio_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# main/idf_component.yml asks for >=5.5,<5.6. Several versions side by side is
# the normal state of an ESP-IDF machine, so a candidate whose path says 5.5 is
# taken ahead of one that does not, rather than the newest winning.
jradio_want="5.5"

jradio_is_idf() { [ -f "${1}/export.sh" ] && [ -f "${1}/tools/idf.py" ]; }

jradio_found=()
jradio_add() {
    if [ -n "${1:-}" ] && jradio_is_idf "$1"; then
        jradio_found+=("$1")
    fi
    return 0   # a miss is the normal case, and must not trip set -e
}

jradio_add "${IDF_PATH:-}"

# The build directory remembers the framework it was configured with, which is
# the one that can rebuild it without a full reconfigure.
if [ -f "${jradio_root}/build/CMakeCache.txt" ]; then
    jradio_add "$(sed -n 's/^IDF_PATH:PATH=//p' "${jradio_root}/build/CMakeCache.txt" | head -n 1)"
fi

# Already activated in this shell: idf.py sits in $IDF_PATH/tools.
if command -v idf.py >/dev/null 2>&1; then
    jradio_add "$(cd "$(dirname "$(command -v idf.py)")/.." && pwd)"
fi

# The usual install locations: the ESP-IDF Installation Manager, the VS Code
# extension, and a hand-cloned framework.
for jradio_glob in \
    "${HOME}/.espressif/v"*/esp-idf \
    "${HOME}/.espressif/frameworks/esp-idf-v"* \
    "${HOME}/esp/esp-idf-v"* \
    "${HOME}/esp/esp-idf" \
    /opt/esp-idf; do
    jradio_add "${jradio_glob}"
done

jradio_idf=""
if [ "${#jradio_found[@]}" -gt 0 ]; then
    for jradio_glob in "${jradio_found[@]}"; do
        case "${jradio_glob}" in
            *"${jradio_want}"*) jradio_idf="${jradio_glob}"; break ;;
        esac
    done
    if [ -z "${jradio_idf}" ]; then
        jradio_idf="${jradio_found[0]}"
        echo "tools/idf.sh: using ${jradio_idf}; this project is built with ESP-IDF ${jradio_want}.x" >&2
    fi
fi

if [ -z "${jradio_idf}" ]; then
    cat >&2 <<'MSG'
tools/idf.sh: no ESP-IDF installation found.

In VS Code: open the command palette (Ctrl+Shift+P) and run
"ESP-IDF: Configure ESP-IDF extension" - it downloads the framework and its
toolchain. Choose version 5.5.x.

Outside VS Code, install it by hand and either export IDF_PATH or source its
export.sh before running this script:
https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/
MSG
    exit 1
fi

# export.sh prints a dozen lines about tool versions and shell completion every
# time. Held back rather than discarded: it is also where a framework that was
# cloned but never had install.sh run for it says so, and that message is the
# whole diagnosis.
jradio_log="$(mktemp)"
echo "tools/idf.sh: ESP-IDF ${jradio_idf}" >&2
# shellcheck disable=SC1091
if ! . "${jradio_idf}/export.sh" >"${jradio_log}" 2>&1; then
    cat "${jradio_log}" >&2
    rm -f "${jradio_log}"
    echo "tools/idf.sh: export.sh failed - run install.sh in that directory first" >&2
    exit 1
fi

# export.sh can report success and still leave nothing on PATH - a framework
# whose tools were never installed does exactly that - so say what happened
# instead of letting the shell answer "idf.py: not found".
if ! command -v idf.py >/dev/null 2>&1; then
    cat "${jradio_log}" >&2
    rm -f "${jradio_log}"
    echo "tools/idf.sh: idf.py is still not on PATH - run install.sh in ${jradio_idf}" >&2
    exit 1
fi
rm -f "${jradio_log}"

cd "${jradio_root}"
exec idf.py "$@"
