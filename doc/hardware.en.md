# Hardware

[← README](../README.en.md) · [Русский](hardware.md)

ESP32-S3 in a QFN56 package, 16 MB flash, 8 MB PSRAM; a display over SPI -
an ILI9341 or ST7789 320x240, or an ILI9488 480x320, each of them either way
up; a rotary encoder with a push button and four buttons; a
PCM5102 DAC over I2S with a line output; a USB host port for a FAT-formatted
drive; a microSD slot over SPI.

The wiring lives in [`board_options.h`](../board_options.h), which is the source
of truth; the table below is for convenience. Each part is chosen on one line,
and everything that follows from that choice sits in profiles beside the
driver:

```c
#define DISPLAY   DISPLAY_ILI9341_320_240
#define AUDIO_DAC DAC_PCM5102
```

The parts with drivers are listed in
[`board_parts.h`](../components/board/include/board_parts.h). A typo fails the
build rather than producing a device that misbehaves.

Which way up the panel stands is part of its name too: `DISPLAY_ILI9341_240_320`
is the same module on its end. The orientation decides the layout of all six
screens and cannot be derived from the wiring - the same reason the resolution
was already in the name.

In portrait the station list shows seven rows instead of five, Settings nine
instead of six, the home carousel three tiles instead of five, and on the
player screen the cover art and the three stream readings stand side by side
above the names rather than beside them. This is a
build option and not a setting: each screen's geometry is compiled for one
shape, and a box is mounted one way round once. The boot splash has a version
for each panel shape and all of them are compiled in, so nothing needs regenerating.

The panel does not have to be an ILI9341. Each controller has a profile of its
own in
[`components/board/include/display/`](../components/board/include/display/) and a
driver file in `components/board/display/`; `board.c` does not know which one
was selected - it calls `board_panel_create()` and works through the common
esp_lcd interface from there. Besides the ILI9341 the catalogue carries the
ST7789 320x240, in two orientations as well; its driver ships inside ESP-IDF,
so it needs no dependency of its own. The landscape profile was taken off a
panel: RGB colour order, inversion off - against the usual advice for an
ST7789, which with INVON renders the whole screen as a negative - and one
horizontal mirror. Portrait has not been built yet and
[`st7789.h`](../components/board/include/display/st7789.h) marks it as derived
rather than measured. The driver that was not selected costs no flash at all.

The third panel in the catalogue is an ILI9488 480x320, measured on the board
on 2026-09-04. It takes the same six wires as the ILI9341 but differs in one
place, and the difference is not a detail: over SPI this controller accepts
18-bit colour only. Three bytes per pixel instead of two, with the RGB565 to
RGB666 conversion done by the driver (`atanisoft/esp_lcd_ili9488` from the
registry - ESP-IDF carries no ILI9488 driver of its own), and the profile
declares `TFT_PIXEL_WIRE_BYTES` and `TFT_PIXEL_BYTE_SWAP`, from which `board.c`
sizes the SPI transfer and decides whether to swap the bytes. Firmware built
for an ILI9341 will not drive this panel at all - not because the resolution
differs but because of those three bytes. A frame costs 460 800 bytes here
against 153 600, and that is the whole reason this panel's bus runs at 40 MHz
where the others run at 20: 92 ms a frame instead of 184. A full repaint of
the screen measures 249 ms, of which 157 is the flush. 40 MHz is above the
ILI9488's own datasheet figure of 20; it holds because the SPI2 pins here are
the ESP32-S3's IOMUX ones, and the check is the picture itself, since nothing
ever reads this bus. The band was narrowed to ten rows so the buffers cost the
same internal memory as they do on the narrow panel. The portrait
`DISPLAY_ILI9488_320_480` builds too: the 320x480 layout exists and has been
seen on the panel.

