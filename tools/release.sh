#!/usr/bin/env bash
# Collects the binaries of a release into release/<version>/ from an existing
# build, with the data image rebuilt without the device's secrets.
#
# Usage, on a clean checkout of the tagged commit, after `idf.py build`:
#     bash tools/release.sh
#
# Why the data image is made again rather than copied: the build stages data/
# as it is on this machine, and on a developer's machine that includes
# wifi.json - the Wi-Fi password - and possibly the Yandex token and the
# weather key. None of that belongs in a file anybody downloads. A device
# flashed with this image comes up with no networks and opens its setup
# access point, which is the right first boot for someone else's board.
#
# The pieces: the app, the bootloader, the partition table, the OTA data,
# the data image, and all of them merged into one file that goes to offset 0
# - the whole flash in one write for a board straight out of the bag.
set -euo pipefail

project_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="${project_dir}/build"
cd "${project_dir}"

[ -f "${build_dir}/jradio.bin" ] || { echo "no build/jradio.bin - run idf.py build first" >&2; exit 1; }
command -v esptool.py >/dev/null 2>&1 || { echo "esptool.py not on PATH - activate ESP-IDF first" >&2; exit 1; }

version=$(git describe --tags --dirty --always)
case "${version}" in
    *-dirty) echo "the working tree is dirty; a release is built from a commit" >&2; exit 1 ;;
esac
# What the firmware in build/ says it is has to be the same string, or the
# files would be named after a tag the binary does not carry.
built=$(strings "${build_dir}/jradio.bin" | grep -m1 -E '^v[0-9]+\.[0-9]+\.[0-9]+' || true)
if [ "${built}" != "${version}" ]; then
    echo "build/jradio.bin says '${built}', git says '${version}': reconfigure (touch CMakeLists.txt) and build again" >&2
    exit 1
fi

out="${project_dir}/release/${version}"
rm -rf "${out}"
mkdir -p "${out}"

# The data image, from a copy of the staged tree with the secrets taken out.
# The parameters are the ones the build uses - see littlefs_create_partition_image
# in the root CMakeLists.txt and build.ninja.
stage=$(mktemp -d)
trap 'rm -rf -- "${stage}"' EXIT
cp -r "${build_dir}/littlefs_data/." "${stage}/"
rm -f "${stage}/config/wifi.json" "${stage}/config/yandex.json" "${stage}/config/weather.json"
# name, type, subtype, offset, size - with whatever spacing the file has.
fs_offset=$(awk -F, '$1 ~ /^littlefs/ {gsub(/ /,"",$4); print $4; exit}' partitions.csv)
fs_size=$(awk -F, '$1 ~ /^littlefs/ {gsub(/ /,"",$5); print $5; exit}' partitions.csv)
[ -n "${fs_size}" ] && [ -n "${fs_offset}" ] || { echo "cannot read the littlefs row of partitions.csv" >&2; exit 1; }
"${build_dir}/littlefs_py_venv/bin/littlefs-python" create "${stage}" "${out}/jradio-${version}-littlefs.bin" \
    --fs-size="${fs_size}" --name-max=64 --block-size=4096 >/dev/null

cp "${build_dir}/jradio.bin" "${out}/jradio-${version}-app.bin"
cp "${build_dir}/bootloader/bootloader.bin" "${out}/jradio-${version}-bootloader.bin"
cp "${build_dir}/partition_table/partition-table.bin" "${out}/jradio-${version}-partition-table.bin"
cp "${build_dir}/ota_data_initial.bin" "${out}/jradio-${version}-ota-data.bin"

# The offsets are the build's own (build/flash_args), plus the data partition.
esptool.py --chip esp32s3 merge_bin -o "${out}/jradio-${version}-full.bin" \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "${build_dir}/bootloader/bootloader.bin" \
    0x8000 "${build_dir}/partition_table/partition-table.bin" \
    0xf000 "${build_dir}/ota_data_initial.bin" \
    0x20000 "${build_dir}/jradio.bin" \
    "${fs_offset}" "${out}/jradio-${version}-littlefs.bin" >/dev/null

(cd "${out}" && sha256sum jradio-* > SHA256SUMS)

cat > "${out}/README.md" <<TEXT
# jRadio ${version}

A whole board in one write (ESP32-S3, 16 MB flash):

    esptool.py --chip esp32s3 -p <port> -b 460800 write_flash 0x0 jradio-${version}-full.bin

Or piece by piece - the app alone keeps the device's data partition, which is
what an update wants:

    esptool.py --chip esp32s3 -p <port> -b 460800 write_flash 0x20000 jradio-${version}-app.bin

    esptool.py --chip esp32s3 -p <port> -b 460800 write_flash ${fs_offset} jradio-${version}-littlefs.bin

The data image carries the web interface, the default station list and the
default settings, and no networks or keys: a device flashed with it opens
its own setup access point on the first boot. Writing it over a working
device erases the saved networks, the playlist edits and the keys - back them
up first (Settings > Backup on the web page) and restore afterwards.

Checksums in SHA256SUMS.
TEXT

echo "release/${version}:"
(cd "${out}" && ls -l --block-size=K | tail -n +2 | awk '{print "  " $5 "\t" $9}')
