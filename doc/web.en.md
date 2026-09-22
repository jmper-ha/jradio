# Web interface

[← README](../README.en.md) · [Русский](web.md)

Opens in a browser at the device's address (shown at the bottom of the
settings screen) from a phone or a computer on the same network. Nothing to
install. Russian and English, a dark and a light theme.

**There is no authentication and `Origin` is not checked. The interface is
meant for a trusted home network - do not expose it.**

## Pages

| Page | What is on it |
|---|---|
| **Player** (`/`) | What is playing, the cover, volume, position, buttons; source choice and the lists of stations, files and server folders; the sleep timer and the alarm |
| **Playlist** (`/playlist`) | The station list editor: name, address, picture, order by dragging; trying an address by ear right on the device; import and export |
| **Settings** (`/settings`) | Everything the device has, plus Wi-Fi networks, the name, time and zone, weather, the alarm, the sleep timer, Yandex Music, Bluetooth speakers, the backup, About |
| **Remote** (`/remote`) | Learning the IR remote's keys; opened by a button in the settings when the build has a receiver |

On a phone the settings sections fold: one open, the rest as headers. Live
data (the player state, the lists, the settings) comes over a WebSocket, so an
open tab always shows what the device's screen shows - including the volume
turned on the encoder.

## The station list

A line is `name<TAB>url<TAB>letter`, with an optional fourth column - the
picture's file name. The letter says which name to show: `S` - the one the
stream announces, `L` - the one written in the list. Foreign playlists that
keep a volume correction in the third column are read too: the name and the
address are taken.

A station's picture is chosen in the browser, shrunk to 96 pixels and stored
on the device. Export gives `playlist.csv`, or `playlist.zip` with the
pictures when there are any; import takes either. The order is changed by
dragging the handle left of the name (or with the arrow keys). Everything
goes to the device on Save. Up to 99 stations.

## The remote

A table of functions with Learn and Forget buttons. While the page is open, a
pressed remote key lights its row, and an unlearned key is named by its code -
that is how a new remote's codes are found out. Learning waits 30 seconds for
a key; if none came, the page says so. More in
[The remote control](usage.en.md#the-remote-control).

## Backup

`GET /api/backup` gives a zip with five files: `wifi.json` (the networks),
`settings.csv` (the settings), `yandex.json` (the token), `weather.json` (the
OpenWeatherMap key), `remote.csv` (the keys the remote was taught). `POST
/api/restore` takes the whole archive or one file;
the device checks all files before writing, writes them and reboots. The
station list is not in the archive - it has its own export on the playlist
page.

**The archive holds the Wi-Fi password, the token and the key in clear
text.** The device gives it to anyone on the local network. Keep the file as
you would a password.

## API

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Wi-Fi and player state in one snapshot |
| `GET /api/playlist` | The station list as CSV |
| `POST /api/playlist` | Replace the whole list |
| `GET /api/stations` | The names of the active source's stations; `?source=internet_radio` - always the radio |
| `GET /api/files` | The current folder of the drive |
| `GET /api/dlna` | The open folder of the media server; `searching` - the server is still being looked for |
| `GET /api/progress` | Position in the track, the buffer, the cover's signature, the sleep timer's remainder |
| `GET /api/cover` | The current cover, 96×96, BMP |
| `GET /api/settings` | The device's settings, the time zone list, the time server |
| `POST /api/settings` | One setting: `{"field":…,"value":…}` - including `timezone`, `ntp_server`, `weather*`, `screensaver*`, `alarm_*` |
| `POST /api/sleep-timer` | `{"minutes":45}`, zero switches it off |
| `GET /api/about` | The firmware, web interface and ESP-IDF versions |
| `GET /api/backup`, `POST /api/restore` | The backup, see above |
| `POST /api/station-test` | Play an address on the device without touching the playlist |
| `POST /api/station-icon`, `GET /api/station-icon` | Upload / fetch a station's picture |
| `GET /api/yandex`, `POST /api/yandex` | Link state and the account's stations; link, unlink, refresh |
| `GET /api/remote`, `GET /api/remote/last` | The remote's table; the last key received |
| `POST /api/remote/learn`, `POST /api/remote/forget` | `{"function":"volume_up"}` - learn / forget |
| `POST /api/wifi` | Save a network |
| `POST /api/wifi-scan`, `GET /api/wifi-scan` | Scan for networks (only while unconnected) and its result |
| `/ws` | Commands and live updates |

**WebSocket commands:** `player.play`, `player.pause`, `player.toggle`,
`player.next`, `player.previous_item`, `player.next_item`, `player.seek` (a
second), `player.like`, `player.dislike`, `source.select`, `list.select`,
`browse.up`, `wifi.save`, `wifi.forget`, `wifi.prioritize`, `wifi.disconnect`.
Back come diffs of the `player`, `list`, `wifi` and `settings` sections. Big
things - the playlist, folders, the position, the cover - go over REST so as
not to load the socket.

Remote codes are written as `nec:<address>:<command>` (`nec:4:08`) or
`raw:<hash>` for a protocol the decoder does not know.

The alarm in the settings is five fields: `alarm_enabled`, `alarm_time`
(`"07:30"`), `alarm_days` (a mask, bit 0 is Sunday, zero is refused),
`alarm_station` (a number counting from one), `alarm_volume`.

## How it works

The static files live in the data partition and are served gzipped, with an
`ETag`: after a data update an ordinary page reload is enough. Settings
changed in the browser apply on the device at once; ones changed on the
device reach the page within a quarter of a second. The language is stored by
the device, so the switch on the page and the row on the screen are one
switch.
