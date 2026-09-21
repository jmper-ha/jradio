# How to use it

[← README](../README.en.md) · [Русский](usage.md)

## First boot

The device needs Wi-Fi. While it knows no network, it opens its own access
point named `jradio-XXXX` (XXXX comes from the board's serial number):

1. Join it from a phone or a computer.
2. Open `http://192.168.4.1`.
3. Pick your network from the list (networks no weaker than −80 dBm are
   shown; a hidden one is added through "Other network…") and enter the
   password.

The device connects and keeps doing so on its own. Up to five networks are
remembered and tried in list order. On a connected device a new network is
added by hand, with "Add network" - no scan runs then, so the stream is not
interrupted.

Every saved network on the settings page has its buttons: **Forget**,
**Disconnect** (the network stays saved, but the device will not return to it
until a reboot - that is how you move to another one without losing the
password) and **Make first**.

**The web interface's address** is shown at the bottom of the device's
settings screen. Scroll down to that bar and press the encoder for a QR code:
in normal mode a link to the web interface, in setup mode an invitation to
join the access point. Press again or wait 30 seconds to dismiss it.

The web page opens dark; the sun in the header switches it to light, and the
choice is remembered in the browser.

## Controls

### The encoder and the buttons

| Action | What it does |
|---|---|
| Turn the encoder | Volume on the player screen, selection in lists |
| Press | Pause / resume |
| Double press | Open the station or file list (the music keeps playing) |
| Triple press | Scrub: turn to pick a position, press to apply |
| Long press | Home screen (or back); playback stops |
| Hold Sleep | Deep sleep |
| Quick_menu | The quick panel - a window over the screen |
| Prev / Next | Previous / next track or station |

A single press acts with a small delay - otherwise it could not be told from
the start of a double one. A short press of Sleep does nothing yet.

### The remote control

With an IR receiver on the board, the device takes any infrared remote - from
a TV, a set-top box or a parts kit. Keys are learned on the Remote page of the
web interface (the "Set up the remote" button in the settings): press Learn
next to a function, then the key on the remote. The row lights up, the code is
stored. One key - one function.

While the page is open it shows what was pressed: the row of a learned
function lights while the key is held, and a key nobody taught is named by
its code.

| Function | What it does |
|---|---|
| Volume up / down | Volume; ramps while held |
| Mute | Silence and back to the previous volume |
| Play / pause, OK | As a press of the encoder |
| Previous / Next | As Prev / Next |
| Up / Down | As turning the encoder in a list |
| Back, Menu | As a long press of the encoder |
| Quick panel | As Quick_menu |
| Station list | Open the station or file list |
| Sleep timer | The next step of the timer, round-robin |
| Like / Dislike | Rate the Yandex Music track |
| 0-9 | An internet-radio station number |
| Radio, USB, SD, Bluetooth, Yandex, Media server | Switch the source |
| Sleep / wake | As holding Sleep - sleep; and wake |

**Digits** dial a station by its list number: two digits in a row select at
once; one digit selects after a second and a half, or at once when no
two-digit number starts with it. Works only while internet radio is playing.

**Waking.** The key learned as "Sleep / wake" wakes the device from deep
sleep on the first press. Other remotes in the room wake the chip too - that
cannot be avoided - but such a boot leaves the screen dark, checks the code
and goes back to sleep. For this the receiver must be on a GPIO 0-21 pin and
be fed from permanent 3.3 V (see [Hardware](hardware.en.md#the-ir-receiver-the-remote)).

The remote's table is stored on the device and survives a firmware update,
but not a rewrite of the data partition.

## Screens

### The home screen

A list of sources or a carousel of large icons - your choice in the settings:
radio, the stick, the card, Yandex Music, the media server, Bluetooth,
settings. Only what is on the board and enabled is shown.

No stick or card inserted - instead of an empty list, a screen with a hint:
insert the drive, it cannot be read, or there is no music on it. Without a
network the radio and Yandex Music stay in the list, but a press answers "No
network - see Settings". If the network is lost for good while playing, the
device returns to the home screen by itself.

### Lists

Stations are numbered in playlist order - the number the remote's digits dial
and the alarm refers to. Files are not numbered; the first row in a folder
goes up, folders and playlists carry icons. The cursor stays in the middle
and the list moves under it; the bar below shows where you are.

### The player screen

The clock and the Wi-Fi level on top, then the cover, the station or album
name, the track and the performer in large type, and at the bottom the volume
scale, the position in the track and a level meter. Beside them the codec, the
bitrate and the sample rate.

The cover comes from the file's tag, from `cover.jpg` / `cover.png` next to
the music, or from the service. Titles are read from tags, including Russian
ones in legacy encodings; what the tags lack is replaced by the folder and the
file name.

**Seeking** is a triple press of the encoder: turn to pick a position, press
to apply, any other button leaves without a change. In the web interface it is
an ordinary slider under the track title.

## Sources

### Internet radio

The station list is edited in the web interface (the Playlist page): name,
address, picture, order by dragging. Up to 99 stations, HTTP and HTTPS, the
track title comes from the stream. After a drop the device reconnects itself.

### The stick and the card

Folders, playlists and tracks. `.m3u`, `.m3u8` and `.pls` files on the drive
open as folders: inside are the tracks in file order, auto-advance and the
buttons follow the playlist, "up" returns to the folder the file is in. Paths
in a playlist are taken relative to its own folder, backslashes are understood;
lines that cannot be opened (links, drive letters, formats without a decoder)
are skipped.

Drives are FAT16/FAT32, up to 256 entries per folder. The card is found on
entering the source.

### Bluetooth

With the [jradio-bt](hardware.en.md#bluetooth-the-jradio-bt-module) module the
menu has a Bluetooth source. Choosing it opens the player screen, and the
device is visible in the phone's Bluetooth under its name (`jradio-XXXX` or
the one set in the settings) for about two minutes; a known phone connects by
itself. Then it is like the radio: the phone's name in place of the station,
the track and the cover from the phone, Prev/Next walk its queue, the encoder is
the volume both ways.

**Sound to a Bluetooth speaker.** Everything the device plays can go to a
speaker or headphones: web settings, "Sound over Bluetooth" → "Find speakers"
→ tap the one found. The device calls it three times (at once, after 10 and
after 20 seconds), then waits for the speaker to connect by itself - a paired
speaker does that when switched on. Every speaker that ever received sound
stays in the list (up to five) and is switched to with one tap; Forget removes
it. The speaker's buttons work: pause, next and previous track, volume.

### Yandex Music and the media server

They have pages of their own: [Yandex Music](yandex.en.md) and
[Media server (DLNA)](dlna.en.md).

## The quick panel

Quick_menu drops a window over the player with what is needed right now: the
**sleep timer**, the **alarm**, the **brightness** and the **BT speaker**.
Without leaving the screen:

| Action | What it does |
|---|---|
| Turn the encoder | The next row |
| Press | Take the value - it turns amber |
| Turn in this mode | Change the value |
| Press again | Release the value |
| Quick_menu, a long press of the encoder | Close the window |

The window closes by itself after ten seconds without a press. The "Sound
over Bluetooth" row is there only while the module answers; the sleep timer
cycles - off, 15, 30, 45, 60, 90, 120 minutes. A change made in the browser
shows in the window at once.

## Sleep, the timer, the alarm

### Deep sleep

Hold Sleep for a second and the device goes to sleep: the screen goes dark,
playback stops, pending settings are written, Bluetooth and Wi-Fi leave
cleanly. With a peripheral power switch fitted, the whole periphery is
powered down too.

The same Sleep, or a learned key on the remote, wakes it. Waking is an ordinary
boot; what plays is decided by the Resume setting. For Bluetooth, resume
brings back the waiting-for-the-phone screen - the sound comes from the phone,
there is nothing to resume.

### The sleep timer

"Play for so long, then sleep": 15, 30, 45, 60, 90 or 120 minutes. Set in the
web settings (the Time section), in the quick panel or with a remote key. The
countdown shows on the player page and as a crescent next to the clock.

When the time is up, the volume fades to zero over ten seconds, and only then
the device goes to sleep. Any press in those ten seconds cancels the sleep and
brings the volume back. The timer does not survive a reboot.

### The alarm clock

Play a station at a set time: the switch, the time, the weekdays, the station
(by its list number) and the volume - in the web settings, the Alarm section.
The alarm's volume applies to the ring only and does not overwrite yours. At
least one day is always selected.

At the appointed minute the device raises the backlight, leaves the
screensaver and starts the station. There is no auto-off - it plays until you
stop it.

**A sleeping device wakes itself.** First ten minutes before the ring,
quietly, with the screen dark: only to check the clock against the time server
(over a night the internal clock drifts by minutes). Then a minute before the
ring, for real, with the screen still dark, so that the sound starts exactly
on its minute. The backlight comes on with the ring.

On the screen an armed alarm shows as a bell next to the clock; on the player
page as a line with the time and the days.

## Settings

On the device: language, home screen style, scrolling of long lines, the
buffer readout, resume, Yandex Music, DLNA, weather, brightness, picture
mirroring, screensaver, sound over Bluetooth, volume. They apply at once and
are saved. The same settings are in the web interface, in the same words;
changes either way show at once.

Only in the web interface: the device name, time and time zone, the weather in
detail, the alarm, the sleep timer, the backup.

- **Device name** - how it is called in Bluetooth and as the setup access
  point. An empty field is the built-in `jradio-XXXX`.
- **Yandex Music**, **DLNA** - the switches remove the source everywhere; a
  playback already running is not stopped.
- **Buffer** - the left corner of the player's footer: a number ("Buffer 88%")
  or a strip chart of the last twenty seconds.
- **Scrolling** - how long lines move: "left-right", or "left" with a jump
  back to the start.
- **Brightness** - 10 to 100, changed with the encoder right on the row.
- **Screensaver** - what the screen does when untouched: none, dimming (to
  the "idle brightness"), a black screen, or the **clock** - the time drifting
  over a black screen with the date, the weather and the track title. The
  delay (15 s - 10 min) and the idle brightness are set on the web. Any action
  wakes the screen; in the dark modes the first press only wakes.
- **Weather** - a switch on the device; the service, the coordinates and the
  key are on the web.
- **Language** - Russian or English, one switch for the screen and the
  browser, no reboot.

### Time

The time zone from a list and the time server's address (`pool.ntp.org` by
default). There is no battery-backed clock: after power-on the time arrives
from the network a couple of seconds after Wi-Fi connects.

### Weather

The temperature and a sky icon next to the clock on every screen and on the
screensaver. The source is your choice: **Open-Meteo** (no key), **wttr.in**
(no key) or **OpenWeatherMap** (a key is needed). The coordinates are typed
in - latitude and longitude in degrees, as a map writes them; Moscow by
default. The icon tells day from night. Polled every fifteen minutes; under
the source choice the page shows what the panel shows, or the reason nothing
is there.

### About

The last item in the settings: the firmware version, its build date, the web
interface's version and ESP-IDF's. There are two versions because the firmware
and the web interface live in different partitions and are updated by
different commands; when they differ the screen says so - time to update the
data partition (see [Building](build.en.md)).

### Backup

Web interface only. "Download archive" gives a zip with the Wi-Fi networks,
the settings, the Yandex token and the weather key - everything the device
knows about itself. The station list is exported separately, on the playlist
page. Restore takes the whole archive or one file from it and reboots the
device.

It is for rewriting the data partition: download → flash → restore. **The
Wi-Fi password is in the archive in clear text** - keep the file as you would
a password.

## Formats: the details

- FLAC plays in 24 bits too (truncated to 16 on the output).
- Seeking exists for files only: radio and Yandex stations have neither a
  length nor a position. A track's length is computed from size and bitrate -
  it drifts a little on variable bitrate.
- `.m4a` (AAC in an MP4 container) is not read - raw ADTS only.
- HLS: MP3 or AAC segments; MPEG-TS is not parsed.
- WAV - from drives only, 16-bit; it does not play from a media server.
