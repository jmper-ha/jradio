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
| **Yandex Music** | works | "My wave" and your account's stations, album art, next track, like and dislike. Can be taken off the home screen |
| **Web interface** | works | Player with cover art, scrubbing, volume and track keys, the device settings, station-list editor, file browsing, Wi-Fi, linking Yandex Music |
| **Clock** | works | Time from the internet, on every screen |
| **Volume** | works | On the knob, remembered across restarts |
| **Autoplay** | works | Starts whatever was playing when the power went off |
| **Cover art** | works | From the file's tag, from a `cover.jpg` beside the music, from Yandex |
| **Interface language** | partly | The switch is there, but it only changes the labels on the settings screen itself |
| **Likes in Yandex Music** | works | "Like" and "dislike" - on F3 and in the web interface |
| **Yandex Music categories** | not built | Only the account's own stations are shown; there is no general catalogue of genres, moods and epochs |
| **Bluetooth, FM, DLNA** | not built | They have no menu entries: the device only shows what it can actually do |

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
screen - that is the only place it can be read. That band is the last stop the
cursor reaches on the screen: turn down to it and press the knob, and the
device puts the address up as a QR code. On a joined network that is a link to
the web interface; in setup mode it is an invitation to join the device's own
access point instead, because `192.168.4.1` is useless until the phone is on
that network. Another press takes the code down, and so does 30 seconds of
nothing.

On a portrait panel the band shows the address without the `http://`: seven
characters that tell the reader nothing, on a band 240 px wide that also has to
carry the QR hint. The code itself keeps the scheme - without it a camera opens
nothing.

## Controls

| Gesture | What it does |
|---|---|
| Turn the knob | Volume on the player screen, selection in lists |
| Press | Play and pause |
| Double press | Open the station or file list; the music keeps playing |
| Triple press | Scrub: the knob picks a position, a press applies it |
| Long press | Home screen; playback stops |
| F2 | Back |
| F3 | Previous track or station |
| F4 | Next track or station |

A single press lands after a short delay - before that it cannot be told from
the beginning of a double press.

While scrubbing, the knob and the press are busy choosing a position, so the
volume does not change there. Any other button leaves the mode without changing
anything. The music plays on throughout.

Scrubbing in the web interface is simpler: the position bar under the track
name is an ordinary slider - drag it, or move it with the arrow keys. It does
not twitch under the pointer while the position is being polled; let go and the
device jumps and answers.

## Home screen

Two looks, chosen in the settings: a list of entries or a carousel of large
icons. Every source - radio, drive, card, Yandex Music, settings - has an icon
of its own.

Choosing the drive or the card when neither is there opens an explanation
rather than an empty list: insert the medium, it cannot be read, or it holds no
music - these are different things, and the advice differs.

## Lists

Stations are numbered - `01`, `02` and on, in playlist order. The number stays
where it is even while a long name under the cursor travels as a marquee: it
belongs to the row, not to the name.

Files are not numbered. The first row of the browser is the way out of the
folder and directories carry a mark; neither is an nth of anything.

The cursor stays on the middle row and the list moves under it. The bar below
the list says where in it you are.

## Player screen

The clock and the Wi-Fi level are at the top. Below them the cover, the station
name, and the track and performer in large type. At the bottom, the volume
scale, the position bar and a level meter.

The codec, the bitrate and the sample rate are shown beside them: as a line
under the performer on the wide panel, and as a column next to the cover on the
tall one, where that line would not fit.

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

**What you can do.** Play a station, pause it, skip to the next track - with F4
on the device or with the button in the web interface. There is no
previous-track button: a station only moves forward, and there is no going back
to a track already played, which is why F3 does something else here - it carries
both marks on the playing track, "like" and "dislike".

