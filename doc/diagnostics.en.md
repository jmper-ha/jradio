# Diagnostics and limits

[← README](../README.en.md) · [Русский](diagnostics.md)

## Diagnostics

Every 10 seconds the firmware logs two lines about the audio, one for the
output and one for the incoming stream:

```
board: audio health: realtime=100% peak=21533 silent=0 zero_run=0 underruns=0
internet_radio: stream health: i2s_underruns=0 starvations=0 min_backlog=131072/262144 (819ms) pcm=100%
```

A healthy picture is `realtime` at 99-100% with every other counter at zero. If
there is still no sound, the fault is past the handover to DMA - in the DAC's
wiring, not in the firmware. Separate `audio gap`, `audio silence` and
`audio zero-run` warnings pin the moment an event happened, and `slow repaint`
reports a display pass that overran what this panel is allowed. The threshold
is not a constant: it is computed from the frame's size and the bus clock, and
comes out at 400 ms on a 320x240 panel and 740 ms on the 480x320 one, where a
full repaint honestly costs 249 ms. `decode_stalls` counts the
times the decoder consumed input without producing audio: two seconds of that
and the stream is restarted, because otherwise the device sits silent behind a
stream that looks perfectly alive.

Once a minute it reports the resources that run out quietly:

```
health: reset reason: power-on
health: uptime=120s internal_free=52027/min 52027 largest=30720/min 30720 dma_largest=30720 psram_free=8053800
health: stack headroom, least seen: player_control=3780 ui=2524 usb_play=5184 usb_msc=2692 ...
```

The minima are the point, not the current values: a stack that has been
shrinking for months overflows the first time the compiler inlines a little
more, and internal memory runs out as "HTTPS will not connect" or "the file
will not play" rather than as anything about memory. The reason for the last
restart is the first line printed - after a reboot loop that is the difference
between someone pulling the power and a panic.

`largest` is the biggest contiguous block, and it cannot be read on its own: it
may well be sitting in RTC RAM, which a task stack cannot be allocated from. If
something fails with `ESP_ERR_NO_MEM` while `internal_free` looks ample, get
the breakdown by region - `heap_caps_print_heap_info()` says which piece of
memory actually ran out.

## Screen shimmer on the ILI9488 board

On the board built here with the 480x320 panel the picture shimmers slightly,
and it is not the drawing. What was measured, and what it is not:

- **not the bus.** 20 MHz and 40 MHz shimmer identically;
- **not the redraw rate.** Thinning the animations from 100 a second to 30
  changed nothing;
- **not the backlight PWM.** 5 kHz and 25 kHz are the same, if anything worse
  at 25;
- **not the amount redrawn.** One second on the player screen is 11-23 flush
  calls and 4000-7700 pixels against 153 600 in a frame: the panel is idle over
  97% of the time, and only changed rectangles are ever sent.

What it does track: mid-tones - a white QR card and the near-black settings
screen stand perfectly still, album art shimmers worst; brightness - much less
at 10 than at 75; and load - worse while playing than paused. A flicker that is
invisible on white and on black but visible in the middle of the scale is the
signature of VCOM, the panel's analogue reference. That comes from the module's
own converter, which is fed from the board's rail - the same rail the backlight
current runs through.

From here it is hardware: put a meter on the module's supply at brightness 10
and 75, stopped and playing; fit 100 uF and 100 nF right at the connector; move
the backlight onto a rail of its own. The ILI9341 board does not shimmer at
all - but it is a different board as well as a different module, so that
confirms the picture rather than separating the causes.

## Limits

- **At most 99 stations in the playlist.** Lines beyond that are dropped as the
  file is parsed, and the reply to an upload says how many stations were
  actually read - the file itself is stored whole, so the number of lines in it
  and the number of stations in the device need not agree. A station name is
  limited to 96 bytes and its URL to 256; a longer line is dropped entirely.
  The catalogue is 35 KB and lives in PSRAM.
- **At most 256 entries per directory.** The rest are dropped with a warning;
  each entry costs about 264 bytes of PSRAM. The listing is one for both
  volumes - only one directory is on screen anyway, and a second copy would
  cost another 68 KB.
- **FAT32 or FAT16 only.** exFAT is not built in, and media of 64 GB and up
  usually ship formatted that way.
- **The card is not held mounted.** It is mounted when its source is entered
  (about 110 ms) and released when it is left: a mount costs some 2.4 KB of
  internal SRAM. The larger point is the missing card-detect line - a card
  inserted later is found only by the next attempt to enter the source, and
  that attempt is the detection.
- **About 1.7 s of silence between Yandex Music tracks.** A track link is valid
  for about a minute, so it is fetched at the last moment, and those requests
  run on the same task that decodes the audio.
- **The web interface assumes a trusted local network.** There is no
  authentication and `Origin` is not checked. Do not expose the device.
- Internal SRAM is the board's scarcest resource, and most of the large buffers
  live in PSRAM: the stream buffer, the directory listing, the station
  catalogue, LVGL's heap. Each of
  those allocations keeps a fallback to internal memory so that a board without
  PSRAM still works - keep that fallback in new ones too.
