# jRadio

An audio player on the ESP32-S3: internet radio, music from a USB stick
and an SD card, a media server on the home network, Yandex Music and
Bluetooth. Driven by the encoder and buttons on the box, by an infrared remote,
or from a browser - a phone or a computer on the same network.

*[Русская версия](README.md)*

---

## Features

- **Everything in one box.** Radio, files, DLNA, Yandex Music, Bluetooth both
  ways - one player instead of several devices.
- **Built from ordinary parts.** An ESP32-S3 module, an SPI display, a PCM5102
  DAC, an encoder and buttons. No custom board needed - a breadboard or a
  simple PCB will do.
- **Flashed from the browser for your board.** The display, the pinout, what
  is fitted and what is not - set in the
  [editor on the site](https://jmper-ha.github.io/jradio/) and written with the
  firmware, nothing to install. For a build from source the same lives in
  [`board_options.h`](board_options.h). What is not on the board is not in the
  menu.
- **A web interface out of the box.** The player, a station-list editor with
  pictures, settings, files, remote learning - all from a phone, no app.
- **Made for every day.** Resumes what was playing, deep sleep, an alarm
  clock, a sleep timer, a clock and the weather on screen, a screensaver.
- **Open source with a straightforward build.** ESP-IDF, VS Code, one build
  key. Ready-made firmware files are in the
  [releases](https://github.com/jmper-ha/jradio/releases) and on the
  [flasher site](https://jmper-ha.github.io/jradio/).

## What it is made of

| | |
|---|---|
| Processor | an ESP32-S3 module, 16 MB flash, 8 MB PSRAM |
| Display | over SPI: ILI9341 or ST7789 320×240, ILI9488 or ST7796S 480×320 - landscape or portrait; ST7789 320×170 (the 1.9" module) |
| Audio | PCM5102 DAC over I2S, line out, 16-bit stereo; optional control of an amplifier's MUTE |
| Media | a USB stick (USB Host) and microSD; FAT16/FAT32 |
| Network | 2.4 GHz Wi-Fi, up to five saved networks |
| Controls | an encoder with a button, four buttons, an IR remote (any, learned) |
| Bluetooth | through the [jradio-bt](https://github.com/jmper-ha/jradio-bt) module on a second ESP32 |
| Firmware | ESP-IDF 5.5.x, target `esp32s3` |

The full pinout and the choice of parts are in [Hardware](doc/hardware.en.md).

## What it does

| | |
|---|---|
| **Internet radio** | Your own station list (up to 99), the track title from the stream, HTTP and HTTPS, reconnects after a drop |
| **Music from USB and SD** | Folders, tags (including Russian ones in legacy encodings), covers, seeking, auto-advance to the next track. `.m3u`, `.m3u8` and `.pls` playlists open as folders, albums with a `.cue` - track by track |
| **Yandex Music** | "My Wave" and the account's stations, covers, like / dislike - [more](doc/yandex.en.md) |
| **Media server (DLNA)** | Finds the server on the network itself, walks the library, tags and covers - [more](doc/dlna.en.md) |
| **Bluetooth** | Receives from a phone (track, cover, buttons, volume) and plays out to a Bluetooth speaker or headphones - [more](doc/usage.en.md#bluetooth) |
| **Web interface** | The player, settings, the station editor, files, Wi-Fi, the remote, a backup - [more](doc/web.en.md) |
| **Remote control** | Any IR remote: keys are learned in the browser, digits dial a station number, a learned key wakes the device from sleep - [more](doc/usage.en.md#the-remote-control) |
| **Clock and weather** | Time from the internet, the temperature and a sky icon next to the clock (Open-Meteo, wttr.in or OpenWeatherMap) |
| **Screensaver** | Dimming, a black screen, or a drifting clock with the date, the weather and the track title |
| **Quick panel** | A window over the player: the sleep timer, the alarm, the brightness, the BT speaker - without leaving the screen |
| **Alarm clock** | A station at a set time on chosen weekdays at its own volume; a sleeping device wakes itself |
| **Sleep timer** | 15-120 minutes, then a fade-out and sleep |
| **Deep sleep** | Holding a button shuts the device down; the same button or the remote wakes it. With a power switch the whole periphery goes dark |
| **Resume** | After power-on continues what was playing: the station, the track, the server folder |
| **Two languages** | Russian and English - on the screen and in the browser, switched on the fly |

Not there yet: FM radio and the general Yandex Music catalogue of genres (only
the account's stations).

### Formats

| Format | Radio | Files | Notes |
|---|:---:|:---:|---|
| MP3 | yes | `.mp3` | |
| AAC | yes | `.aac`, `.adts` | ADTS only; `.m4a` (an MP4 container) is not read |
| FLAC | yes | `.flac` | 24-bit included |
| Ogg | yes | `.ogg`, `.oga` | FLAC, Vorbis or Opus inside |
| WAV | - | `.wav` | 16-bit, mono or stereo |
| HLS (`.m3u8`) | yes | - | MP3 or AAC segments; MPEG-TS is not parsed |

## Getting started

1. **Build the board** after [Hardware](doc/hardware.en.md) - or start with the
   minimum: the module, a display, the DAC and an encoder.
2. **Flash it.** The easiest is [from the browser](https://jmper-ha.github.io/jradio/):
   describe the wiring in the editor and write the firmware - all it takes is
   Chrome or Edge on a computer. There is also a ready-made image for the
   README board and a build from source. All three are in
   [Building and flashing](doc/build.en.md); setting up the tools is in
   [Installing the toolchain](doc/toolchain.en.md).
3. **Connect it to Wi-Fi.** On the first boot the device opens its own access
   point, `jradio-XXXX`: join it from a phone, open `http://192.168.4.1` and
   pick your home network. Then see [How to use it](doc/usage.en.md).

## Worth knowing up front

- **The web interface is meant for a trusted home network.** There is no
  password; do not expose it to the internet.
- **At most 99 stations** in the list and **256 files** in one folder of a
  drive.
- **exFAT is not supported.** Sticks and cards of 64 GB and up usually come
  formatted that way - reformat to FAT32.
- **Flashing goes through UART**, not through the board's USB connector: USB
  belongs to the stick.

The full list of limits is in [Diagnostics and limits](doc/diagnostics.en.md).

## Documentation

| Page | About |
|---|---|
| [How to use it](doc/usage.en.md) | First boot, controls, screens, sources, settings, sleep and the alarm |
| [Building and flashing](doc/build.en.md) | Flashing from the browser, a ready image or a build from source, the first flash, updates, the data on the device, tests |
| [Installing the toolchain](doc/toolchain.en.md) | VS Code, the ESP-IDF extension, Python, the board's port |
| [Hardware](doc/hardware.en.md) | Parts, pinout, the wiring editor and `board_options.h`, displays, the amplifier, the remote, the Bluetooth module |
| [Web interface](doc/web.en.md) | Pages, the API, the station list format, the backup |
| [Yandex Music](doc/yandex.en.md) | Linking the account, stations, likes |
| [Media server (DLNA)](doc/dlna.en.md) | Finding the server, walking the library, resume |
| [Diagnostics and limits](doc/diagnostics.en.md) | What the log says, how to read it, the limits |
