# Building and flashing

[← README](../README.en.md) · [Русский](build.md)

There are three ways. **From the browser** - the flasher on the project's site:
you describe your board's wiring in the editor, any supported display, nothing
to install. **A ready-made image** - one `esptool.py` command, but for the
README board only. **The full way** - a build from source: it needs the ESP-IDF
toolchain, but anything in the firmware is yours to change.

What to know in either case:

- **The board is flashed through its UART port, not its USB connector.** The
  USB connector on the board belongs to the stick (USB Host), so flashing and
  the log go through a USB-UART bridge. It shows up as `/dev/ttyUSB0` or
  `/dev/ttyACM0` (Linux), `/dev/cu.usbserial-*` (macOS) or `COMn` (Windows). On
  a module with two connectors, take the one labelled UART or COM.
- **The cable must carry data.** A charge-only cable powers the board but no
  port appears - the most common reason for "it won't flash".
- **The flash holds three independent partitions: the app, the wiring and the
  data.** The app is the firmware itself. The wiring (the `board` partition)
  says which parts the board has and on which pins; the firmware reads it at
  every start. The data is the web pages, the station list, the settings, the
  Wi-Fi networks. Updating the app leaves the data alone; rewriting the data
  erases everything the device has remembered. This is repeated below wherever
  it matters.

## From the browser

