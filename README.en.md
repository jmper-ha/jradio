# jradio

A desktop audio player: internet radio, music from a USB drive or an SD card,
and Yandex Music. Driven by a knob and four buttons on the device itself, or
from a browser on your phone or computer.

Built on an ESP32-S3, with a 320x240 screen and a separate DAC with a line
output.

*[Русская версия](README.md)*

---

# What it does

| Feature | State | Details |
|---|---|---|
| **Internet radio** | works | Your own station list, the track name straight off the air, reconnection when a stream drops |
| **Music from a USB drive** | works | Walk through folders, tags, cover art, scrubbing, moves on to the next track |
| **Music from an SD card** | works | The same; the card is recognised when its source is entered |
| **Yandex Music** | works | "My wave" and your account's stations, album art, a next-track button |
| **Web interface** | works | Player, station-list editor, file browsing, Wi-Fi, linking Yandex Music. No volume and no scrubbing on the web |
| **Clock** | works | Time from the internet, on every screen |
| **Volume** | works | On the knob, remembered across restarts |
| **Autoplay** | works | Starts whatever was playing when the power went off |
| **Cover art** | works | From the file's tag, from a `cover.jpg` beside the music, from Yandex |
| **Interface language** | partly | The switch is there, but it only changes the labels on the settings screen itself |
| **Likes in Yandex Music** | not built | There are no like and dislike buttons yet |
| **Yandex Music categories** | not built | Only the account's own stations are shown; there is no general catalogue of genres, moods and epochs |
| **Bluetooth, FM, DLNA** | not built | Their menu entries are there, but they answer "not available yet" |

## Formats

| Format | Radio | Files | Notes |
|---|:---:|:---:|---|
| MP3 | yes | `.mp3` | |
| AAC | yes | `.aac`, `.adts` | Raw ADTS only. `.m4a` is AAC in an MP4 container and cannot be read |
| FLAC | yes | `.flac` | |
| FLAC in Ogg | yes | `.ogg`, `.oga` | Vorbis under the same extensions will not play |
| WAV | - | `.wav` | 16-bit, mono or stereo |
| HLS (`.m3u8`) | yes | - | Segments in MP3 or AAC. A stream wrapped in MPEG-TS is not demuxed |

Radio works over both HTTP and HTTPS. A station whose address ends in `.m3u8`
is not a stream but an index of short files; the device handles such an index
by itself, with nothing to configure.

Only files can be scrubbed: neither a radio stream nor a Yandex station has a
length or a position to move to. Track length is worked out from the file size
and the bitrate - exact for WAV and for constant bitrate, taken from the header
for FLAC, and drifting a little on a variable-bitrate file.

## First run

The device needs Wi-Fi. On its first start it brings up an access point of its
own: connect to it from a phone, open `http://192.168.4.1` and pick your
network. Up to five networks are remembered, and after that it connects on its
own.

The device shows the address of its web interface at the bottom of the settings
screen - that is the only place it can be read.

## Controls

| Gesture | What it does |
|---|---|
| Turn the knob | Volume on the player screen, selection in lists |
| Press | Play and pause |
| Double press | Open the station or file list; the music keeps playing |
| Triple press | Scrub: the knob picks a position, a press applies it |
| Long press | Home screen; playback stops |
| F2 | Back |
| F3 | Play and pause |

A single press lands after a short delay - before that it cannot be told from
the beginning of a double press.

While scrubbing, the knob and the press are busy choosing a position, so the
volume does not change there. Any other button leaves the mode without changing
anything. The music plays on throughout.

## Home screen

Two looks, chosen in the settings: a list of entries or a carousel of large
icons. Every source - radio, drive, card, Yandex Music, settings - has an icon
of its own.

Choosing the drive or the card when neither is there opens an explanation
rather than an empty list: insert the medium, it cannot be read, or it holds no
music - these are different things, and the advice differs.

## Player screen

The clock and the Wi-Fi level are at the top. Below them the cover, the station
name, and the track and performer in large type. At the bottom, the volume
scale, the position bar and a level meter.

