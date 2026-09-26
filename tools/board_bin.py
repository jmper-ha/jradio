#!/usr/bin/env python3
"""Writes build/board.bin: the board board_options.h describes, as the
firmware's `board` partition holds it.

`idf.py flash` writes this image every time, so for someone who builds the
firmware themselves board_options.h stays the one place a board is described:
whatever the partition held - a file from the web flasher, say - is replaced
by the header's board on the next flash.

The values come from the C preprocessor, not from reading the header as text:
board_options.h can include board_options.local.h, #undef what it set, and
leave out any optional part, and only the preprocessor gets all of that right.
The compiler is the one the firmware is built with, which every machine that
runs idf.py has - Windows included, where there is no host C compiler.

What is written here has to be what board_config_to_csv() writes for
board_config_compiled() - same keys, same order, same spelling. The host tests
compare the two (tests/run_host_tests.sh); change one and the other with it.

Usage: board_bin.py --cc <compiler> -I <dir> [-I <dir> ...] --out <board.bin>
       board_bin.py --cc <compiler> -I <dir> ... --csv     (the text, on stdout)
"""

import argparse
import re
import struct
import subprocess
import sys
import zlib

MAGIC = b"JRBD"
VERSION = 1
PARTITION_SIZE = 4096
HEADER_SIZE = 16

PROBE = '#include "board_parts.h"\n#include "board_options.h"\n#include "board_input.h"\n'

# In board_config.c's order; the choices are the ones it offers.
DISPLAYS = [
    "st7796s_480_320", "st7796s_320_480", "ili9488_480_320", "ili9488_320_480",
    "ili9341_320_240", "ili9341_240_320", "st7789_320_240", "st7789_240_320",
    "st7789_320_170",
]
DACS = ["pcm5102"]


