#!/usr/bin/env python3
"""Pack the staged web files into www.tar, the archive a firmware update
carries its web interface in.

An update over the network writes only the app slot; without this the pages
the device serves stay those of the version before, and the About card says
the two halves disagree. The device unpacks the archive into /littlefs/www and
leaves config/ alone - stations, Wi-Fi and settings are not in it.

What goes in is exactly what components/ota/ota_tar.c accepts: the web stamp
first as version.json, then www/<name> for every staged file, flat, as plain
ustar. Sorted, with the times and owners zeroed, so one commit packs to the
same bytes on any machine.

Run from the littlefs_stage target in the root CMakeLists.txt, after the
gzip pass and the stamp, under ESP-IDF's Python for the reason that target
gives.

Usage:
    python3 tools/pack_www.py --stage build/littlefs_data --out build/www.tar
"""

import argparse
import io
import os
import tarfile


def add(archive, name, data):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--stage", required=True, help="the staged copy of data/")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    www = os.path.join(args.stage, "www")
    with open(os.path.join(args.stage, "config", "version.json"), "rb") as stamp:
        version = stamp.read()
    temporary = args.out + ".part"
    with tarfile.open(temporary, "w", format=tarfile.USTAR_FORMAT) as archive:
        add(archive, "version.json", version)
        for name in sorted(os.listdir(www)):
            path = os.path.join(www, name)
            if not os.path.isfile(path):
                raise SystemExit("pack_www.py: %s is not a file; the web directory is flat" % path)
            with open(path, "rb") as handle:
                add(archive, "www/" + name, handle.read())
    os.replace(temporary, args.out)


if __name__ == "__main__":
    main()