Cover art comes from the file itself, and where the file has none, from a
`cover.jpg`, `cover.jpeg` or `cover.png` beside the music. For Yandex Music the
service provides it. With no picture anywhere, an icon stays in its place.

Names for files are read from their tags, including Russian ones in older
encodings. Whatever the tags do not say is replaced by what is known: the
folder in place of the album, the file name in place of the title.

## Yandex Music

An active subscription is required.

**Linking an account.** The device shows an address and a short code: open the
address on a phone or a computer and enter the code. No password is typed on
the device - it is never transmitted and never stored there.

**Stations.** The ones Yandex itself offers your account: "My wave" and a few
picked for your taste. The list is personal, Yandex composes it, and it is the
same list the phone app shows.

**What you can do.** Play a station, pause it, skip to the next track (with the
button in the web interface). There is no previous-track button - a station
only moves forward, and there is no going back to a track already played.

The device does not tell Yandex what you listened to or skipped. That does not
affect the music, but it means the device does not shape your recommendations
either - those follow only what you play in other apps.

An account can be unlinked with F4 on the Yandex Music screen, or from the web
interface.

## Settings

Language, the look of the home screen, autoplay, flipping the picture
vertically and horizontally, volume. They apply at once and are saved.

The language switch still only changes the labels on the settings screen
itself.

---

# Building and hacking on it

What follows is the technical half: how to build the firmware, how to rebuild
the board, and what to know when changing things.

## Hardware

ESP32-S3 in a QFN56 package, 16 MB flash, 8 MB PSRAM; an ILI9341 320x240
display over SPI; a rotary encoder with a push button and four buttons; a
PCM5102 DAC over I2S with a line output; a USB host port for a FAT-formatted
drive; a microSD slot over SPI.

The wiring lives in [`board_options.h`](board_options.h), which is the source
of truth; the table below is for convenience. Each part is chosen on one line,
and everything that follows from that choice sits in profiles beside the
driver:

```c
#define DISPLAY   DISPLAY_ILI9341_320
#define AUDIO_DAC DAC_PCM5102
```

The parts with drivers are listed in
[`board_parts.h`](components/board/include/board_parts.h). A typo fails the
build rather than producing a device that misbehaves.

| Signal | GPIO | | Signal | GPIO |
|---|---:|---|---|---:|
| ILI9341 CS | 10 | | Encoder button | 6 |
| ILI9341 DC | 47 | | F1 | 45 |
| ILI9341 MOSI | 11 | | F2 | 21 |
| ILI9341 SCLK | 12 | | F3 | 46 |
| Backlight | 2 | | F4 | 9 |
| Encoder right | 5 | | PCM5102 DOUT | 16 |
| Encoder left | 7 | | PCM5102 BCLK | 18 |
| USB D- | 19 | | PCM5102 LRCK | 17 |
| USB D+ | 20 | | microSD CS | 1 |
| microSD SCK | 41 | | microSD MISO | 40 |
| microSD MOSI | 42 | | | |

The display's RST is tied to the ESP32's reset and takes no GPIO.

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
  "Limits". FAT16/FAT32 only, no exFAT.

## Building and flashing

ESP-IDF 5.5.x, target `esp32s3`.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash              # application only, leaves LittleFS alone
idf.py -p PORT littlefs-flash     # overwrites the whole data partition
idf.py -p PORT monitor
```

`littlefs-flash` **destroys user data** - playlist edits, saved networks,
device settings and the Yandex Music account link: all of it lives on that one
partition. Compare the live state first: `curl http://<ip>/api/playlist` and
`curl http://<ip>/api/yandex`. There is nothing that can read the settings
back - they return to their defaults, and the account has to be linked again.

## Tests

```bash
bash tests/run_host_tests.sh
```

65 suites, no ESP-IDF activation needed. They compile the real component
sources rather than mocks, with `-Werror` and the address and undefined
behaviour sanitizers. That is why format parsing, state machines and view
derivation live in files with no ESP-IDF dependencies - new logic belongs
there. Browser JavaScript runs under Node against a hand-written fake DOM, with
no npm and no bundler.

