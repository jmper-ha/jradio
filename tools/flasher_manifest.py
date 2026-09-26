#!/usr/bin/env python3
"""Writes the flasher site's manifest.json: the version, the displays there is
a firmware for, and where each image goes.

The offsets are the build's own - build/flasher_args.json, which idf.py writes
from the partition table - plus the data partition's from partitions.csv,
which `idf.py flash` leaves out on purpose and so flasher_args.json does not
name. flasher/flasher_core.js reads exactly these keys.

Usage: flasher_manifest.py --flasher-args build/flasher_args.json
           --partitions partitions.csv --version v1.4.0
           --display st7796s_480_320 [--display ...] --out site/manifest.json
"""

import argparse
import csv
import json


def littlefs_offset(partitions_path):
    with open(partitions_path, newline="") as table:
        for row in csv.reader(table):
            cells = [cell.strip() for cell in row]
            if cells and cells[0] == "littlefs":
                return int(cells[3], 0)
    raise SystemExit("flasher_manifest.py: no littlefs row in " + partitions_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--flasher-args", required=True)
    parser.add_argument("--partitions", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--display", action="append", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    with open(args.flasher_args) as handle:
        flasher_args = json.load(handle)
    offset = lambda name: int(flasher_args[name]["offset"], 0)  # noqa: E731
    manifest = {
        "version": args.version,
        "displays": args.display,
        "offsets": {
            "bootloader": offset("bootloader"),
            "partition_table": offset("partition-table"),
            "ota_data": offset("otadata"),
            "board": offset("board"),
            "app": offset("app"),
            "littlefs": littlefs_offset(args.partitions),
        },
    }
    with open(args.out, "w") as out:
        json.dump(manifest, out, indent=2)
        out.write("\n")


if __name__ == "__main__":
    main()
