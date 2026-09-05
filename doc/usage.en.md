# Using the device

[← README](../README.en.md) · [Русский](usage.md)

## First run

The device needs Wi-Fi. On its first start it brings up an access point of its
own: connect to it from a phone, open `http://192.168.4.1` and pick your
network. While the device is joined to no network at all, the settings page
shows the networks around it with their signal levels - pick one and type the
password. Up to five networks are remembered, and after that it connects on its
own.

Looking around is offered only in that mode. A scan takes the radio off its
channel for a few seconds, which with a network already up would break the
stream - so a connected device takes a new network by name, typed in behind the
"Добавить сеть" button. The list keeps what is no weaker than -80 dBm: below
that a network will associate and will not carry a stream, and a list padded
with them buries the two or three that are really there. A hidden network never
appears in it at all - the "Другая сеть…" row at the bottom is for those.

### Saved networks

Every remembered network on the settings page carries its own buttons:

- **Забыть** (forget) - erases the network from `wifi.json`. It asks first, and
  when it is the network this very page is reachable over the question says so:
  the device disconnects with it, and the page stops answering.
- **Отключиться** (disconnect, on the active one only) - the network stays
  saved, but the device will not return to it until the next reboot. It moves
  on to the next saved network, and to its own access point when there is none
  left to try. This is how a network is changed without losing its password.
- **Сделать первой** (make it first) - the order of the list is the priority:
  the device walks it from the top. It takes effect on the next connection and
  never interrupts a playing stream.

The device shows the address of its web interface at the bottom of the settings
screen - that is the only place it can be read. That band is the last stop the
cursor reaches on the screen: turn down to it and press the knob, and the
device puts the address up as a QR code. On a joined network that is a link to
the web interface; in setup mode it is an invitation to join the device's own
access point instead. There the band names only that network: `192.168.4.1` is
useless until the phone is on it, and once the phone is, the QR has already
opened it. Another press takes the code down, and so does 30 seconds of
nothing.

The page opens dark, whatever the phone or the computer is set to. The sun in
the header switches it to light; the choice is remembered by the browser and
holds until it is changed back.

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

With no network, internet radio and Yandex Music go dim: the rows stay where
they are and the cursor still lands on them, but a press answers "Нет сети -
см. Настройки" instead of starting anything. They are not taken off the screen,
or everything below them would shuffle up and back every time the Wi-Fi
dropped. The player page shows the same two sources the same way, and hides
their station list with them - there is nothing to play it with.

When the network goes away entirely while the radio or Yandex Music is
playing, the device returns to the home screen on its own and stops the source:
waiting on a screen for a stream that is not coming back serves no one.
"Entirely" means it has come to the setup access point; a short break the
device rides out by reconnecting does not throw anyone out of the player.

## Lists

Stations are numbered - `01`, `02` and on, in playlist order. The number stays
where it is even while a long name under the cursor travels as a marquee: it
belongs to the row, not to the name.

Files are not numbered. The first row of the browser is the way out of the
folder, and directories and playlists carry a mark each of their own; neither
is an nth of anything.

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

## Settings

Language, the look of the home screen, how long lines scroll, how the buffer
reading is shown, autoplay, Yandex Music, screen brightness, flipping the
picture vertically and horizontally, volume. They apply at once and are saved.

"Buffer" picks what the footer's left corner holds on the player screen: a
number ("Буфер 88%") or a strip. The strip is the same percentage, taken every
0.7 s and stood up as a bar; a new bar arrives at the left and the older ones
walk right, and about twenty seconds fit on it. The number answers how much is
held right now, the strip answers whether it has been holding - a dropout is
the number falling, and a fall is what a single figure cannot show, because by
the time anyone looks it is back.

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
words and the same order - except the volume, which has a knob on the device and
a slider in the player itself; a third place to set it only confused matters. It
works both ways: a change made in the browser
takes effect at once, as if it had been made on the knob, and a volume or
brightness turned on the device reaches an open page within a quarter of a
second. A slider being held with the pointer does not jump - the update is
dropped until it is let go.

## Formats in detail

FLAC plays at 24 bits too. The I2S slots are 16-bit and are not reconfigured on
the fly, so the sample is narrowed to 16 bits as it leaves the decoder; a frame
of such a stream is 24 KB rather than 16, and the output buffer grows to fit it
by itself.

Only files can be scrubbed: neither a radio stream nor a Yandex station has a
length or a position to move to. Track length is worked out from the file size
and the bitrate - exact for WAV and for constant bitrate, taken from the header
for FLAC, and drifting a little on a variable-bitrate file.

### Playlists on the media

Besides the audio the device reads playlist files on a drive and on a card -
`.m3u`, `.m3u8` and `.pls`. They have nothing to do with the station list:
they are ordinary files beside the music, written by whatever player put the
media together.

A playlist is not played but opened, the way a folder is. Its tracks are what
the list then shows, in the order the file writes them in, and from there
everything works as it does in a folder: the track keys and auto-advance move
along the playlist, and browsing up lands in the folder the file sits in. The
tracks themselves may be anywhere on the media, across as many folders as they
like.

Paths inside a playlist are read relative to the folder the file itself is in:
an `.m3u` in the root of a drive writes `Music/Album/1.mp3`, while a `.pls`
inside `Music` writes `Album/1.mp3`. Backslashes, as Windows writes them, are
understood as separators too. Lines the device cannot open - `http://` links,
drive letters, paths with `..`, formats with no decoder - are skipped, and how
many there were is in the log.
