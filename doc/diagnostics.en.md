# Diagnostics and limits

[← README](../README.en.md) · [Русский](diagnostics.md)

## The log

The log goes over the board's UART port: `idf.py -p PORT monitor`, or the
**ESP-IDF: Monitor** task in VS Code. The first line after boot is the reason
for the last reset: after a reboot loop it is the difference between "the
power was pulled" and "a panic".

### Audio

Every 10 seconds, two lines about audio: the output and the stream intake.

```
board: audio health: realtime=100% peak=21533 silent=0 zero_run=0 underruns=0
internet_radio: stream health: i2s_underruns=0 starvations=0 min_backlog=131072/262144 (819ms) pcm=100%
```

A healthy picture: `realtime` 99-100 %, the other counters zero. If there is
no sound anyway, the trouble is around the DAC, not in the firmware. Separate
warnings - `audio gap`, `audio silence`, `audio zero-run` - mark the moment of
an event; `decode_stalls` counts the times the decoder consumed data without
producing sound - after two seconds of that the stream restarts itself.

### Memory and stacks

Once a minute, the state of the resources that run out quietly:

```
health: uptime=120s internal_free=52027/min 52027 largest=30720/min 30720 dma_largest=30720 psram_free=8053800
health: stack headroom, least seen: player_control=3780 ui=2524 usb_play=5184 ...
```

Look at the minimums, not the current values: a stack overflows the first
time the compiler inlines a little more, and a shortage of internal memory
looks like "HTTPS won't connect" or "the file won't play".

## Screen flicker

If the picture flickers - more on midtones and at high brightness, worse while
playing - the panel is powered from the ESP32-S3 module's 3V3 pin. Its LDO
cannot carry the panel along with everything else, and it depends on the
module: two identical modules on the same board behave differently. The fix
is a separate regulator for the panel (an AMS1117-3.3 from +5 V). Capacitors
on the module's pins, the bus speed, the backlight PWM and the firmware
version have nothing to do with it - all of that was tested.

## Limits

- **At most 99 stations in the playlist.** Lines beyond that are dropped, and
  the upload's reply says how many were read. A station name is up to
  96 bytes, an address up to 256.
- **At most 256 entries in a folder of a drive.** The rest are dropped with a
  warning.
- **FAT16 and FAT32 only.** exFAT is not supported - drives of 64 GB and up
  usually come in it from the factory.
- **The card is not kept mounted.** It is found on entering the source; a card
  inserted later is found on the next entry.
- **About 1.7 s between Yandex Music tracks** - the track link is requested at
  the last moment.
- **The first 64 rows of a media server folder are taken**; there is no
  server-side search.
- **The web interface is for a trusted local network only.** No
  authentication.
- **Flashing is over UART**; the board's USB connector belongs to the stick.
- Internal SRAM is the board's scarcest resource: the large buffers live in
  PSRAM, so take a module with it (8 MB).