**The like mark.** The same one the app has: the track joins your liked tracks
or leaves them. On the player screen a heart beside the volume shows it -
outline while the track is not marked, solid once it is. What you liked earlier
comes from Yandex with the track itself, so the heart is solid for songs marked
from the phone too. It fills in only after the service has answered, never on
the press alone: otherwise the screen would show something the account does not
have. The same heart is in the web interface.

**The dislike.** The same F3, pressed twice: the track joins your rejected ones
and the station stops offering it. The heart is struck through. Any further
press of that key takes it back - single or double, because while a track is
rejected the key can do only one thing and that is undo it. The single press now
lands a third of a second late, which is how long the key waits for a possible
second one; on a mark nothing is waiting for, it is not felt.

In the web interface the dislike is a button of its own beside the heart: there
is room for both, and no reason to make one press mean two things. The two marks
exclude each other - setting one clears the other - and that is how Yandex
itself behaves, not only this end.

Yandex does not hand a rejected track back, so there is nothing to tell it about
the mark afterwards: on the device it is visible only while the track you
rejected is still playing. Nothing is lost by that - next time the song simply
is not there.

**The device tells the station what you listened to.** What you put on, what
played to the end, what you skipped - the same events the phone app sends.
Without them the rotor never learns that a song has already been heard, and a
station opened tomorrow starts where it started today; so this is not
decoration, it is what stopped tracks from repeating. The reports go out on
their own task a few seconds late, deliberately clear of the track change, so
they do not lengthen the gap between songs.

**If you do not want it.** Settings > General has a "Yandex Music" switch.
Turned off, the source disappears from the home screen and the device stops
mentioning it; the account stays linked, and it can be turned back on at any
time.

To leave it out of the firmware altogether, use the `YANDEX_MUSIC` line in
`board_options.h`: `FEATURE_OFF` - or the line deleted - means there is neither
the source nor the switch in Settings. See "What the home screen shows".

An account can be unlinked from the web interface.

## Settings

Language, the look of the home screen, how long lines scroll, autoplay, Yandex
Music, screen brightness, flipping the picture vertically and horizontally,
volume. They apply at once and are saved.

Scrolling is about lines that do not fit: the track name on the player screen,
and the list row the cursor is on. "Left-right" runs the line out to its end
and back again; "Left" runs it out, holds for a second, and shows it whole from
the start. Either way the pause before the next pass is 3 seconds.

Brightness is a number from 10 to 90 rather than a switch: click the row, the
number is taken into angle brackets, and turning the knob then changes it. The
panel follows on every detent. Clicking again releases the knob, and it moves
through the list as before.

The language switch still only changes the labels on the settings screen
itself.

The same settings are in the web interface, on its Settings page, in the same
words and the same order. It works both ways: a change made in the browser
takes effect at once, as if it had been made on the knob, and a volume or
brightness turned on the device reaches an open page within a quarter of a
second. A slider being held with the pointer does not jump - the update is
dropped until it is let go.

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
#define DISPLAY   DISPLAY_ILI9341_320_240
#define AUDIO_DAC DAC_PCM5102
```

The parts with drivers are listed in
[`board_parts.h`](components/board/include/board_parts.h). A typo fails the
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
for each orientation and both are compiled in, so nothing needs regenerating.

The panel does not have to be an ILI9341. Each controller has a profile of its
own in
[`components/board/include/display/`](components/board/include/display/) and a
driver file in `components/board/display/`; `board.c` does not know which one
was selected - it calls `board_panel_create()` and works through the common
esp_lcd interface from there. Besides the ILI9341 the catalogue carries the
ST7789 320x240, in two orientations as well; its driver ships inside ESP-IDF,
so it needs no dependency of its own. The landscape profile was taken off a
panel: RGB colour order, inversion off - against the usual advice for an
ST7789, which with INVON renders the whole screen as a negative - and one
horizontal mirror. Portrait has not been built yet and
[`st7789.h`](components/board/include/display/st7789.h) marks it as derived
rather than measured. The driver that was not selected costs no flash at all.

The same file names the parts this board does not carry: an FM tuner and
Bluetooth. Their blocks are commented out rather than deleted - the file should
answer "can this firmware drive one" with a no as well as with a yes. For
Bluetooth the answer is final: the ESP32-S3 has no classic Bluetooth, only BLE,
and A2DP is a classic-Bluetooth profile, so playing from a phone would need a
receiver module of its own.

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
  "Limits". FAT16/FAT32 only, no exFAT.

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
partition. Compare the live state first: `curl http://<ip>/api/playlist`,
`curl http://<ip>/api/settings` and `curl http://<ip>/api/yandex`. The settings
return to their defaults after the flash, and the account has to be linked
again.

