#!/usr/bin/env bash
# How many radios updated themselves, from GitHub's own download counts.
#
# Usage:  bash tools/ota_stats.sh [owner/repo]
#
# Per release: ota.json is the number of times radios asked whether there is
# something new (each radio asks at boot and once a day, so this is checks,
# not radios), ota-<display>.bin the updates radios started, per display, and
# jradio-*.bin the files people took by hand. ota.json is fetched through
# releases/latest/download/, so its count belongs to whichever release was the
# latest at the time. A download is not an install: an update cut short and
# tried again counts twice. No server of ours sees any of it.
set -euo pipefail

repo=${1:-$(gh repo view --json nameWithOwner -q .nameWithOwner)}
gh api --paginate "repos/${repo}/releases" --jq '
  .[] |
  "\(.tag_name)\n" +
  ([.assets[] | select(.name == "ota.json")] |
     if length > 0 then "  checks        \(.[0].download_count)\n" else "" end) +
  ([.assets[] | select(.name | test("^ota-.*\\.bin$"))] |
     if length > 0 then
       "  radio updates \(map(.download_count) | add)\n" +
       (map("    \(.name | sub("^ota-"; "") | sub("\\.bin$"; ""))\t\(.download_count)") | join("\n")) + "\n"
     else "" end) +
  ([.assets[] | select(.name | test("^jradio-.*\\.bin$"))] |
     if length > 0 then "  by hand (.bin) \(map(.download_count) | add)\n" else "" end)'
