# Music from a media server (DLNA)

[← README](../README.en.md) · [Русский](dlna.md)

The device finds a media server on the home network by itself and plays what
is on it. Nothing to set up: no address to type, no folders to configure.

**Which servers work.** Any DLNA/UPnP media server on the same network: a
NAS, Plex, Jellyfin, miniDLNA, Home Media Server. Verified with Plex Media
Server on a NAS.

## How it works

1. Choose the Media server source on the device, or the DLNA tab in the web
   interface. The screen says "Looking for a media server" - usually about a
   second.
2. The server found opens at its top level; its name is in the list header.
   With several servers, their list is shown first.
3. Then it is like a flash drive: the encoder scrolls, a press opens a folder
   or plays a track, `..` at the top goes up a level, a long press leaves the
   source. While a folder loads, a bar runs at the bottom.

The search runs on every entry into the source - the server may have been
switched off, moved, or appeared later. If nothing is found, the list says so:
"No media server found on the network".

At the top level only the music sections are shown: `Video`, `Photos` and the
like are hidden. Rows that cannot be played (video, a format without a
decoder) stay in the list but are marked and do not respond.

**What plays:** MP3, AAC, FLAC, Ogg - with the same decoder as internet
radio. WAV does not play from a server (it does from a stick). The performer,
the title and the cover come from the tags the server sends. When a track
ends, the next one in the folder starts; Prev/Next and the web buttons step
through.

## Resume

With resume on, the device remembers the server, the folder and the track,
and after power-on goes back there: finds the server, opens the folder and
starts the track. The first sound comes about four and a half seconds after
power is applied.

If something changed - the server did not answer, the folder vanished after a
library rescan, the track is not in it - the device steps back: opens the
server's root, plays the folder's first track, or shows a list to choose
from. There is no silent screen.

## Limits

- The first 64 rows of a folder are taken - browse big libraries through
  folders or artists.
- No server-side search; the search box in the web interface filters what is
  shown.
- No seeking inside a track.
- The place in the tree is not remembered, except by resume.
- Plain HTTP only.
