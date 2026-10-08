#!/usr/bin/env python3
"""Lay out a release's update files and write ota.json, the manifest a radio
reads to learn there is a new version.

Run by the flasher-site workflow on a version tag, over the nine firmware
builds it has just made. Into --out it puts, for every display:

    ota-<display>.bin              what the radio downloads by itself
    jradio-<version>-<display>.bin the same bytes, for the update by hand

plus the web files twice the same way (ota-www.tar, jradio-<version>-www.tar)
and ota.json. Two names for one file so that GitHub's download counts tell
radios from people: the radios only ever ask for the ota- names.

ota.json is uploaded last, after everything it names: a radio reads it from
releases/latest/download/ota.json, and a manifest that arrived before its
files would send radios to a 404.

The change list comes from doc/changelog.md and changelog.en.md, so there is
one place it is written: the sections of this version and the HISTORY_MAX - 1
released before it, newest first. A radio that skipped a few releases is
shown every one it is about to get, not only the last - it keeps the ones
newer than itself. A tag with no section of its own stops the release here
rather than shipping an update that cannot say what it changes.

Usage:
    ota_manifest.py --version v1.6.0 --builds builds --repo jmper-ha/jradio
        --changelog-ru doc/changelog.md --changelog-en doc/changelog.en.md
        --out release-assets
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys

# The mark components/ota/ota_update.c puts after the app description: an
# image without it is refused by every radio that can update itself, so a
# build that lost it must not be released as an update.
MARK_OFFSET = 24 + 8 + 256
MARK_MAGIC = b"JRD1"


def fail(message):
    sys.exit("ota_manifest.py: " + message)


# Sections in one ota.json: room for a radio a good while behind, at about a
# kilobyte a version in two languages, inside the 24 KB the radio reads.
HISTORY_MAX = 10

RELEASE_HEADING = re.compile(r"^##\s+(v\d+\.\d+\.\d+)(\s|$)")


def version_key(version):
    return tuple(int(part) for part in version[1:].split("."))


def released_versions(path):
    """The vX.Y.Z of every "## vX.Y.Z ..." heading - "Unreleased" is not one."""
    with open(path, encoding="utf-8") as handle:
        return [match.group(1) for match in
                (RELEASE_HEADING.match(line) for line in handle.read().splitlines()) if match]


def changelog_section(path, version):
    """The lines under "## <version> ..." up to the next "## ", trimmed."""
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()
    heading = re.compile(r"^##\s+" + re.escape(version) + r"(\s|$)")
    start = next((index for index, line in enumerate(lines) if heading.match(line)), None)
    if start is None:
        fail("%s has no section for %s - write the changelog before the tag" % (path, version))
    body = []
    for line in lines[start + 1:]:
        if line.startswith("## "):
            break
        # The changelog wraps its items at 80 columns for git; a page lays
        # text out itself, so an indented continuation joins its item.
        if line[:1].isspace() and line.strip() and body and body[-1]:
            body[-1] += " " + line.strip()
            continue
        body.append(line.rstrip())
    while body and not body[0]:
        body.pop(0)
    while body and not body[-1]:
        body.pop()
    if not body:
        fail("the %s section of %s is empty" % (version, path))
    return "\n".join(body)


def describe(path, url):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for piece in iter(lambda: handle.read(65536), b""):
            digest.update(piece)
    return {"url": url, "size": os.path.getsize(path), "sha256": digest.hexdigest()}


def check_mark(path):
    with open(path, "rb") as handle:
        head = handle.read(MARK_OFFSET + 4)
    if head[MARK_OFFSET:MARK_OFFSET + 4] != MARK_MAGIC:
        fail("%s has no display mark at offset %d" % (path, MARK_OFFSET))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", required=True)
    parser.add_argument("--builds", required=True,
                        help="firmware-<display>/ directories, each with jradio.bin and www.tar")
    parser.add_argument("--repo", required=True, help="owner/name on GitHub")
    parser.add_argument("--changelog-ru", required=True)
    parser.add_argument("--changelog-en", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    # A release, not a commit between them: radios compare these.
    if not re.fullmatch(r"v\d+\.\d+\.\d+", args.version):
        fail("%s is not a release version (vX.Y.Z)" % args.version)
    # This version and the ones before it, newest first. The English file may
    # lack an old section the Russian one has; such a version goes out with
    # its Russian text alone rather than not at all.
    older = sorted({version for version in released_versions(args.changelog_ru)
                    if version_key(version) < version_key(args.version)},
                   key=version_key, reverse=True)
    english = set(released_versions(args.changelog_en))
    history = []
    for version in [args.version] + older[:HISTORY_MAX - 1]:
        history.append({
            "version": version,
            "ru": changelog_section(args.changelog_ru, version),
            "en": changelog_section(args.changelog_en, version)
            if version in english or version == args.version else "",
        })

    base = "https://github.com/%s/releases/download/%s/" % (args.repo, args.version)
    os.makedirs(args.out, exist_ok=True)
    builds = sorted(name for name in os.listdir(args.builds) if name.startswith("firmware-"))
    if not builds:
        fail("no firmware-* builds under " + args.builds)

    firmware = {}
    for build in builds:
        display = build[len("firmware-"):]
        source = os.path.join(args.builds, build, "jradio.bin")
        check_mark(source)
        for name in ("ota-%s.bin" % display, "jradio-%s-%s.bin" % (args.version, display)):
            shutil.copyfile(source, os.path.join(args.out, name))
        firmware[display] = describe(source, base + "ota-%s.bin" % display)

    # The web files are the same in every build; the first one's will do.
    www_source = os.path.join(args.builds, builds[0], "www.tar")
    for name in ("ota-www.tar", "jradio-%s-www.tar" % args.version):
        shutil.copyfile(www_source, os.path.join(args.out, name))

    manifest = {
        "format": 1,
        "version": args.version,
        "history": history,
        "www": describe(www_source, base + "ota-www.tar"),
        "firmware": firmware,
    }
    with open(os.path.join(args.out, "ota.json"), "w", encoding="utf-8") as out:
        json.dump(manifest, out, ensure_ascii=False, indent=1)
        out.write("\n")


if __name__ == "__main__":
    main()
