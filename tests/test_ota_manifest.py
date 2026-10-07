"""tools/ota_manifest.py against a fake set of builds and changelogs."""

import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import ota_manifest  # noqa: E402

CHANGELOG_RU = """# История изменений

## v1.6.0 — 20 октября 2026

Новое:
- Обновление по сети.
- Длинный пункт, перенесённый
  на вторую строку.

## v1.5.5 — 6 октября 2026

Новое:
- Темы.
"""

CHANGELOG_EN = CHANGELOG_RU.replace("Новое", "New").replace("Обновление по сети", "Updates")


def image(marked=True):
    head = bytearray(1024)
    head[0] = 0xE9
    if marked:
        head[ota_manifest.MARK_OFFSET:ota_manifest.MARK_OFFSET + 4] = ota_manifest.MARK_MAGIC
    return bytes(head)


def lay_out(root, marked=True):
    for display in ("st7789_320_240", "ili9341_320_240"):
        build = os.path.join(root, "builds", "firmware-" + display)
        os.makedirs(build)
        with open(os.path.join(build, "jradio.bin"), "wb") as handle:
            handle.write(image(marked) + display.encode())
        with open(os.path.join(build, "www.tar"), "wb") as handle:
            handle.write(b"tar")
    for name, text in (("ru.md", CHANGELOG_RU), ("en.md", CHANGELOG_EN)):
        with open(os.path.join(root, name), "w", encoding="utf-8") as handle:
            handle.write(text)


def run(root, version="v1.6.0"):
    ota_manifest.main(["--version", version, "--builds", os.path.join(root, "builds"),
                       "--repo", "owner/jradio",
                       "--changelog-ru", os.path.join(root, "ru.md"),
                       "--changelog-en", os.path.join(root, "en.md"),
                       "--out", os.path.join(root, "out")])


def refused(root, version="v1.6.0"):
    try:
        run(root, version)
    except SystemExit as stop:
        return str(stop)
    return None


def test_every_display_gets_its_two_names_and_one_manifest():
    with tempfile.TemporaryDirectory() as root:
        lay_out(root)
        run(root)
        out = os.path.join(root, "out")
        assert sorted(os.listdir(out)) == [
            "jradio-v1.6.0-ili9341_320_240.bin", "jradio-v1.6.0-st7789_320_240.bin",
            "jradio-v1.6.0-www.tar", "ota-ili9341_320_240.bin", "ota-st7789_320_240.bin",
            "ota-www.tar", "ota.json"]
        with open(os.path.join(out, "ota.json"), encoding="utf-8") as handle:
            manifest = json.load(handle)
        assert manifest["version"] == "v1.6.0"
        # Only this version's section, without its heading.
        # The wrapped item comes out as one line.
        assert manifest["notes"]["ru"] == ("Новое:\n- Обновление по сети.\n"
                                           "- Длинный пункт, перенесённый на вторую строку.")
        assert manifest["notes"]["en"].startswith("New:\n- Updates.\n")
        entry = manifest["firmware"]["st7789_320_240"]
        # The radio's own name, so its downloads are counted apart.
        assert entry["url"] == ("https://github.com/owner/jradio/releases/download/v1.6.0/"
                                "ota-st7789_320_240.bin")
        assert entry["size"] == 1024 + len("st7789_320_240")
        assert len(entry["sha256"]) == 64
        assert manifest["www"]["url"].endswith("/v1.6.0/ota-www.tar")


def test_a_tag_without_a_changelog_section_is_not_released():
    with tempfile.TemporaryDirectory() as root:
        lay_out(root)
        assert "no section for v1.7.0" in refused(root, "v1.7.0")


def test_only_a_release_version_goes_out():
    with tempfile.TemporaryDirectory() as root:
        lay_out(root)
        assert "not a release version" in refused(root, "v1.6.0-3-gabcdef0")


def test_a_build_without_the_display_mark_is_not_released():
    with tempfile.TemporaryDirectory() as root:
        lay_out(root, marked=False)
        assert "no display mark" in refused(root)


if __name__ == "__main__":
    test_every_display_gets_its_two_names_and_one_manifest()
    test_a_tag_without_a_changelog_section_is_not_released()
    test_only_a_release_version_goes_out()
    test_a_build_without_the_display_mark_is_not_released()
    print("ota manifest tests passed")