The flasher is the page
[jmper-ha.github.io/jradio](https://jmper-ha.github.io/jradio/). It needs
**Chrome or Edge on a computer**: from a phone, Firefox or Safari the browser
cannot reach a serial port.

1. **Describe the board** on the Hardware tab: the display, which parts are
   fitted and on which pins. Pins are picked from a list or by clicking the
   module's picture; the checks at the bottom will not let two signals share a
   pin or a part go without one. The draft is kept in the browser.

   The one rule: **everything that is wired has to be in the wiring.** The
   firmware drives only the pins it knows about. If the amplifier's MUTE or the
   DAC's XSMT is wired to a GPIO and the wiring does not say so, there is no
   sound: the level meter moves, the speakers stay silent. If XSMT is pulled up
   on the board itself, leave MUTE empty.
2. **Open the Flasher tab** and check the summary: the board's name, the
   display, what is fitted. The firmware is chosen by the display in the
   wiring.
3. **Connect the board** by its UART port and press **Write the firmware**.
   The browser asks for the port; writing takes about a minute, then the board
   restarts.
4. **On a first install** write the **file system** too, with the second
   button. Without it the radio has no web pages. Afterwards the board opens
   the `jradio-XXXX` access point - see [First boot](usage.en.md#first-boot).

**Updating** is the first button alone: it writes the app and the wiring and
leaves the Wi-Fi networks, the settings and the playlist alone. The second
button erases all of that; if a new version changed the web interface and you
do want it, take a backup first - see
[The data on the device](#the-data-on-the-device).

If something is wrong:

- **No port shows up** - the bridge needs its driver: CP210x or CH340/CH343.
- **The board does not answer** - hold BOOT, tap RESET, let go of BOOT and
  press the button again.
- **On Linux the port is "lost" at once** ("The device has been lost") - if
  `idf.py`, esptool or a serial monitor had the port open before, reconnect the
  cable. Those programs leave the port set up in a way that makes Chrome lose
  it; reconnecting resets it.

## A ready-made image

1. Download `jradio-<version>-full.bin` from a
   [release](https://github.com/jmper-ha/jradio/releases) - it is the whole
   flash: the bootloader, the firmware and the data partition.
2. Install `esptool`: `pip install esptool` (Python 3 required).
3. Connect the board's UART port to the computer and write the image:

   ```bash
   esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 write_flash 0x0 jradio-v1.3.0-full.bin
   ```

   On Windows the port is `COM3` or similar. If esptool cannot connect, hold
   BOOT, tap RESET, release BOOT and try again.

4. After the write the board reboots and opens the `jradio-XXXX` access point -
   see [First boot](usage.en.md#first-boot).

The ready-made image is built for the README board: an ST7796S 480×320
display and the pinout from [Hardware](hardware.en.md). If your display or pins
differ, flash [from the browser](#from-the-browser).

**Updating to a new version** without losing the settings - the app only:

```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 write_flash 0x20000 jradio-v1.3.0-app.bin
```

The data partition stays yours. If the release also changed the web interface,
the device says so on its About screen - then also write
`jradio-<version>-littlefs.bin` at offset `0x620000`, after taking a backup
(see [The data on the device](#the-data-on-the-device)).

## The full way: a build from source

### 1. Install the toolchain

ESP-IDF 5.5.x, target `esp32s3`. The project is verified on 5.5.5. How to set
up VS Code with the ESP-IDF extension, or a bare ESP-IDF in a terminal, is in
[Installing the toolchain](toolchain.en.md). From here on the toolchain is
assumed to be in place.

### 2. Describe your board

Open [`board_options.h`](../board_options.h) at the project root. It is the
only file to edit: it says which parts are fitted and on which pins.

```c
#define DISPLAY   DISPLAY_ST7796S_480_320   // which panel and how it stands
#define AUDIO_DAC DAC_PCM5102

#define TFT_CS_GPIO 10                        // and then the pins
...
```

- **The display** is one of the names in
  [`board_parts.h`](../components/board/include/board_parts.h):
  `DISPLAY_ILI9341_320_240`, `DISPLAY_ST7789_240_320`,
  `DISPLAY_ILI9488_480_320`, `DISPLAY_ST7796S_320_480`,
  `DISPLAY_ST7789_320_170` and so on. The order of the numbers is the
  orientation: `480_320` is landscape, `320_480` portrait.
- **What is not on the board, comment out.** No microSD slot - remove its
  block, and the card is gone from the menu and the web interface. No buttons -
  remove the `BUTTON_*_GPIO` lines. The optional parts are enabled the same
  way: the IR receiver, the Bluetooth module, the amplifier's MUTE pin, the
  peripheral power switch.
- **Features without hardware** - `YANDEX_MUSIC` and `DLNA` - are switched with
  `FEATURE_ON` / `FEATURE_OFF`.

A typo in a part name stops the build with a clear error - a device with a wrong
option does not build, rather than staying silent. Every block is explained in
[Hardware](hardware.en.md).

The build puts the pinout from `board_options.h` into the `board` partition,
and `idf.py flash` writes it **on every flash**. That keeps the file the one
description of your board: wiring written from the browser before is replaced
by it. The display is built into the firmware itself - each has its own screen
layouts.

### 3. Build

In VS Code: `Ctrl+Shift+B`. In a terminal with ESP-IDF activated:

```bash
idf.py build
```

The first build takes a few minutes and **goes online**: the component
manager downloads LVGL, the codecs and the panel drivers, and the littlefs
component installs `littlefs-python` for itself to build the data image. After
that everything lives in `managed_components/` and `build/`, and no network is
needed.

If the network blinks, the build fails with something like
`Could not find a version that satisfies the requirement littlefs-python` -
just run `idf.py build` again; only what is missing is fetched. A proxy is set
with the usual pip variables: `PIP_INDEX_URL`, `PIP_PROXY`.

The result is `build/jradio.bin` (the app) and `build/littlefs.bin` (the data
image).

### 4. Flash for the first time: the app and the data

A new board has no data partition yet, so the first flash writes both. In
VS Code: **Terminal → Run Task → ESP-IDF: First flash (app + data)**. In a
terminal:

```bash
idf.py -p /dev/ttyUSB0 flash            # the bootloader, the partition table, the app
idf.py -p /dev/ttyUSB0 littlefs-flash   # the data partition
```

The port is the board's UART one (see the top of the page). With one board on
the system the VS Code tasks find it themselves; with several, name it in the
`ESPPORT` variable.

Flashed only the app, without the data? The device boots, but the screen
shows nothing and the web interface does not open. Add the data with
`littlefs-flash`.

### 5. Watch the log

```bash
idf.py -p /dev/ttyUSB0 monitor          # exit: Ctrl+]
```

or the **ESP-IDF: Monitor** task. What to look for and how to read the lines
about audio and memory health is in [Diagnostics](diagnostics.en.md).

The port belongs to whoever opened it first: if a flash does not start and the
log is empty, a monitor is almost certainly open in another window.

### 6. Update the app only

Changed the code, built it - flash the app:

```bash
idf.py -p /dev/ttyUSB0 flash
```

The data partition is untouched: the station list, the networks, the settings,
the Yandex link and the learned remote live on. This is the normal working
cycle.

The data partition is rewritten only when it changed itself - the web pages
in `data/www/` or the default station list. Then it is `littlefs-flash`, but a
backup first, see below.

## The data on the device

The `littlefs` partition holds together:

- the web interface - from `data/www/` (gzipped at build time);
- the default station list `data/config/stations.csv` and the station
  pictures `data/radio_img/`;
- the default settings `data/config/settings.csv`;
- what the device itself creates: the Wi-Fi networks `wifi.json`, the Yandex
  token `yandex.json`, the weather key `weather.json`, the remote's table
  `remote.csv`, the edits to the station list and the settings.

`littlefs-flash` **rewrites the whole partition** - everything in the last
item is gone, and the device comes up after the flash with its setup access
point, as if new. So the order is:

1. **Download a backup:** the settings page → Backup → "Download archive", or
   `curl -O -J http://<ip>/api/backup`. The archive holds the networks, the
   settings, the Yandex token, the weather key and the keys the remote was
   taught.
2. **Save the station list separately:** the playlist page → Export, or
   `curl http://<ip>/api/playlist > stations.csv`. It is not in the archive.
3. Flash: `idf.py -p PORT littlefs-flash`.
4. **Restore:** Backup → Restore, or
   `curl -X POST --data-binary @jradio-*.zip "http://<ip>/api/restore?name=backup.zip"`.
   The device reboots with its networks and settings back.
5. Bring the station list back with Import on the playlist page.

The keys the remote was taught are in the backup and come back with it.

Everything under `data/` goes into the image and onto every board you flash -
keep passwords, tokens and keys out of it. The device's own files (`wifi.json`,
`yandex.json`, `weather.json`) are not in Git.

## Versions and releases

The firmware version is not typed in by hand: it comes from `git describe` at
build time and shows in the boot log and on the About screen. Tag your
commits - without tags the version is a short hash that says nothing about
which build is newer:

```bash
git tag -a v1.4.0 -m "what is new"
git push origin v1.4.0        # a plain git push does not send tags
```

A new tag is picked up at the next reconfiguration - after `git tag`, run
`touch CMakeLists.txt` and build again.

The web interface has a version of its own: it lives in the data partition and
is written by a different command. The device shows both on the About screen
and warns when they differ - time to update the data partition.

**To build a release** - the downloadable files in `release/<version>/`:

```bash
touch CMakeLists.txt && idf.py build
bash tools/release.sh
```

The script collects the app, the bootloader, the partition table, the data
image and all of them merged into one file for offset 0, with checksums and a
short instruction. It builds the data image afresh without `wifi.json`,
`yandex.json` and `weather.json` - somebody else's board must not get your
passwords. On a dirty tree, or when the version in the binary does not match
the tag, the script refuses.

## Tests

```bash
bash tests/run_host_tests.sh
```

Runs on Linux, macOS or WSL; ESP-IDF need not be activated (the script finds
its cJSON sources itself). The tests compile the real component files with
`-Werror` and sanitizers: format parsing, state machines, screen derivation.
The browser JavaScript is tested under Node without npm or bundlers. One test
runs by its own line from the script, or directly:
`node tests/test_web_playlist.js`.

## If something goes wrong

| What you see | Cause | What to do |
|---|---|---|
| `Failed to connect to ESP32-S3` | a charge-only cable, a busy port, or the board is not in bootloader mode | change the cable; close the monitor; hold BOOT, tap RESET, release BOOT |
| `Permission denied: '/dev/ttyUSB0'` (Linux) | the user is not in the port's group | `sudo usermod -aG dialout $USER` and log in again |
| `Could not find a version that satisfies the requirement littlefs-python` | the network blinked while building the data image | run `idf.py build` again |
| Blank screen, no web interface, yet the device boots | only the app was flashed | `idf.py littlefs-flash` or the "First flash" task |
| Black screen or garbage | the wrong panel or orientation in `board_options.h` | check `DISPLAY` and the TFT pins |
| The About screen says the versions differ | the app was updated, the data was not | backup → `littlefs-flash` → restore |
| `idf.py: command not found` | the toolchain is not activated in this terminal | `source <idf>/export.sh` or the VS Code tasks - see [Installing](toolchain.en.md) |
