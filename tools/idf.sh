#!/usr/bin/env bash
# Run idf.py with ESP-IDF activated, wherever it happens to be installed.
#
# The VS Code tasks call this instead of idf.py directly; tools/idf.ps1 is the
# same script for Windows, and a change here belongs there too. A freshly cloned
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

# The version this project is built and verified with, and the one
# dependencies.lock names.
#
# Pinned exactly rather than by family, because "5.5" is not one answer on a
# machine that has ever upgraded: 5.5.4 and 5.5.5 sit side by side under
# ~/.espressif, the glob below finds both, and taking whichever came first
# meant the VS Code tasks quietly built on 5.5.4 while a terminal that had
# sourced export.sh built on 5.5.5. That mix does not stay quiet for long -
# a build on the other version rewrites dependencies.lock and turns up in the
# diff - but by then it has already produced firmware nobody meant to make.
jradio_want="5.5.5"
# What main/idf_component.yml actually allows, >=5.5,<5.6. The fallback when
# the pinned version is not installed, so a machine carrying only 5.5.6 builds
# instead of being told no.
jradio_want_family="5.5"

jradio_is_idf() { [ -f "${1}/export.sh" ] && [ -f "${1}/tools/idf.py" ]; }

jradio_found=()
jradio_add() {
    if [ -n "${1:-}" ] && jradio_is_idf "$1"; then
        jradio_found+=("$1")
    fi
    return 0   # a miss is the normal case, and must not trip set -e
}

# IDF_PATH is deliberately not a candidate here - it is handled below as an
# override. The build directory used to be one: it recorded IDF_PATH in its
# CMakeCache, and reusing that avoided a reconfigure. As of 5.5.5 the cache no
# longer carries the variable at all, and what it does carry can name two
# different versions at once after a build on each - which is the state that
# motivated the pin above, not a source to trust.

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

# Takes the first candidate whose path contains $1, if any.
jradio_pick() {
    [ "${#jradio_found[@]}" -gt 0 ] || return 1
    for jradio_candidate in "${jradio_found[@]}"; do
        case "${jradio_candidate}" in
            *"$1"*) jradio_idf="${jradio_candidate}"; return 0 ;;
        esac
    done
    return 1
}

jradio_idf=""
if jradio_is_idf "${IDF_PATH:-}"; then
    # An explicit IDF_PATH is somebody's deliberate choice - testing another
    # version, or a shell where export.sh has been sourced - and it wins
    # outright. Being redirected to the pinned version without being told is
    # no way to test one.
    jradio_idf="${IDF_PATH}"
elif ! jradio_pick "${jradio_want}"; then
    if jradio_pick "${jradio_want_family}"; then
        echo "tools/idf.sh: ESP-IDF ${jradio_want} is not installed; using ${jradio_idf}" >&2
    elif [ "${#jradio_found[@]}" -gt 0 ]; then
        jradio_idf="${jradio_found[0]}"
        echo "tools/idf.sh: using ${jradio_idf}; this project is built with ESP-IDF ${jradio_want}" >&2
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
https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/
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

# Without a port, esptool probes every /dev/ttyS* the machine has before it
# reaches the board - 34 of them here, several seconds of scrolling for a
# task someone pressed a button to run. One obvious candidate is taken as the
# answer; with several, idf.py is left to do its own thing, because guessing
# which board is the radio is worse than a slow probe.
if [ -z "${ESPPORT:-}" ]; then
    jradio_ports=()
    for jradio_glob in /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial*; do
        [ -e "${jradio_glob}" ] && jradio_ports+=("${jradio_glob}")
    done
    if [ "${#jradio_ports[@]}" -eq 1 ]; then
        export ESPPORT="${jradio_ports[0]}"
        echo "tools/idf.sh: port ${ESPPORT}" >&2
    elif [ "${#jradio_ports[@]}" -gt 1 ]; then
        echo "tools/idf.sh: several ports (${jradio_ports[*]}); set ESPPORT to choose" >&2
    fi
fi

cd "${jradio_root}"
exec idf.py "$@"
