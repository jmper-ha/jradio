# jradio

A desktop audio player: internet radio, music from a USB drive and an SD card,
Yandex Music. Driven by a knob and four buttons on the device itself, or from a
browser on a phone or a computer.

*[Русская версия](README.md)*

---

## Specifications

| | |
|---|---|
| SoC | ESP32-S3 in a QFN56 package, 16 MB flash, 8 MB PSRAM |
| Display | over SPI: ILI9341 or ST7789 320x240, ILI9488 480x320 - each of them either way up |
| Audio | PCM5102 DAC over I2S, line output; 16-bit stereo |
| Media | USB host for a flash drive and a microSD slot, FAT16 or FAT32 |
| Network | 2.4 GHz Wi-Fi, up to five saved networks, a web interface on the LAN |
| Controls | a rotary encoder with a push button and four buttons |
| Firmware | ESP-IDF 5.5.x, target `esp32s3` |

Which parts are fitted and how they are wired is one line each in
[`board_options.h`](board_options.h), and that file also decides what ends up on
the home screen. Details in [Hardware](doc/hardware.en.md).

## What it does

| Feature | State | More |
|---|---|---|
| **Internet radio** | works | Your own station list, the track name straight off the air, reconnects when a stream drops |
| **Music from a USB drive** | works | Folders, tags, cover art, seeking, automatic advance to the next track |
| **Music from an SD card** | works | The same; the card is found when the source is opened |
| **Playlists on the media** | works | `.m3u`, `.m3u8` and `.pls` files open as a folder - [more](doc/usage.en.md#playlists-on-the-media) |
| **Yandex Music** | works | "My Wave" and the account's stations, cover art, like and dislike - [more](doc/yandex.en.md) |
| **Web interface** | works | Player, device settings, station list editor, file browser, Wi-Fi - [more](doc/web.en.md) |
| **Clock** | works | Time from the internet, on every screen |
| **Volume** | works | The knob, the web player and Settings; remembered |
| **Autoplay** | works | Starts whatever was playing when the device was switched off |
| **Cover art** | works | From the file's tag, from `cover.jpg` beside the music, from Yandex |
| **Interface language** | partly | The switch exists but relabels the settings screen only |
| **Yandex categories** | not done | Only the account's own stations; there is no catalogue of genres and moods |
| **Bluetooth, FM, DLNA** | not done | No menu entries: the device shows only what it can actually do |

### Formats

| Format | Radio | Files | Notes |
|---|:---:|:---:|---|
| MP3 | yes | `.mp3` | |
| AAC | yes | `.aac`, `.adts` | Raw ADTS only. `.m4a` is AAC in an MP4 container and is not read |
| FLAC | yes | `.flac` | Plays at 24 bits too |
| Ogg | yes | `.ogg`, `.oga` | May hold FLAC, Vorbis or Opus - which one is read from the stream |
| WAV | - | `.wav` | 16-bit, mono or stereo |
| HLS (`.m3u8`) | yes | - | Segments in MP3 or AAC. A stream wrapped in MPEG-TS is not parsed |

Radio works over both HTTP and HTTPS. Seeking is files-only: neither a radio
stream nor a Yandex station has a length or a position to jump to. The details
and the caveats are in [Formats in detail](doc/usage.en.md#formats-in-detail).

## Where the project stands

The device is built and in daily use; everything marked "works" above has been
checked on live hardware, not only in tests.

Limits worth knowing about in advance:

- **The translation is unfinished.** The language switch relabels the settings
  screen only; the rest are in Russian. Deliberately left for last: while the
  screens are still being reworked, the strings would have to be translated
  again.
- **The web interface assumes a trusted LAN.** There is no authentication and
  `Origin` is not checked. Do not expose it to the internet.
- **At most 99 stations** in the playlist and **256 entries** in one directory
  on the media; anything past that is dropped with a warning.
- **exFAT is not supported** - drives of 64 GB and up usually ship formatted
  that way and have to be reformatted.
- **On the board with the ILI9488 480x320 the screen shimmers slightly** on
  mid-tones, worst on album art, more with brightness and under load. The
  firmware has been ruled out by measurement: neither the bus clock, nor the
  animation rate, nor the backlight PWM frequency changes it, and under 5% of a
  frame's pixels are repainted per second. It is the module's supply - see
  [Diagnostics](doc/diagnostics.en.md).

The full list of limits, with numbers, is in
[Diagnostics and limits](doc/diagnostics.en.md).

## Documentation

| Page | About |
|---|---|
| [Using the device](doc/usage.en.md) | First run and Wi-Fi, buttons and gestures, the screens, settings, playlists on the media |
| [Yandex Music](doc/yandex.en.md) | Linking an account, stations, the marks, what the device reports back |
| [Web interface](doc/web.en.md) | Pages, API endpoints, the station list format |
| [Hardware](doc/hardware.en.md) | Pinout, choosing parts, panels and screen layouts, fonts |
| [Setting up the development environment](doc/toolchain.en.md) | VS Code, the ESP-IDF extension, Python and everything else to install |
| [Building, flashing and tests](doc/build.en.md) | Building, flashing, host tests, data on the device |
| [Diagnostics and limits](doc/diagnostics.en.md) | What the log says and how to read it, the full list of limits |
