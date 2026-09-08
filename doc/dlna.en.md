# Music from a media server (DLNA)

[← README](../README.en.md) · [Русский](dlna.md)

The device finds a media server on the home network by itself and plays what is
on it. Nothing needs setting up: no server address to type, no folders to
declare.

**Which servers.** Any DLNA/UPnP media server on the same network: a NAS, Plex,
Jellyfin, miniDLNA, a media server on a PC. Tested against Plex Media Server
1.42 (Platinum stack) on a NAS.

**Finding one.** Pick the DLNA source on the device, or press the DLNA tab in
the web interface. The device sends a query across the network and waits for
answers, saying "Поиск медиасервера" while it does. Usually about a second: the
wait ends when the network goes quiet rather than when the whole window
expires. The server that replies opens at its top level, with its name as the
heading of the list.

While the search runs the list is empty and says nothing about it - the bar at
the foot sweeps instead. "Медиасервер не найден в сети" appears only once the
search has finished and found nothing.

**What the rest costs.** Opening a folder is one request to the server, or
several for a large one. Measured on this network: a small folder about 0.1 s,
a 52-row folder 0.6 s. While a request is out, the bar under the list - the slot
the player screen gives the buffer - sweeps a segment back and forth; in the web
interface the rows dim, since they are still correct but there is no point
pressing them. The movement is the point: a caption that sits still looks
exactly like a device that has hung, which is why there is no caption, only the
bar.

A folder that will not open says so - "Папка не открылась" - rather than
leaving the wait to run out.

The search runs every time the source is entered rather than once at start-up.
That is deliberate: a server may be switched off, may move to another address,
or may appear after the device has already booted.

**When there is more than one server.** If two or more answer the search, the
device opens none of them and shows the list instead: one row per server, named
as the server names itself. The one chosen opens at its top level, and the `..`
row at that top level goes back to the list, so switching is always one step
away.

Opening whichever answered first was not good enough precisely because "first"
means "quickest to reply to a multicast" - a different server from one power-up
to the next. With a single server nothing changes: there is no extra screen, it
opens straight away.

At most four servers are taken, and one that replies more than 600 ms after the
previous one will not be in the list - the search has stopped listening by then.

**Walking the server.** Exactly like a flash drive. The knob scrolls, a press
opens a folder or starts a track, and the `..` row at the top goes back up. A
long press leaves the source.

Every server lays its tree out differently. On Plex it is `Music → your library
→ By Album / By Folder / All Artists → …`.

At the very top level the device shows only the music sections: `Video`,
`Photos` and their like are hidden - they can be walked into, but there is
nothing to listen to in them. The name is all there is to tell them apart: a
server marks every section the same way and says nothing about what is inside.
So only what is recognised for certain is hidden, an unfamiliar section stays
where it is - and if everything were hidden the listing would come back whole
rather than empty.

**What plays.** MP3, AAC, FLAC, Ogg. The server hands out an ordinary HTTP
link and from there the same decoder runs as for internet radio - which, unlike
the file player, has no WAV decoder at all. So a WAV row from a server is
marked unplayable even though the same file plays from a drive: there the
format comes from the file name, here from the type the server declared.

Rows that cannot be played - a video, or a format with no decoder - stay on the
list, marked and unclickable. They are not hidden on purpose: a list without
them would look like a server with files missing.

**What is shown.** The performer and the title come from the tags the server
sent, not from the file name. The cover comes from there too. When a server
offers two pictures the device takes the smaller one: only 160 pixels reach the
panel, and the rest would be bytes fetched to be thrown away.

**Through the album.** When a track ends the next one in the same listing
starts, as on a flash drive. F3 and F4 step to the neighbouring track, and so
do the back and forward buttons in the web interface.

## Limits

**The first 64 rows of a container.** Libraries get large - "By Album" on the
test server holds 769 of them - and no amount of them can be scrolled with a
knob anyway. Go in through folders or artists.

**No search on the server.** The device only walks the tree; the search box in
the web interface filters what is already on screen.

**No seeking inside a track.** The server allows it, but the device plays the
stream from start to end, the way it plays radio.

**The place in the tree is not remembered.** Leaving the source starts the next
visit at the top level again, and autoplay cannot resume from a media server.

**Plain HTTP only.** A media server on a home network does not encrypt, and the
device does not expect it to.
