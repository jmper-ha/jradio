# Web interface

[← README](../README.en.md) · [Русский](web.md)

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Wi-Fi and player state in one snapshot |
| `GET /api/playlist` | Station list as CSV |
| `POST /api/playlist` | Replaces the station list wholesale |
| `GET /api/files` | Contents of the current directory on the active medium |
| `GET /api/settings` | The device settings, the same ones its own screen has |
| `POST /api/settings` | Changes one setting: `{"field":…,"value":…}` |
| `GET /api/progress` | Track position, buffer fill, cover signature |
| `GET /api/cover` | The current cover, 96x96, as a BMP |
| `GET /api/stations` | The station names of the active source |
| `POST /api/station-test` | Plays an address on the device without touching the playlist |
| `GET /api/station-icon` | An uploaded station picture, by file name |
| `POST /api/station-icon` | Uploads a station picture; the device names the file |
| `GET /api/yandex` | Link state and the account's stations (never the token) |
| `POST /api/yandex` | Link, cancel, unlink, refresh the stations |
| `POST /api/wifi` | Saves a network |
| `POST /api/wifi-scan` | Starts a scan for nearby networks (only with no connection) |
| `GET /api/wifi-scan` | What it found: `scanning`, `done` with the list, or `idle` |
| `/ws` | Commands and live updates |

WebSocket commands: `player.play`, `player.pause`, `player.toggle`,
`player.next`, `player.previous_item`, `player.next_item`, `player.seek`,
`player.like`, `player.dislike`, `source.select`, `list.select`, `browse.up`,
`wifi.save`, `wifi.forget`, `wifi.prioritize`, `wifi.disconnect`. Live
state arrives as diffs - `player`, `list`, `wifi`, `settings`; anything large -
the playlist, media directories - goes over REST, because it does not fit in a
frame and must not spend internal SRAM.

The player's three lines - what is being listened to, the performer, the track -
come to the page and to the screen from one function
([`ui_now_playing.h`](../components/ui/include/ui_now_playing.h)) rather than being
worked out twice. Worked out twice, they drifted: on files the page showed the
file's name where the screen read its tags, and it named a station by whatever
the stream called itself while the screen obeyed the playlist's third column -
"Радио Шоколад" marked `L` is named from the list on the screen, and was named
`DB91-TX` on the page, which is what the stream calls itself. Each also had its
own way of splitting the ICY line into a performer and a track: the page knew
about the en dash and repaired broken UTF-8, the screen did neither.

The tags themselves still stay out of the snapshot: it is copied onto the stack
of every task that polls it, and three more strings would cost 384 bytes on each
of them. What travels is `track_tag_revision` - the tags are read after the
track has already started, and without a counter the page would go on showing
the file name until the track ended.

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
is the cheaper answer. The URL carries the cover's signature - a checksum of the
bytes it was decoded from - and the answer itself is marked `no-store`.

The URL used to carry the generation instead, and the answer was cached for a
day. The generation counts from zero at every boot, so `?g=1` meant one picture
on Tuesday and another on Wednesday, and three browsers showed three different
covers for one track, each the one it had cached first. A signature cannot do
that: one picture, one URL; different pictures, different URLs.

The rotor's covers do not come from the device at all. `/api/progress` hands the
page the picture's address on `avatars.yandex.net` at 400x400 and the browser
fetches it itself: the device pays nothing - only the address travels - and the
page draws something four times as detailed as the 96 pixels the panel decodes
it into.

A station's own picture is the playlist's fourth column, after the letter and a
tab; the column is optional. A picture chosen in the browser is scaled there to
96 pixels on its longer side and re-encoded as PNG - kilobytes travel to the
device, not a photograph - and the device names the file itself: a name that
comes from a client is a name that can turn out to be a path. The extension
comes from the first bytes rather than from the request's header, so PNG and
JPEG are each stored under their own: the picture is handed back over HTTP
afterwards, and a JPEG served as `image/png` is a broken picture in the browser.
It doubles as the only check that a picture arrived at all. The files live in
`/littlefs/radio_img/`, and saving the playlist deletes the ones nothing refers
to any more - that is the one moment when the full list of names in use is
known.

