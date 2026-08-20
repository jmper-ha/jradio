# jradio

Firmware for a desktop audio player built on the ESP32-S3 (ESP-IDF 5.5.x):
internet radio and files from a USB drive. Driven by a rotary encoder and four
buttons on the device, or from a browser on the local network.

*[Русская версия](README.md)*

## What it does

| Mode | State |
|---|---|
| Internet radio | MP3, AAC, FLAC, Ogg FLAC, HLS (`.m3u8`); HTTP and HTTPS; ICY metadata; playlist; reconnection |
| USB player | Directory browser; MP3, AAC, FLAC, Ogg FLAC, WAV; tags and cover art; advances to the next track; track position |
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
currently relabels **the settings screen only**.

## Player screen

A status strip along the top: clock centred, Wi-Fi strength on the right as
four bars with the figure in dBm. Until SNTP has answered the clock reads
`--:--` — the device has no real-time clock of its own.

Below it, the cover art, the station name, a large track line and the
performer: an ICY title is split at the first dash with spaces on both sides,
and left whole whenever that is ambiguous.

Playing from the drive, those same three lines come out of the file's tags -
the album where the station name goes, then the track title and the performer -
and each falls back on its own to what is known without any tags: the directory
instead of the album, the file name instead of the title. ID3v2.2 to 2.4 are
read (with ID3v1 as a last resort) along with Vorbis comments in FLAC; text is
converted to UTF-8 from Latin-1, UTF-16 and UTF-8, and Windows-1251 stored
under a Latin-1 label is recognised from the byte range it uses - without that,
half the Russian files on a drive would read as "Áàëòèéñêîå ìîðå".

The cover comes from an `APIC` frame or a `PICTURE` block - baseline JPEG only -
and is reduced to 96x96 by averaging rather than sampling: at that size a cover
is mostly lettering, and sampling it reads as noise. A picture that is not
square is fitted inside the square rather than stretched to it. One cover
shared by a whole album is decoded once, so moving to the next track does not
blink the tile. Where there is no cover, or it is not a JPEG, or it fails to
decode, the tile keeps its placeholder symbol.

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
| Long press (0.8 s) | Home screen, playback stops |

A single press lands after 350 ms - before that it cannot be told from the
first half of a double press. The delay applies on the player screen only.

## Hardware

ESP32-S3 in a QFN56 package, 16 MB flash, 8 MB PSRAM; an ILI9341 320x240
display over SPI; a rotary encoder with a push button and four buttons; a
PCM5102 DAC over I2S with a line output; a USB host port for a FAT-formatted
drive.

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
| USB D+ | 20 | | | |

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
  re-enumerated by a logical power cycle of the root port.

## Building and flashing

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash              # application only, leaves LittleFS alone
idf.py -p PORT littlefs-flash     # overwrites the whole data partition
idf.py -p PORT monitor
```

`littlefs-flash` **destroys user data** - playlist edits and saved networks.
Compare the live state first: `curl http://<ip>/api/playlist`.

## Tests

```bash
bash tests/run_host_tests.sh
```

44 suites, no ESP-IDF activation needed. They compile the real component
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
| `GET /api/usb` | Contents of the drive's current directory |
| `POST /api/wifi` | Saves a network |
| `/ws` | Commands and live updates |

WebSocket commands: `player.play`, `player.pause`, `player.toggle`,
`source.select`, `list.select`, `browse.up`, `wifi.save`. Live state arrives as
diffs; anything large - the playlist, USB directories - goes over REST, because
it does not fit in a frame and must not spend internal SRAM.

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
health: uptime=120s internal_free=23027/min 22999 largest=9728/min 9728 dma_largest=9728 psram_free=8186180
health: stack headroom, least seen: player_control=3732 ui=2552 usb_play=5892 usb_msc=2724 ...
```

The minima are the point, not the current values: a stack that has been
shrinking for months overflows the first time the compiler inlines a little
more, and internal memory runs out as "HTTPS will not connect" rather than as
anything about memory. The reason for the last restart is the first line
printed - after a reboot loop that is the difference between someone pulling
the power and a panic.

## Data on the device

The `littlefs` partition holds both the web assets and user data: `data/www/`
(gzipped at build time), `data/config/stations.csv`,
`data/config/settings.csv`, and `wifi.json`, which the device creates itself
and which is not in Git. Do not commit passwords or keys.

## Limits

- **At most 256 entries per directory on the drive.** The rest are dropped with
  a warning; each entry costs about 264 bytes of PSRAM.
- **FAT32 or FAT16 only.** exFAT is not built in, and drives of 64 GB and up
  usually ship formatted that way.
- **The web interface assumes a trusted local network.** There is no
  authentication and `Origin` is not checked. Do not expose the device.
- Free internal SRAM is scarce. Large buffers are allocated in PSRAM with a
  fallback to internal memory - keep that fallback when adding new ones.

`.vscode/tasks.json` carries build, flash and monitor tasks; they take `idf.py`
from the terminal's environment.
