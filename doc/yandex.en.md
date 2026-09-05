# Yandex Music

[← README](../README.en.md) · [Русский](yandex.md)

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
the source nor the switch in Settings. See [What the home screen shows](hardware.en.md#what-the-home-screen-shows).

An account can be unlinked from the web interface.