def read_macros(cc, include_dirs):
    command = [cc, "-E", "-dM", "-x", "c", "-"]
    for directory in include_dirs:
        command += ["-I", directory]
    result = subprocess.run(command, input=PROBE, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit("board_bin.py: the preprocessor failed:\n" + result.stderr)
    macros = {}
    for line in result.stdout.splitlines():
        match = re.match(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(.*))?$", line)
        if match and "(" not in match.group(1):
            macros[match.group(1)] = (match.group(2) or "").strip()
    return macros


def value(macros, name, depth=0):
    """The integer a macro comes to, or None when it is not defined."""
    if name not in macros or depth > 16:
        return None
    text = macros[name]
    # Casts and outer parentheses: ((int8_t)-1), (5), ...
    text = re.sub(r"\(\s*(?:u?int\d+_t|int|unsigned)\s*\)", "", text).strip()
    while text.startswith("(") and text.endswith(")"):
        text = text[1:-1].strip()
    negative = text.startswith("-")
    if negative:
        text = text[1:].strip()
    try:
        number = int(text.rstrip("uUlL"), 0)
    except ValueError:
        number = value(macros, text, depth + 1)
        if number is None:
            return None
    return -number if negative else number


def pin(macros, name):
    number = value(macros, name)
    return "none" if number is None or number < 0 else str(number)


def switch(macros, name, default):
    number = value(macros, name)
    return str(default if number is None else (1 if number else 0))


def choice(macros, name, prefix, names):
    number = value(macros, name)
    for candidate in names:
        if value(macros, prefix + candidate.upper()) == number and number is not None:
            return candidate
    return ""


def board_csv(macros):
    defined = lambda name: name in macros  # noqa: E731
    sd = defined("SDC_CS_GPIO")
    usb = defined("USB_DP_GPIO") and defined("USB_DM_GPIO")
    bt = value(macros, "BLUETOOTH") == value(macros, "BLUETOOTH_JRADIO_BT") and defined("BLUETOOTH")
    reset = pin(macros, "TFT_RESET_GPIO") if defined("TFT_RESET_GPIO") else "rst"
    feature_on = value(macros, "FEATURE_ON")
    rows = [
        ("board_format", "1"),
        ("board_name", ""),
        ("module", "esp32s3_n16r8"),
        ("display", choice(macros, "DISPLAY", "DISPLAY_", DISPLAYS)),
        ("spi2_sclk", "12"),
        ("spi2_mosi", "11"),
        ("spi3_sclk", pin(macros, "SDC_SCK_GPIO") if sd else "none"),
        ("spi3_mosi", pin(macros, "SDC_MOSI_GPIO") if sd else "none"),
        ("spi3_miso", pin(macros, "SDC_MISO_GPIO") if sd else "none"),
        ("i2s0_bclk", pin(macros, "I2S_BCLK_GPIO")),
        ("i2s0_lrck", pin(macros, "I2S_LRCK_GPIO")),
        ("i2s0_dout", pin(macros, "I2S_DOUT_GPIO")),
        ("uart1_tx", pin(macros, "BT_UART_TX_GPIO") if bt else "none"),
        ("uart1_rx", pin(macros, "BT_UART_RX_GPIO") if bt else "none"),
        ("tft_spi", "2"),
        ("tft_cs", pin(macros, "TFT_CS_GPIO")),
        ("tft_dc", pin(macros, "TFT_DC_GPIO")),
        ("tft_reset", reset),
        ("tft_backlight", pin(macros, "TFT_BACKLIGHT_GPIO")),
        ("encoder_right", pin(macros, "ENCODER_RIGHT_GPIO")),
        ("encoder_left", pin(macros, "ENCODER_LEFT_GPIO")),
        ("encoder_button", pin(macros, "ENCODER_BUTTON_GPIO")),
        ("encoder_pullups", switch(macros, "ENCODER_USE_INTERNAL_PULLUPS", 0)),
        ("button_sleep", pin(macros, "BUTTON_SLEEP_GPIO")),
        ("button_quick_menu", pin(macros, "BUTTON_QUICK_MENU_GPIO")),
        ("button_prev", pin(macros, "BUTTON_PREV_GPIO")),
        ("button_next", pin(macros, "BUTTON_NEXT_GPIO")),
        ("buttons_pullups", switch(macros, "BUTTONS_USE_INTERNAL_PULLUPS", 0)),
        ("ir_receiver", pin(macros, "IR_RECEIVER_GPIO")),
        ("dac", choice(macros, "AUDIO_DAC", "DAC_", DACS)
         if defined("AUDIO_DAC") else "pcm5102"),
        ("dac_i2s", "0"),
        ("dac_mute", pin(macros, "AUDIO_DAC_MUTE_GPIO")),
        ("amp_enable", pin(macros, "AUDIO_AMP_GPIO")),
        ("amp_on_level", switch(macros, "AUDIO_AMP_ON_LEVEL", 1)),
        ("peripheral_power", pin(macros, "PERIPHERAL_POWER_GPIO")),
        ("peripheral_power_on_level", switch(macros, "PERIPHERAL_POWER_ON_LEVEL", 1)),
        ("usb_dp", pin(macros, "USB_DP_GPIO") if usb else "none"),
        ("usb_dm", pin(macros, "USB_DM_GPIO") if usb else "none"),
        ("sd_spi", "3"),
        ("sd_cs", pin(macros, "SDC_CS_GPIO") if sd else "none"),
        ("bluetooth", "jradio_bt" if bt else "none"),
        ("bt_uart", "1"),
        ("bt_i2s", "0"),
        ("yandex_music", "1" if defined("YANDEX_MUSIC") and
         value(macros, "YANDEX_MUSIC") == feature_on else "0"),
        ("dlna", "1" if defined("DLNA") and value(macros, "DLNA") == feature_on else "0"),
    ]
    return "# key,value\n" + "".join(f"{key},{text}\n" for key, text in rows)


def blob(text):
    payload = text.encode("utf-8")
    if HEADER_SIZE + len(payload) > PARTITION_SIZE:
        sys.exit("board_bin.py: board.csv does not fit in the board partition")
    header = MAGIC + struct.pack("<HHII", VERSION, 0, len(payload), zlib.crc32(payload))
    return header + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cc", required=True)
    parser.add_argument("-I", dest="include_dirs", action="append", default=[])
    parser.add_argument("--out")
    parser.add_argument("--csv", action="store_true")
    args = parser.parse_args()
    text = board_csv(read_macros(args.cc, args.include_dirs))
    if args.csv:
        sys.stdout.write(text)
    if args.out:
        image = blob(text)
        try:
            with open(args.out, "rb") as existing:
                if existing.read() == image:
                    return  # unchanged: leave the timestamp alone
        except OSError:
            pass
        with open(args.out, "wb") as out:
            out.write(image)


if __name__ == "__main__":
    main()
