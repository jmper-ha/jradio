# jradio

Firmware for a desktop audio player built on the ESP32-S3 (ESP-IDF 5.5.x):
internet radio and files from a USB drive or an SD card. Driven by a rotary encoder and four
buttons on the device, or from a browser on the local network.

*[Русская версия](README.md)*

## What it does

| Mode | State |
|---|---|
| Internet radio | MP3, AAC, FLAC, Ogg FLAC, HLS (`.m3u8`); HTTP and HTTPS; ICY metadata; playlist; reconnection |
| File player | USB drive and SD card; directory browser; MP3, AAC, FLAC, Ogg FLAC, WAV; tags and cover art; advances to the next track; track position and scrubbing |
| Volume | Digital, on the encoder, saved across restarts |
| Clock | SNTP, on every screen |
| Wi-Fi | Set up through a temporary access point, up to five saved networks |
| Web interface | Player, drive browser, settings, playlist editor; live state over WebSocket |
| Screens | Home (list or carousel), player, station and file lists, settings |
| Autoplay | Restores the station or file that was playing at power-off |
| DLNA, FM, Bluetooth, RTC, OTA | Not implemented |

Settings (`Language`, `General`, `Display`) apply at once and are saved to
`/littlefs/config/settings.csv`: language, home screen style, autoplay,
independent vertical and horizontal display flips, volume. The language switch
currently relabels **the settings screen only**. A band along the bottom of the
settings screen carries the web interface's address: it is the only place the
device says where to reach itself, and without it the address was known only to
whoever was watching the UART log. In Wi-Fi setup mode it reads
`http://192.168.4.1`, the device's own access point.

## Home screen

Two styles, chosen in the settings: a list of entries and a carousel of large
icons. Every source - internet radio, the drive, an SD card, Bluetooth, FM,
DLNA, Yandex Music, settings - has an icon of its own; the list puts it in
front of the label, the carousel gives it a tile. The icons are A8 bitmaps and
are recoloured to suit their state, so one file serves both the selected entry
and its neighbours.

Sources that are not implemented stay on the screen and answer with a line
saying so: what is intended stays visible, and nobody has to guess why a press
did nothing. Choosing the drive or the card when there is none opens not an
empty browser but a screen with a picture of the right medium and the reason
spelled out - insert one, it cannot be read (FAT32 is needed), or it holds no
files. Those are different things, and telling somebody to insert something
they have already inserted is worse than saying nothing. Both the picture and
the wording follow the source: a memory card is neither called nor drawn as a
flash drive, or the reader goes to the wrong socket.

## Player screen

A status strip along the top: clock centred, Wi-Fi strength on the right as
four bars with the figure in dBm. Until SNTP has answered the clock reads
`--:--` — the device has no real-time clock of its own.

Below it, the cover art, the station name, a large track line and the
performer: an ICY title is split at the first dash with spaces on both sides,
and left whole whenever that is ambiguous.

Playing from either medium, those same three lines come out of the file's tags -
the album where the station name goes, then the track title and the performer -
and each falls back on its own to what is known without any tags: the directory
instead of the album, the file name instead of the title. ID3v2.2 to 2.4 are
read (with ID3v1 as a last resort) along with Vorbis comments in FLAC; text is
converted to UTF-8 from Latin-1, UTF-16 and UTF-8, and Windows-1251 stored
under a Latin-1 label is recognised from the byte range it uses - without that,
half the Russian files on a drive would read as "Áàëòèéñêîå ìîðå".

The cover comes from an `APIC` frame or a `PICTURE` block. Where the file
carries none, `cover.jpg`, `cover.jpeg` or `cover.png` beside the track is used
instead, in any case. Other names (`folder.jpg`, `front.jpg`) are not guessed
at: they are the conventions of particular players, and accepting them means
opening some unrelated file on every track of every album that has no cover. A
file's own picture always wins over the folder's - it belongs to the track,
while the folder's belongs to everything filed with it.

Baseline JPEG is decoded by tjpgd, which needs four kilobytes of working
memory. Progressive JPEG and PNG it cannot read - and both turn up in
ordinary files, progressive being what several taggers write by default - so
those go to stb_image, which holds the whole picture
at once: about 13 bytes per pixel for JPEG and 6 for PNG. Two things follow.
The decode runs **on a task of its own with its stack in PSRAM** (its inflate
wants six kilobytes of stack, which no player task has to spare, and task
stacks come out of internal RAM where some 18 KB is free); and the size limit
is measured against free PSRAM rather than fixed, because a folder cover is a
full-size scan rather than the thumbnail a tag carries.

