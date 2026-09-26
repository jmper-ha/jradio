# Hardware

[← README](../README.en.md) · [Русский](hardware.md)

## Parts

| Part | What fits | Required |
|---|---|---|
| Processor | an ESP32-S3 module with 16 MB flash and 8 MB PSRAM (e.g. ESP32-S3-WROOM-1 N16R8) | yes |
| Display | over SPI: ILI9341 or ST7789 320×240, ILI9488 or ST7796S 480×320, ST7789 320×170 | yes |
| Encoder | rotary, with a push button | yes |
| DAC | PCM5102 (a module with line out) | yes |
| Buttons | four tactile: sleep, quick panel, previous, next | no |
| USB stick | a USB-A socket on the D−/D+ pins | no |
| microSD | a slot over SPI | no |
| IR receiver | a three-pin 38 kHz one: TSOP38238, VS1838B, HX1838 | no |
| Bluetooth | a second ESP32 module (WROOM-32) running [jradio-bt](https://github.com/jmper-ha/jradio-bt) | no |
| Amplifier | any with a MUTE / SD input - the firmware mutes it on pause | no |
| Peripheral power switch | a load switch or a P-MOSFET - cuts the periphery in sleep | no |

Everything optional is up to you: what is not on the board is not in the menu
nor in the web interface.

## How to describe the board

The board's description - which display, what is fitted and on which pins -
is called the wiring. The firmware reads it at every start. There are two
ways to set it:

- **In the editor on the site** - the Hardware tab at
  [jmper-ha.github.io/jradio](https://jmper-ha.github.io/jradio/). Pins are
  picked from a list or by clicking the module's picture, and the checks at
  the bottom will not let two signals share a pin or a part go without one.
  The wiring is written to the board together with the firmware, from the same
  browser - see [Building and flashing](build.en.md#from-the-browser). Nothing
  to install.
- **In [`board_options.h`](../board_options.h)** - for a build from source,
  see below. `idf.py flash` writes the wiring from this file on every flash.

One rule for both: **everything that is wired has to be in the wiring.** The
firmware drives only the pins it knows about. An amplifier's MUTE or a DAC's
XSMT wired to a GPIO but left out of the wiring leaves the board silent - the
level meter moves, the speakers do not. An optional part that is not
physically there is the opposite: switch it off, and the sound is not
affected.

## board_options.h

For a build from source the board is described in
[`board_options.h`](../board_options.h) at the project root. A part is chosen
with one line, the pinout with one line per pin:

```c
#define DISPLAY   DISPLAY_ST7796S_480_320
#define AUDIO_DAC DAC_PCM5102
#define TFT_CS_GPIO 10
...
```

The valid part names are listed in
[`board_parts.h`](../components/board/include/board_parts.h). A typo stops the
build instead of giving a silent device.

**An optional part is enabled by its block of lines and disabled by removing
it** (or commenting it out): no microSD block - no card in the menu; no
`BUTTON_*_GPIO` lines - the firmware builds with the encoder alone. Features
without hardware - Yandex Music and DLNA - are switched with the
`YANDEX_MUSIC` and `DLNA` lines set to `FEATURE_ON` or `FEATURE_OFF`.

## The default pinout

| Part | GPIO | | Part | GPIO |
|---|---:|---|---|---:|
| TFT CS | 10 | | Encoder button | 6 |
| TFT DC | 47 | | Encoder A | 5 |
| TFT MOSI | 11 | | Encoder B | 7 |
| TFT SCLK | 12 | | Sleep button | 21 |
| Backlight | 2 | | Quick_menu button | 45 |
| PCM5102 DOUT | 16 | | Prev button | 46 |
| PCM5102 BCLK | 18 | | Next button | 9 |
| PCM5102 LRCK | 17 | | USB D− | 19 |
| microSD CS | 1 | | USB D+ | 20 |
| microSD SCK | 41 | | IR receiver (optional) | 4 |
| microSD MISO | 40 | | UART for flashing | 43, 44 |
| microSD MOSI | 42 | | | |

What matters when wiring:

- **Flashing goes through UART (GPIO 43, 44), not the USB connector.** USB
  belongs to the stick, and the built-in USB-Serial-JTAG sits on the same pins
  19/20. A USB-UART bridge is needed - most dev modules already have one.
- **The sleep button must be on GPIO 0-21**: only those pins can wake the chip
  from deep sleep. The same goes for the IR receiver if the remote is to wake
  the device.
- The display's RST is tied to the module's reset. If your module brings RST
  out separately, name it with `TFT_RESET_GPIO` (the line is in the file,
  commented out).
- microSD has its own SPI3 bus: the display has no MISO wired, and the card
  cannot work without one. No card-detect is wired - the card is found on
  entering the source.
- Internal pull-ups are on for the buttons (`BUTTONS_USE_INTERNAL_PULLUPS 1`)
  and off for the encoder, where external ones suffice.

## The display

The panel and its orientation are one name: `DISPLAY_ILI9341_320_240` is
landscape, `DISPLAY_ILI9341_240_320` the same module standing on end. The
screens are laid out for each shape separately: portrait lists show more rows,
and the cover stands above the titles rather than beside them.

| Panel | Names | Notes |
|---|---|---|
| ILI9341 320×240 | `DISPLAY_ILI9341_320_240`, `DISPLAY_ILI9341_240_320` | the most common one; both orientations verified |
| ST7789 320×240 | `DISPLAY_ST7789_320_240`, `DISPLAY_ST7789_240_320` | landscape verified, portrait derived |
| ST7789 320×170 (1.9") | `DISPLAY_ST7789_320_170` | landscape only; its own compact layout |
| ILI9488 480×320 | `DISPLAY_ILI9488_480_320`, `DISPLAY_ILI9488_320_480` | 18-bit colour over SPI; the driver from the component registry installs itself; both orientations verified |
| ST7796S 480×320 | `DISPLAY_ST7796S_480_320`, `DISPLAY_ST7796S_320_480` | the project default; landscape verified |

All panels connect with the same six wires (CS, DC, MOSI, SCLK, backlight,
power). Drivers that are not selected do not go into the firmware.

**Power the panel from its own regulator**, e.g. an AMS1117-3.3 fed from +5 V,
not from the ESP32-S3 module's 3V3 pin: the module's LDO cannot carry the panel
along with everything else, and the picture flickers - worse on midtones and
at high brightness. Capacitors on the module's pins do not help; that was
tested.

If the panel stands differently from what was expected, the picture can be
mirrored vertically and horizontally in the device's settings - no rebuild.

## Audio

The **PCM5102** runs over I2S without MCLK: 16-bit stereo slots, BCLK = 32 × Fs.
On the module:

- **Tie SCK to ground and XSMT to 3.3 V.** Left floating, they cause rare
  audio dropouts with a perfectly healthy digital path.
- If XSMT is handed to the firmware (`AUDIO_DAC_MUTE_GPIO`, below), the
  factory jumper to 3.3 V must be cut.

### The amplifier: the MUTE pin

If a speaker amplifier is fitted, hand its MUTE / SD / standby input to the
firmware:

```c
#define AUDIO_AMP_GPIO 39
#define AUDIO_AMP_ON_LEVEL 1     // the level at which the amplifier plays
```

The firmware opens the amplifier only while sound is actually produced - on
pause, on stop and while the sound goes to a Bluetooth speaker it is muted and
does not hiss into the speakers. Two rules against clicks:

- pull the pin with a resistor to the "quiet" side: during reset and the
  first milliseconds of boot nobody drives it;
- the firmware mutes before stopping I2S itself, so a DAC that lost its clock
  does not thump into the speakers.

### Powering the periphery down in sleep

The optional `PERIPHERAL_POWER_GPIO` pin (38, 39 and 48 are free) drives a
switch that feeds everything outside the module: the panel, the DAC, the card,
the Bluetooth module, USB. `PERIPHERAL_POWER_ON_LEVEL` is the level that
opens the switch (1 for a load switch, 0 for a P-channel MOSFET in the
positive rail). In deep sleep the firmware closes the switch and holds it
closed; without this line only the chip sleeps, while the backlight and the
DAC keep drawing.

The switch must carry the whole periphery with margin: the panel with its
backlight and the Bluetooth module draw noticeably more at their peaks than on
average.

## The IR receiver: the remote

Any three-pin 38 kHz receiver gives a full remote control - how to use it is in
[The remote control](usage.en.md#the-remote-control):

```c
#define IR_RECEIVER_GPIO 4
```

Any pin will do, but for the remote to **wake** the device from deep sleep the
receiver must be on GPIO 0-21 and be fed from the board's permanent 3.3 V, not
from the peripheral rail that sleep switches off. It draws a fraction of a
milliamp. Without this line there is no remote - no web page, no receiver
task.

## Bluetooth: the jradio-bt module

The ESP32-S3 has no Bluetooth Classic, and A2DP is a Classic profile. So
Bluetooth is done by a second module - an ordinary ESP32-WROOM-32 running
[jradio-bt](https://github.com/jmper-ha/jradio-bt). It sits on the same I2S
bus as the DAC and is driven over UART. Three lines enable it:

```c
#define BLUETOOTH BLUETOOTH_JRADIO_BT
#define BT_UART_TX_GPIO 13
#define BT_UART_RX_GPIO 14
```

| S3 (jRadio) | WROOM (jradio-bt) | What |
|---|---|---|
| GPIO 13 | GPIO 16 (RX) | UART 921600 8N1 |
| GPIO 14 | GPIO 17 (TX) | |
| GPIO 18 (BCLK) | GPIO 26 | the shared I2S bus, through 33-47 Ω on each side |
| GPIO 17 (LRCK) | GPIO 25 | |
| GPIO 16 (DOUT) | GPIO 22 | the same wire also goes to the DAC's DIN |
| 3V3, GND | 3V3, GND | the module draws up to 200 mA at peaks |

The resistors on the I2S lines are needed: when the bus master changes (the
S3 or the module), both sides can be outputs for an instant. The module
appears in the menu only while it answers over UART - a disconnected or
reflashing module is simply not offered.

For the built-in DAC to stay silent while the sound goes to a Bluetooth
speaker, wire its XSMT pin to a free GPIO:

```c
#define AUDIO_DAC_MUTE_GPIO 15
```

On the purple PCM5102 modules this is the XMT pad, pulled to 3.3 V by a jumper
- cut the jumper and connect the pad to the GPIO (1 kΩ in series is fine).
Without this line the DAC simply always plays.

## What the home screen shows

The home screen shows exactly what is on the board and enabled in the build:

- **hardware** - by the wiring: a USB or microSD block in `board_options.h`
  means a source;
- **features** - by the `YANDEX_MUSIC` and `DLNA` lines.

Internet radio and Settings are always there. If nothing but those two is left
in a build, the home screen is not shown at all: the device boots straight
into the station list, and a long press of the encoder switches between the
list and the settings.
