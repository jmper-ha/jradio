#!/usr/bin/env python3
"""Write the version of the web assets into the LittleFS staging directory.

The firmware carries its own version already: ESP-IDF runs `git describe` and
puts the result in the app header, where esp_app_get_description() reads it.
The web interface has no such thing, and it needs one - it is flashed by a
different command. `idf.py flash` never touches LittleFS and
`idf.py littlefs-flash` touches nothing else, so the two halves drift apart as
soon as one is written without the other, and until this file existed nothing
on the device could say so.

Run from the littlefs_stage target in the root CMakeLists.txt, which restages
data/ on every build, so the stamp is always as fresh as the image it goes
into. It is written into the staging copy and never into data/ - a generated
file in the source tree would be committed by somebody sooner or later.

Usage:
    python3 tools/stamp_version.py --version <string> --out <path>
"""

import argparse
import datetime
import json
import os


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True,
                        help="what the firmware is stamped with, from CMake's PROJECT_VER")
    parser.add_argument("--out", required=True, help="the json file to write")
    args = parser.parse_args()

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    # UTC, and to the day: the device shows this beside the firmware's own
    # build date, and a local timezone would make the two disagree for reasons
    # that have nothing to do with what was built.
    stamp = {
        "version": args.version,
        "built": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"),
    }
    # Compact and with a trailing newline: it is read by a device with a fixed
    # buffer, and by a person with `cat`.
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(stamp, handle, ensure_ascii=False, separators=(",", ":"))
        handle.write("\n")
    print(f"web assets stamped {args.version}")


if __name__ == "__main__":
    main()
