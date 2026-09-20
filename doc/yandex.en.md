# Yandex Music

[← README](../README.en.md) · [Русский](yandex.md)

An active subscription is required.

**Linking the account.** In the web interface, the Yandex Music section: the
device shows an address and a short code - open the address on a phone or a
computer and enter the code. No password is typed on the device or stored
anywhere. The account is unlinked in the same place.

**Stations.** The ones Yandex offers your account: "My Wave" and a few to your
taste. The list is the same as in the phone app.

**Controls.** Play, pause, next track - F4 on the device or the button in the
web interface. There is no "previous track": a station only goes forward.

**Like.** F3 sets and clears the mark - the track joins your favourites or
leaves them, as in the app. On the player screen it is the heart next to the
volume: outlined without a mark, filled with one. What was liked before,
Yandex reports itself, so the heart is filled for songs marked from the phone
too.

**Dislike.** A double press of F3: the track goes to the rejected ones, the
station stops offering it, the heart becomes crossed out. Any next press of
F3 clears the mark. In the web interface dislike has its own button next to
the heart. The two marks exclude each other.

**The device tells Yandex what you listened to** - what was started, what
played to the end, what was skipped - the same events the app sends. Without
them the station does not know a song has played, and tracks repeat.

**If you do not need it.** Settings → General has a "Yandex Music" switch: the
source disappears from the home screen, the link is kept. To take it out of
the firmware entirely, set `YANDEX_MUSIC FEATURE_OFF` in `board_options.h`.
