# Yandex Music

[← README](../README.en.md) · [Русский](yandex.md)

An active subscription is required.

**Linking the account.** In the web interface, the Yandex Music section: the
device shows an address and a short code - open the address on a phone or a
computer and enter the code. No password is typed on the device or stored
anywhere. The account is unlinked in the same place.

**Stations.** "My Wave" and the waves Yandex picks for you - the same ones the
music.yandex.ru site shows under "My Wave": genres ("All that jazz"), moods and
activities ("Run faster than the wind"), artist waves. An artist wave plays
music in the artist's style, not only their songs, so the name carries
"artist-style wave" (on the panel, "· alike"). The selection is personal and
changes; stations already in the list keep their places.

**Dropped connections.** A track is a file, and the radio holds about 8 s of
it ahead. If the connection to Yandex's server breaks or goes quiet, the track
is fetched on from the same byte in the background while that plays - a drop
is usually not heard.

**Controls.** Play, pause, next track - Next on the device or the button in the
web interface. There is no "previous track": a station only goes forward.

**Like.** Prev sets and clears the mark - the track joins your favourites or
leaves them, as in the app. On the player screen it is the heart next to the
volume: outlined without a mark, filled with one. What was liked before,
Yandex reports itself, so the heart is filled for songs marked from the phone
too.

**Dislike.** A double press of Prev: the track goes to the rejected ones, the
station stops offering it, the heart becomes crossed out. Any next press of
Prev clears the mark. In the web interface dislike has its own button next to
the heart. The two marks exclude each other.

**The device tells Yandex what you listened to** - what was started, what
played to the end, what was skipped - the same events the app sends. Without
them the station does not know a song has played, and tracks repeat.

**If you do not need it.** Settings → General has a "Yandex Music" switch: the
source disappears from the home screen, the link is kept. To take it out of
the firmware entirely, set `YANDEX_MUSIC FEATURE_OFF` in `board_options.h`.