## Tests

```bash
bash tests/run_host_tests.sh
```

70 suites, no ESP-IDF activation needed. They compile the real component
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
| `GET /api/settings` | The device settings, the same ones its own screen has |
| `POST /api/settings` | Changes one setting: `{"field":…,"value":…}` |
| `GET /api/progress` | Track position, buffer fill, cover generation |
| `GET /api/cover` | The current cover, 96x96, as a BMP |
| `GET /api/yandex` | Link state and the account's stations (never the token) |
| `POST /api/yandex` | Link, cancel, unlink, refresh the stations |
| `POST /api/wifi` | Saves a network |
| `/ws` | Commands and live updates |

WebSocket commands: `player.play`, `player.pause`, `player.toggle`,
`player.next`, `player.previous_item`, `player.next_item`, `player.seek`,
`player.like`, `player.dislike`, `source.select`, `list.select`, `browse.up`,
`wifi.save`. Live
state arrives as diffs - `player`, `list`, `wifi`, `settings`; anything large -
the playlist, media directories - goes over REST, because it does not fit in a
frame and must not spend internal SRAM.

`player.seek` carries a second rather than a percentage: seconds are what the
device turns into a byte offset, and a percentage would have to be turned back
into seconds using a length the browser only has an estimate of.

The track position and the cover go over REST too, for a different reason: the
snapshot is diffed and broadcast on every change, and a counter that ticks
would push a frame a second to every open browser. The page polls
`/api/progress` once a second while something is playing and rarely otherwise.
The cover is served as a BMP - the device holds it already decoded to RGB565
and no longer has the original bytes, so anything compressed would mean a PNG
encoder in firmware for a picture that travels over a LAN; 27 KB uncompressed
is the cheaper answer. The cover's generation number rides in the URL, so the
browser takes it from its cache until it actually changes.

The device settings are written by both the panel and the web, through the same
setters, serialised at the file level. A write from the browser raises a flag,
and the UI task re-reads `settings.csv` and re-applies the brightness, the
panel rotation, the volume and the Yandex row - a stored value does none of
that by itself.

They come back as a `settings` section on the socket rather than by polling:
the knob changes the volume without telling anyone, and `settings.csv` is
eleven consecutive reads - far too much to ask on a timer. The UI task
publishes a copy in memory, the broadcaster reads it every 250 ms and sends a
diff only when something has moved. That section grew the complete snapshot by
270 bytes and pushed it past 4096, so the frame limit is now 4608;
`test_web_server.c` builds the worst case - 32 stations named in nothing but
quotes and backslashes - and asserts the margin is still there.

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

- **At most 99 stations in the playlist.** Lines beyond that are dropped as the
  file is parsed, and the reply to an upload says how many stations were
  actually read - the file itself is stored whole, so the number of lines in it
  and the number of stations in the device need not agree. A station name is
  limited to 96 bytes and its URL to 256; a longer line is dropped entirely.
  The catalogue is 35 KB and lives in PSRAM.
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
  live in PSRAM: the stream buffer, the directory listing, the station
  catalogue, LVGL's heap. Each of
  those allocations keeps a fallback to internal memory so that a board without
  PSRAM still works - keep that fallback in new ones too.

`.vscode/tasks.json` carries build, flash and monitor tasks; they take `idf.py`
from the terminal's environment.