## The playlist format

A line is `name<TAB>address<TAB>letter`, plus an optional fourth column naming
the picture file. The letter says which name to show: `S` for the one the
stream announces, `L` for the one kept in the list.

The letter sits where playlists written for other devices - and there are many
of those about - keep a volume correction: a signed integer, almost always
zero. Those lines are read too, but only for their name and address: the volume
here is one setting and there is no per-station gain in the model. The
correction is dropped, the stream gets to announce the name, and nothing in
that dialect could name a picture. Lists of two columns, with no third at all,
read the same way.

The letter is what makes the two tellable apart. The column used to hold 0 or
1 - exactly what a correction of 0 or +1 dB is written with - so a foreign 1
read as "name from the list", and every other correction took the whole line
down as malformed.

The order is changed by dragging a row by the grip left of its name. The grip
is what moves, not the whole row: the list is scrolled with a finger, and a row
that followed it would take away the only way to scroll at all. It is built on
pointer events rather than the drag and drop built into HTML, which a finger
cannot start at all - and a list of 99 stations is exactly what gets sorted on
a phone. The up and down arrows on that same grip move a row from the keyboard,
and the order, like any other edit, reaches the device on Save.

Export hands over `playlist.csv` while no station has a picture, which is a
file anything can read. Once one does, it hands over `playlist.zip`: the same
`playlist.csv` with a `radio_img/` folder beside it. Import takes either,
telling them apart by the file's signature. The archive is built and read
entirely in the browser - the device needs neither an unpacker nor a buffer of
several megabytes - and it is written stored, since PNG and JPEG are already
compressed, but read with deflate too, because an archive assembled by anything
else will be compressed.

Imported pictures travel to the device when Save is pressed rather than when
the file is opened: an import that is then abandoned would otherwise leave up
to 99 unwanted files there, and the only thing that ever deletes them is the
next save.

The picture is published through the same `album_art` that carries file and
rotor covers, so the panel's tile and the browser's both draw it without a line
of new UI code. The key is the file name rather than the station index: replace
the picture of the station that is playing and it updates.

The device settings are written by both the panel and the web, through the same
setters, serialised at the file level. A write from the browser raises a flag,
and the UI task re-reads `settings.csv` and re-applies the brightness, the
panel rotation, the volume and the Yandex row - a stored value does none of
that by itself.

The rotation is the one exception: there is no way to turn the boot splash over
once it has been drawn. MADCTL decides where *arriving* pixels land and moves
nothing already on the glass, and the splash is drawn at the end of
`board_init()`, long before the UI task exists. So `app_main()` mounts LittleFS
and reads the two flip flags before the board, and passes them into
`board_init()` - the only settings it needs. If they cannot be read the flips
are off, which is the panel's own baseline. It costs the screen nothing: the
backlight sits at zero until the splash is on the glass.

They come back as a `settings` section on the socket rather than by polling:
the knob changes the volume without telling anyone, and `settings.csv` is
eleven consecutive reads - far too much to ask on a timer. The UI task
publishes a copy in memory, the broadcaster reads it every 250 ms and sends a
diff only when something has moved. That section grew the complete snapshot by
270 bytes and pushed it past 4096, so the frame limit is now 4608;
`test_web_server.c` builds the worst case - 32 stations named in nothing but
quotes and backslashes - and asserts the margin is still there.

Static assets are served with `Cache-Control: no-cache` and an `ETag` over the
file's bytes: the browser keeps them but asks before showing them. An unchanged
file costs one `304` with no body, and after `littlefs-flash` an ordinary reload
picks up the new styles and scripts. They were cached for an hour before that,
and that hour meant a page running against a device that had already moved on.

**There is no authentication and `Origin` is not checked. Do not expose the
device.**