The screen layout lives in [`ui_layout.h`](../components/ui/include/ui_layout.h)
and is checked by a host test. Everything that follows from the panel's size -
how many rows the list and Settings hold, how wide the level meter's blocks are,
how many tiles the carousel shows - is computed from `TFT_WIDTH` and
`TFT_HEIGHT` rather than picked per orientation. The numbers that were placed by
eye on a screen (the carousel axis, the player's rows, the footer) sit in one
file per panel shape under [`layout/`](../components/ui/include/layout/): deriving
them was tried and does not work - centring the carousel misses the two measured
positions by 7 px and 3 px, in opposite directions.

A panel of a new size is a copy of that file and one line in the dispatcher; a
shape with no file fails the build with a message saying what to create. That is
how both ILI9488 shapes came about. The numbers follow the faces a shape asks
for rather than its resolution: 480x320 asks for 18 and 26 px instead of 14 and
20 and gets the same 5 list rows and 6 settings rows the narrow panel gets,
while portrait 320x480 at those same faces gets 9 and 10.

Fonts are part of the shape too. Screens ask for a role rather than a size -
body, title, icon, display
([`ui_fonts.h`](../components/ui/include/ui_fonts.h)) - and which size each role
gets is the shape file's decision. The Cyrillic faces are made by
[`tools/gen_ui_fonts.sh`](../tools/gen_ui_fonts.sh) from DejaVu Sans, because
LVGL's built-in Montserrat is Latin only; the icon faces are Montserrat
precisely because what is drawn with them is not text but the symbol block
bundled into it, and a missing `CONFIG_LV_FONT_MONTSERRAT_<size>` fails the
build with a message instead of failing at the link.

The faces cover more than the interface's own language. The UI is Russian, but
file names, ICY titles and tags arrive as whoever wrote them wrote them, so
accented Latin reaches the screen as readily as Cyrillic - and a character with
no glyph is drawn by LVGL as a box, which makes an album read as damaged rather
than as unsupported. So the faces carry Latin-1 Supplement, Latin Extended-A,
the punctuation a tagger reaches for, and the combining marks 0x0300-0x030F.
The last of those because a name can arrive decomposed - "u" followed by a
separate diaeresis - and FATFS hands it over exactly as it was written. The
converter gives such a mark zero advance and a negative offset, so LVGL draws it
back over the letter before it and nothing has to be composed. The Cyrillic
bound is the one thing left where it was: the 14 px and 20 px faces do not agree
on it, and 0x0460-0x048F by itself raises the 20 px line height from 23 to 26.

Each face's line height is copied into
[`ui_font_metrics.h`](../components/ui/include/ui_font_metrics.h): the layout has
to be computed before there is a program, and a font knows its own metrics only
once LVGL is up. `ui.c` checks the copy against the font at start-up and logs
the disagreement. Checked both ways: a face no layout selects is dropped by the
linker - a build carrying a spare 18 px face came out the same size to the byte
- and pointing the panel at that face instead moves the line height to 24 and
every row spacing with it. It earned its keep at once: widening the character
set moved the 20 px face from 23 to 24, and the row spacings followed on their
own.

The same file names the parts this board does not carry: an FM tuner and
Bluetooth. Their blocks are commented out rather than deleted - the file should
answer "can this firmware drive one" with a no as well as with a yes. For
Bluetooth the answer is final: the ESP32-S3 has no classic Bluetooth, only BLE,
and A2DP is a classic-Bluetooth profile, so playing from a phone would need a
receiver module of its own.

| Signal | GPIO | | Signal | GPIO |
|---|---:|---|---|---:|
| TFT CS | 10 | | Encoder button | 6 |
| TFT DC | 47 | | F1 | 45 |
| TFT MOSI | 11 | | F2 | 21 |
| TFT SCLK | 12 | | F3 | 46 |
| Backlight | 2 | | F4 | 9 |
| Encoder right | 5 | | PCM5102 DOUT | 16 |
| Encoder left | 7 | | PCM5102 BCLK | 18 |
| USB D- | 19 | | PCM5102 LRCK | 17 |
| USB D+ | 20 | | microSD CS | 1 |
| microSD SCK | 41 | | microSD MISO | 40 |
| microSD MOSI | 42 | | | |

The display's RST is tied to the ESP32's reset and takes no GPIO. On modules
that bring RST out separately the pin is declared by a `TFT_RESET_GPIO` line;
`board_options.h` carries one, commented out. The ST7789 tried here did not
need one, though these boards are known to.

Worth knowing if you build the board:

- there is no MISO to the display, the bus is one-way; the panel wants BGR
  order;
- the PCM5102 runs without MCLK, I2S slots are 16-bit stereo, BCLK = 32 x Fs.
  The DMA ring of 8 x 512 frames (~93 ms) was chosen to stop clicking and must
  not be shrunk;
- **tie the PCM5102 module's SCK to ground and XSMT to 3.3 V.** Left floating
  they produce rare dropouts with a perfectly healthy digital path;
- debouncing is done in software. Internal pull-ups are enabled for the buttons
  (GPIO 45, 46 and 21 are unstable without them) and disabled for the encoder,
  where the external ones suffice;
- USB VBUS is permanently powered; a drive left on the bus across a reboot is
  re-enumerated by a logical power cycle of the root port;
- the microSD slot has an SPI bus of its own, SPI3: the display has no MISO
  wired, which a card cannot work without. GPIO 40, 41 and 42 are the external
  JTAG pins, and the card takes them over;
- the slot has no card-detect line, so an inserted card can only be found by
  trying to mount it - which is also why the card is not held mounted, see
  [Limits](diagnostics.en.md#limits). FAT16/FAT32 only, no exFAT.

## What the home screen shows

The home screen shows exactly what the firmware can make use of, and nothing
else. `board_options.h` decides, in two different ways:

- **hardware**, by its wiring. USB, microSD, an FM tuner: a block of pins is
  there, so the source is there. Comment the block out or delete it, and the
  entry disappears from the device's screen, from the web interface and from
  autoplay;
- **features**, by a `FEATURE_ON` / `FEATURE_OFF` line. That is how
  `YANDEX_MUSIC` and `DLNA` are declared; a missing line means `FEATURE_OFF`.

Internet radio and Settings are always there: the first needs nothing beyond
the Wi-Fi already on the chip, and without the second there would be no way to
configure the device. Yandex Music has a second step as well - the switch in
Settings, which hides the source in a firmware that was built with it.

One case is its own: a build with nothing left but the radio and Settings. A
home screen of two rows offers no choice, so there is none at all - the device
starts straight into the station list, and a long press (or F2) switches
between the list and Settings. Going into Settings stops the radio, exactly as
a long press on the player screen does. The "Home screen" row in Settings is
hidden in such a build too: there is nothing to choose between.
