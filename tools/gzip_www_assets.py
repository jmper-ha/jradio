#!/usr/bin/env python3
"""Pre-compress the web assets staged for the littlefs image.

web_server.c serves a `.gz` beside the original whenever the browser says it
accepts gzip, so the compression happens once here, at build time, and the
device never carries a compressor. The originals are staged too, for clients
that do not ask for gzip.

Python rather than a shell script because the build has to run on Windows,
where neither `bash` nor `gzip` exists - and the CMake target that calls this
was the only place in the whole build that reached outside CMake for an
external program.
"""

import gzip
import sys
from pathlib import Path

# The three kinds of file worth compressing. Everything else in www/ is either
# already compressed or too small to gain from it.
SUFFIXES = (".html", ".js", ".css")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: gzip_www_assets.py <www-dir>", file=sys.stderr)
        return 2

    www_dir = Path(argv[1])
    if not www_dir.is_dir():
        # Not "nothing to do": the staging step copies data/ here first, so a
        # missing directory means the copy went wrong and the image would be
        # built without a web interface at all.
        print(f"gzip_www_assets: not a directory: {www_dir}", file=sys.stderr)
        return 1

    for source in sorted(www_dir.iterdir()):
        if source.suffix.lower() not in SUFFIXES or not source.is_file():
            continue
        raw = source.read_bytes()
        target = source.with_name(source.name + ".gz")
        # No stored name, no timestamp: the same input has to give the same
        # bytes out. web_server.c tags a static asset with a hash of the bytes
        # it serves, so a gzip header carrying this build's clock would change
        # the ETag - and send every browser to re-fetch a file that did not
        # change - on every single build.
        with open(target, "wb") as handle:
            with gzip.GzipFile(fileobj=handle, filename="", mtime=0, mode="wb",
                               compresslevel=9) as compressed:
                compressed.write(raw)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