## Web interface

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Wi-Fi and player state in one snapshot |
| `GET /api/playlist` | Station list as CSV |
| `POST /api/playlist` | Replaces the station list wholesale |
| `GET /api/files` | Contents of the current directory on the active medium |
| `GET /api/yandex` | Link state and the account's stations (never the token) |
| `POST /api/yandex` | Link, cancel, unlink, refresh the stations |
| `POST /api/wifi` | Saves a network |
| `/ws` | Commands and live updates |

WebSocket commands: `player.play`, `player.pause`, `player.toggle`,
`player.next`, `source.select`, `list.select`, `browse.up`, `wifi.save`. Live
state arrives as diffs; anything large - the playlist, media directories - goes
over REST, because it does not fit in a frame and must not spend internal SRAM.

**There is no authentication and `Origin` is not checked. Do not expose the
device.**

## Diagnostics

Every 10 seconds the firmware logs two lines about the audio, one for the
output and one for the incoming stream:

```
board: audio health: realtime=100% peak=21533 silent=0 zero_run=0 underruns=0
internet_radio: stream health: i2s_underruns=0 starvations=0 min_backlog=32768/65536 (1024ms) pcm=100%
```

A healthy picture is `realtime` at 99-100% with every other counter at zero. If
there is still no sound, the fault is past the handover to DMA - in the DAC's
wiring, not in the firmware. Separate `audio gap`, `audio silence` and
`audio zero-run` warnings pin the moment an event happened, and `slow repaint`
reports a display pass that took longer than 400 ms. `decode_stalls` counts the
times the decoder consumed input without producing audio: two seconds of that
and the stream is restarted, because otherwise the device sits silent behind a
stream that looks perfectly alive.

Once a minute it reports the resources that run out quietly:

```
health: reset reason: power-on
health: uptime=120s internal_free=52027/min 52027 largest=30720/min 30720 dma_largest=30720 psram_free=8053800
health: stack headroom, least seen: player_control=3780 ui=2524 usb_play=5184 usb_msc=2692 ...
```

The minima are the point, not the current values: a stack that has been
shrinking for months overflows the first time the compiler inlines a little
more, and internal memory runs out as "HTTPS will not connect" or "the file
will not play" rather than as anything about memory. The reason for the last
restart is the first line printed - after a reboot loop that is the difference
between someone pulling the power and a panic.

`largest` is the biggest contiguous block, and it cannot be read on its own: it
may well be sitting in RTC RAM, which a task stack cannot be allocated from. If
something fails with `ESP_ERR_NO_MEM` while `internal_free` looks ample, get
the breakdown by region - `heap_caps_print_heap_info()` says which piece of
memory actually ran out.

## Data on the device

The `littlefs` partition holds both the web assets and user data: `data/www/`
(gzipped at build time), `data/config/stations.csv`,
`data/config/settings.csv`, and `wifi.json` and `yandex.json`, which the device
creates itself and which are not in Git. Do not commit passwords, tokens or
keys.

## Limits

- **At most 256 entries per directory.** The rest are dropped with a warning;
  each entry costs about 264 bytes of PSRAM. The listing is one for both
  volumes - only one directory is on screen anyway, and a second copy would
  cost another 68 KB.
- **FAT32 or FAT16 only.** exFAT is not built in, and media of 64 GB and up
  usually ship formatted that way.
- **The card is not held mounted.** It is mounted when its source is entered
  (about 110 ms) and released when it is left: a mount costs some 2.4 KB of
  internal SRAM. The larger point is the missing card-detect line - a card
  inserted later is found only by the next attempt to enter the source, and
  that attempt is the detection.
- **About 1.7 s of silence between Yandex Music tracks.** A track link is valid
  for about a minute, so it is fetched at the last moment, and those requests
  run on the same task that decodes the audio.
- **The web interface assumes a trusted local network.** There is no
  authentication and `Origin` is not checked. Do not expose the device.
- Internal SRAM is the board's scarcest resource, and most of the large buffers
  live in PSRAM: the stream buffer, the directory listing, LVGL's heap. Each of
  those allocations keeps a fallback to internal memory so that a board without
  PSRAM still works - keep that fallback in new ones too.

`.vscode/tasks.json` carries build, flash and monitor tasks; they take `idf.py`
from the terminal's environment.
