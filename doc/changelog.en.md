# Changelog

[← README](../README.en.md) · [Русский](changelog.md)

What each version added and fixed, briefly. The firmware files are in the
[releases](https://github.com/jmper-ha/jradio/releases); the step-by-step
detail is in the commit history.

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