The picture is then reduced to 96x96 by averaging rather than sampling: at that
size a cover is mostly lettering, and sampling it reads as noise. One that is
not square is fitted inside the square rather than stretched to it. A cover
shared by a whole album is decoded once - a folder cover is read off the
medium once as well - so moving to the next track does not blink the tile. Where there
is no cover anywhere, or it fails to decode, the tile keeps its placeholder
symbol.

Every format has an elapsed time and a position bar, but they are arrived at
differently. An MP3's length comes from its bitrate and the size of the file;
FLAC states no bitrate - its frames are as large as the music in them needs -
so the length is read exactly out of STREAMINFO, where the encoder wrote the
sample count. A jump through a FLAC is additionally aligned to a frame boundary
and hands the decoder a stream header: it cannot start in the middle of a frame
and answers an attempt to with an error.

Along the bottom, the decoder's input buffer fill (for a file, the elapsed time
and a position bar) and the volume, with a per-channel level meter between
them: twenty blocks, the top fifth red. The scale is logarithmic, 20 dB from
-5 to -25 dBFS, and measures not peak but the loudest RMS over 5.8 ms windows.
**The level is taken before the volume stage** - otherwise turning the volume
down would look exactly like a failing stream. Pause shows as a badge in the
middle of the screen.

Every screen shares one palette and one status strip.

## Controls

| Gesture | Action |
|---|---|
| Turn the encoder | Volume on the player screen, movement through lists |
| Short press | Play / pause |
| Double press | Station or file list, playback continues |
| Triple press | Scrub the track: the knob moves the bar, a press applies it |
| Long press (0.8 s) | Home screen, playback stops |
| F2 | Back: to the home screen from the player, and out of the lists |
| F3 | Play / pause on the player screen |

A single press lands after 350 ms - before that it cannot be told from the
first half of a double press. The delay applies on the player screen only.

Scrubbing owns both the knob and the press while it is open, so the volume and
the play/pause click cannot be triggered by accident by the gesture that
chooses a position. Any other button leaves the mode without changing anything:
a mode with only one way out is too easy to be stuck in. The music plays on
throughout - only the readout follows the knob.

## Hardware

ESP32-S3 in a QFN56 package, 16 MB flash, 8 MB PSRAM; an ILI9341 320x240
display over SPI; a rotary encoder with a push button and four buttons; a
PCM5102 DAC over I2S with a line output; a USB host port for a FAT-formatted
drive; a microSD slot over SPI.

The wiring lives in [`board_options.h`](board_options.h), which is the source
of truth; the table below is for convenience. Options are grouped by device,
each part is chosen on one line, and everything that follows from that choice
sits in profiles beside the driver:

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

58 suites, no ESP-IDF activation needed. They compile the real component
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
| `POST /api/wifi` | Saves a network |
| `/ws` | Commands and live updates |

WebSocket commands: `player.play`, `player.pause`, `player.toggle`,
`source.select`, `list.select`, `browse.up`, `wifi.save`. Live state arrives as
diffs; anything large - the playlist, media directories - goes over REST,
because it does not fit in a frame and must not spend internal SRAM.

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
`data/config/settings.csv`, and `wifi.json`, which the device creates itself
and which is not in Git. Do not commit passwords or keys.

## Limits

- **At most 256 entries per directory.** The rest are dropped with a warning;
  each entry costs about 264 bytes of PSRAM. The listing is one for both
  volumes - only one directory is on screen anyway, and a second copy would
  cost another 68 KB.
- **FAT32 or FAT16 only.** exFAT is not built in, and media of 64 GB and up
  usually ship formatted that way.
- **The card is not held mounted.** It is mounted when its source is entered
  (about 110 ms) and released when it is left: a mount costs some 2.4 KB of
  internal SRAM, and there is no reason to hold one that is not being used. The
  larger point is the missing card-detect line - a card inserted later is found
  only by the next attempt to enter the source, and that attempt is the
  detection.
- **The web interface assumes a trusted local network.** There is no
  authentication and `Origin` is not checked. Do not expose the device.
- Internal SRAM is the board's scarcest resource, and most of the large buffers
  live in PSRAM: the stream buffer, the directory listing, LVGL's heap. Each of
  those allocations keeps a fallback to internal memory so that a board without
  PSRAM still works - keep that fallback in new ones too.

`.vscode/tasks.json` carries build, flash and monitor tasks; they take `idf.py`
from the terminal's environment.
