# Changelog

[← README](../README.en.md) · [Русский](changelog.md)

What each version added and fixed, briefly. The firmware files are in the
[releases](https://github.com/jmper-ha/jradio/releases); the step-by-step
detail is in the commit history.

## Next version

New:
- With nothing playing, the station list opens with the cursor on the
  station played last rather than the first - after coming back from the
  sound settings through the menu, for one.
- The screensaver has a new "Comes on" setting: "Always" - as before, the
  set time after the last touch - or "When nothing plays": while something
  plays the screen stays up, and the screensaver comes on the same time
  after a pause or a stop. Music starting - the alarm, say - wakes the
  screen by itself. In the web settings, under Screen.
- A weather screensaver: the whole weather, like a weather station, with
  colour pictures - the clock, the temperature and what it feels like,
  humidity, pressure, the wind with an arrow and gusts, sunrise and sunset,
  cloud cover and precipitation, a three-day forecast (from Open-Meteo and
  OpenWeatherMap). On every screen, each with its own layout; once a minute
  the picture moves by a pixel against burn-in. Chosen under Screensaver on
  the device or on the web.

## v1.8.6 — 21 October 2026

Fixed:
- A crackle on 22050 Hz AAC stations with the loudness on or the tone off
  zero. On those stations a block of sound went to the DAC in two halves,
  and the second went through the tone's filters twice - the seam clicked.

## v1.8.5 — 20 October 2026

New:
- Even volume: stations and tracks play equally loud. The radio measures a
  station's loudness the way broadcasters do (EBU R128), brings it to
  -14 LUFS and remembers it per station. Yandex Music tracks and files with
  ReplayGain/R128 tags already have theirs known, and start at their level.
  Switched on under Sound on the device or in the web's tone panel; off by
  default. The correction shows over the volume figure, in the station
  name's colour while the level is being found and in its own once settled.
- A volume nudge of your own for an internet station, +-6 dB: a Station row
  in the quick panel and a slider in the web's tone panel. Kept per station,
  and works without even volume as well.
- The boot log names the button pins, and warns of a button held at power-on.
- The encoder's and the buttons' internal pull-ups are always on, and the
  editor's switches for them are gone. A button can go straight between its
  pin and ground: a Sleep button with no pull-up of its own no longer
  presses itself and puts the board to sleep.

Fixed:
- Bad internet: a station no longer gives up with "could not connect". While
  the backlog plays, the radio reconnects in the background, so a short drop
  goes unheard; once it is spent, "Reconnecting..." shows and the tries go
  on until the station is back (every second at first, then less often, down
  to once a minute). A press of the encoder during "Reconnecting..." stops
  the station.
- After a dropped connection the station played again while the screen kept
  "Reconnecting..." and the buffer gauge froze.
- A station that did not answer could stay unreachable until a reboot: its
  address is now looked up afresh after a failed try. The wait for a server
  is 5 seconds rather than 10.
- Switching station from the web flashed the station list on the screen for
  a second.
- The warning about an all-zero audio block repeated in the log hundreds of
  times in a row.

## v1.8.1 — 18 October 2026

New:
- The tone and loudness are at hand: in the web player behind a button beside
  the volume (a sheet from the bottom on a phone), on the device in the quick
  panel. The web settings drop their Sound group.
- In the quick panel the switches (alarm, BT speaker) flip with one press of
  the encoder.
- Three buttons instead of the encoder: +, - and OK on the same pins, chosen in
  the wiring. Held, + and - repeat, faster and faster.
- A remote's volume keys step as the encoder does, by the "Volume step"
  setting.

Fixed:
- Yandex Music: after a dropped connection a track is picked up in about half a
  second instead of 4-10: the radio first tries the same stream address,
  without the API.
- VBR MP3: the bitrate on screen no longer jumps (the average is shown), the
  track length is exact and a jump lands where it should.
- Firmware built from a ZIP download (no git) showed version "1".

## v1.8.0 — 14 October 2026

New:
- Tone controls (bass and treble, ±12 dB) and loudness in three strengths -
  on the device in a new Sound group, and in the web interface.
- Yandex Music: genres, moods, activities and artist waves are back - the same
  ones the site shows under "My Wave". Yandex's station list shrank to "My
  Wave" alone in the last days; the stations now come from where the site
  takes them. Artist waves are marked "artist-style wave".
- Yandex Music plays through radio "sessions", as the official apps do.
- Ordinary MP3 and AAC stations keep everything the server sends ahead, up to
  128 KB - usually 3-8 seconds instead of 1-2.

Fixed:
- Dropouts with a full buffer on HTTPS stations: with packet loss a read
  waited for the missing part of a TLS record and held the decoder. Now the
  buffer plays out, and the meter shows it draining.
- A dropped station connection no longer means silence: the reconnect runs in
  the background while the backlog plays, and a server gone quiet is noticed
  after 1.5 s.
- Yandex Music: a track cut off is fetched on from the same byte in the
  background - no skipped track, no repeated stretch, no gap; no more extra
  seconds of silence at the end of a track.
- Yandex Music: the list no longer reshuffles on a refresh, and the wave
  picked in the web page is the one pressed.
- A Bluetooth module that answered late could stay on the bus with the board:
  USB crackled and DLNA was silent until a restart.

## v1.7.0 — 11 October 2026

New:
- A station search on the Stations page: the open Radio Browser catalogue
  (about 50 000 stations) by name, genre and country; a station can be
  listened to in the browser and added to the list with its picture.
- "Find pictures" - pictures for stations that have none, by stream address
  or by name.
- The catalogue is searched through a jRadio copy on GitHub, which opens
  where the catalogue itself does not; the catalogue directly is a choice too.
- A DS3231 or DS1307 real-time clock: the time is there right after power-up,
  even with no network, and the alarm wakes without Wi-Fi.
- The alarm can play FM radio when there is a tuner.
- The alarm can switch the radio off at a set time - only when the alarm woke
  it.

Fixed:
- A sleeping radio no longer wakes on noise from the IR receiver, only on the
  remote's Power key.
- Switching Yandex Music stations, the web page showed the previous station
  for a couple of seconds.
- The saved Yandex Music station sometimes did not start at power-up after a
  network hiccup in the first seconds - the request is now repeated.

## v1.6.2 — 10 October 2026

Fixed:
- Noise on the ILI9488 screen since 1.6.0: its lines have their former
  drive strength back.

## v1.6.1 — 10 October 2026

New:
- A new memory layout: 4 MB for the firmware instead of 3 - room for new
  features.
- Update rollback: when a new firmware does not start, the radio goes back to
  the one before by itself.

When moving to it:
- Flashing by cable or from the flasher onto a board with a version before
  1.6.1 changes the layout and erases the settings, the Wi-Fi networks and
  the stations. Download a backup and an export of the stations first and
  restore them after. The flasher warns and writes everything needed by
  itself.
- A radio on 1.6.0 updates over the network as usual, keeping its data; it
  gets the new layout at its next flash by cable.

## v1.6.0 — 10 October 2026

New:
- Firmware updates over the network: once a day the radio learns about a new
  version, the web interface shows what is new and installs it with one
  button. Versions that were skipped are listed together.
- An update from a file in the web interface, confirmed with the encoder on
  the radio.
- The settings sections of the web interface in a new order.

Fixed:
- Crackle on some boards: HTTPS is decrypted in hardware again.
- Clicks on the ILI9341 and ST7789 displays: the data goes to the screen in
  shorter, gentler bursts.
- Clicks while the volume changes: it now changes smoothly, and the volume
  and the brightness are saved without a pause in the sound.
- The screen no longer freezes after a turn of the encoder or while a browser
  downloads the backup.
- Yandex Music starts even when the network came up a moment late.
- N16R8 boards with two Type-C connectors start (the flash runs in QIO).
- The flasher finishes writing the data to a slow flash in time, and refuses
  Firefox, where the write came out broken.
- More free memory: steadier HTTPS and updates.

## v1.5.5 — 6 October 2026

New:
- Colour themes: Standard, Contrast for displays whose text looks washed out,
  and Custom — six colours picked in the web interface.
- Neater lists: thin lines between rows, a bright mark on the selected row,
  clearer group headings in the settings.
- The flasher puts the radio on Wi-Fi over the cable, no access point needed.
- The lowest volume is quieter, and every step at the bottom can be heard.
- A setting for how far one click of the encoder moves the volume.
- The web station lists show station pictures and keep the playing station in
  view; a long station name scrolls on the player.

Fixed:
- The encoder no longer loses clicks on a quick turn.
- Grey text on the ILI9341 is easier to read.
- The screen no longer freezes while the settings page is open in a browser.

## v1.5.0 — 2 October 2026

New:
- FM radio on an RDA5807: the frequency in large digits, a station scan,
  manual tuning, the station name and RDS text.
- Three ways to bring the FM sound in, including through a PCM1808 ADC.
- One Stations page on the web for internet radio and FM, with one import and
  export.

Fixed:
- A track picked from a .cue on a CD rip is the one that plays.
- A board with a remote and no buttons can sleep from the remote.
- The screen goes dark before the board sleeps.

## v1.4.0 — 26 September 2026

New:
- Flashing from the browser: describe the board in the wiring editor and
  write the firmware from Chrome, nothing to install.
- The wiring is read at boot instead of being built in: one firmware per
  display fits any board.
- Albums ripped to one file with a .cue sheet: tracks by title, no gaps.
- An "end of folder" setting: stop or start over.
- 96 kHz / 24-bit FLAC from a USB drive plays without stutter.
- The level meter moves while a phone plays over Bluetooth.
- Display colour inversion as a setting.

Fixed:
- HTTPS streams on a fully fitted board.
- Large covers, folder covers on 480×320, an apostrophe in a song title, the
  station list in Chrome on Android.

## v1.3.0 — 20 September 2026

New:
- An infrared remote: its keys are learned in the web interface, the digits
  dial a station.
- The remote wakes the device from deep sleep.
- Waking from sleep is a second faster.

Fixed:
- Yandex Music plays on the 480×320 display again.
- The sleep timer shows 120 minutes in full.

## v1.2.0 — 18 September 2026

New:
- Deep sleep on a button, with the power to the peripherals cut.
- A sleep timer: the music fades out after 15–120 minutes.
- An alarm clock: a station at a set time on the days chosen; the device wakes
  by itself.
- A quick panel over any screen: the timer, the alarm, the brightness, the
  speaker.
- Autoplay comes back to Bluetooth too.
- Brightness up to 100%, the amplifier muted while nothing plays.

Fixed:
- A station moving from https to http.
- A server's service text shown as the track title.
- Keys stuck while a station's server does not answer.
- The built-in station no longer added to someone's own playlist.

## v1.1.0 — 14 September 2026

New:
- Bluetooth both ways through the jradio-bt module: a phone plays through the
  radio, the radio plays to a speaker or headphones.
- A name of the device's own, for Bluetooth and the access point.
- The ST7789 320×170 display.

Fixed:
- The clock screensaver on the ILI9488.
- Finding speakers and choosing the one that connected.

## v1.0.1 — 12 September 2026

New:
- A media server (DLNA) as a source.
- The weather beside the clock, from a choice of three services.
- A screensaver: dimming, a black screen or a floating clock.
- The time zone and the time server as settings.
- The whole interface in English.
- A backup of the settings in one archive.
- The ST7796S display.

Fixed:
- A station whose stream starts mid-frame plays at once.
- A media server no longer freezes the screen and the web interface.

## v1.0.0 — 6 September 2026

The first numbered version: internet radio, music from a USB drive and an SD
card, Yandex Music, a web interface, ILI9341, ST7789 and ILI9488 displays.
