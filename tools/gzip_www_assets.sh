#!/usr/bin/env bash
set -euo pipefail

www_dir=${1:?usage: gzip_www_assets.sh <www-dir>}
shopt -s nullglob
for file in "${www_dir}"/*.html "${www_dir}"/*.js "${www_dir}"/*.css; do
    gzip -9 -k -f "${file}"
done
