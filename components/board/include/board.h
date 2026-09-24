#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* The display's two flip settings are arguments, and so is the colour
 * inversion, because this ends by drawing the boot splash: MADCTL steers where
 * arriving pixels land and moves nothing already on the glass, so a splash
 * drawn before the switches are known stays the way it was written. Inversion
 * would take effect on the glass at once, but a splash that flashes up as a
 * negative and then corrects itself is not a boot anyone should watch.
 * Everything else the settings decide - brightness, volume - is applied later
 * by the UI, which is why only these three have to be here. */
/* `dark` leaves the backlight at zero when the board comes up: the alarm's
 * last hop boots a minute before it rings, and a panel that lights the bedroom
 * at 6:59 is not what the alarm was set for. The splash is still drawn - the
 * UI raises the backlight when the alarm goes off, or at the first press. */
esp_err_t board_init(bool flip_vertical, bool flip_horizontal, bool invert_colors, bool dark);
esp_err_t board_backlight_set(uint8_t percent);
/* The switch feeding everything outside the module - the panel, the DAC, the
 * card, the Bluetooth module, the USB port - on boards that wire
 * PERIPHERAL_POWER_GPIO; a no-op elsewhere. board_init() turns it on, and
 * only deep sleep turns it off. */
void board_peripheral_power(bool on);
/* False on a board whose sleep button (F1 here) is not on an RTC pin: such a
 * board could not be woken by it, so it is never offered the sleep. */
bool board_deep_sleep_supported(void);
/* Cuts the peripherals, arms BUTTON_SLEEP_GPIO - F1 - as the wake source and
 * sleeps.
 *
 * `wake_after_seconds` arms the RTC timer as a second source, for the alarm
 * clock: 0 leaves the button as the only way back. Both sources are live at
 * once, so a board sleeping until the morning still answers the button.
 *
 * Does not return: waking is a fresh boot, so whatever has to be saved is
 * saved by the caller before it calls this. */
void board_deep_sleep(uint32_t wake_after_seconds);
/* The same sleep from a board that was never brought up: the alarm's quiet
 * wake-up runs before board_init(), decides it is still too early, and goes
 * back down without ever lighting the panel or raising the peripheral rail -
 * which is still held off from the sleep before it. Arms both sources and
 * touches nothing else. Does not return. */
void board_deep_sleep_again(uint32_t wake_after_seconds);
/* True when this boot was started by the infrared receiver's pin and not the
 * button - which says a remote sent something, not yet that it was ours. */
bool board_woke_by_remote(void);
esp_err_t board_audio_write(const void *pcm, size_t pcm_length, size_t *written,
                            uint32_t timeout_ms);
esp_err_t board_audio_start(const void *pcm, size_t pcm_length, size_t *preloaded);
esp_err_t board_audio_set_sample_rate(uint32_t sample_rate);
/* Told every time the output rate changes, and once on registration with
 * the current rate. For the Bluetooth module, which listens to the bus
 * and has to know what it is hearing; the board does not know the module. */
typedef void (*board_audio_rate_listener_t)(uint32_t sample_rate);
void board_audio_set_rate_listener(board_audio_rate_listener_t listener);
uint32_t board_audio_sample_rate(void);
esp_err_t board_audio_set_enabled(bool enabled);
/* The I2S pins handed to the Bluetooth module and taken back: released, the
 * channel is gone and the three pins are inputs; reclaimed, the channel is
 * created again at the last sample rate, disabled, ready for the next
 * start. Both are no-ops when already in that state. */
esp_err_t board_audio_release_bus(void);
esp_err_t board_audio_reclaim_bus(void);
/* The DAC's soft mute, on boards that wire AUDIO_DAC_MUTE_GPIO; a no-op
 * elsewhere. The stream keeps going either way - only the analogue output
 * is silenced, which is what a Bluetooth speaker taking the sound wants. */
void board_audio_set_dac_muted(bool muted);
esp_err_t board_audio_self_test(uint32_t duration_ms);
/* Monotonic count of I2S TX underruns (DMA ran dry mid-playback, i.e. an
 * audible dropout). Nothing else reports these; poll and diff to attribute
 * glitches to starvation rather than to decoding or the network. */
unsigned int board_audio_underrun_count(void);
esp_err_t board_display_draw_rgb565(int x1, int y1, int x2, int y2, const uint16_t *pixels);
/* The same rectangle from pixels already in the panel's wire order - swapped
 * with board_display_wire_pixel() - and straight from where they are, PSRAM
 * included: no band copy, one write. For a bitmap in PSRAM the buffer and its
 * size must be BOARD_DISPLAY_WIRE_ALIGN-aligned, or the SPI driver will try
 * to copy it into internal RAM. */
esp_err_t board_display_draw_wire(int x1, int y1, int x2, int y2, const uint16_t *pixels);
#define BOARD_DISPLAY_WIRE_ALIGN 64
/* A rectangle of one colour, given in wire order. */
esp_err_t board_display_fill(int x1, int y1, int x2, int y2, uint16_t wire_colour);
/* Shifts the whole picture along the panel's gate axis - screen x on a
 * landscape build, y on a portrait one - by `offset` pixels, in the
 * controller: it takes effect at the panel's next refresh, tear-free, and
 * costs the bus two bytes. What runs off one edge comes back in at the
 * other. 0 puts the picture back where it was written. */
esp_err_t board_display_scroll(int offset);
/* Native RGB565 pixels rewritten in place as the panel wants them on the
 * wire - a byte swap on the 16-bit panels, nothing on the converting ones. */
void board_display_to_wire(uint16_t *pixels, size_t count);
esp_err_t board_display_set_rotation(bool flip_vertical, bool flip_horizontal);
/* The user's inversion switch on top of the profile's measured baseline: IPS
 * and TN glass on the same controller read the same memory the other way
 * round, and a module from another shop shows a negative until this is
 * flipped. Takes effect on the glass at once. */
esp_err_t board_display_set_invert(bool invert_colors);

/* Loudest sample per channel since the previous call, then resets. Taking
 * rather than reading matters: PCM arrives a block at a time - 26 ms of MP3,
 * 93 ms of FLAC - while the UI polls every 10 ms, so a plain "current level"
 * would be sampled several times between blocks and would miss peaks whenever
 * the two rates drift.
 *
 * Returns false when no block has been written since the last call, which is
 * not the same as silence and must not be drawn as it: most passes fall
 * between two blocks, and on FLAC eight of every nine do. */
bool board_audio_level_take(uint16_t *left, uint16_t *right);

/* Hands the meter a reading taken somewhere else - the Bluetooth module's, while
 * a phone plays through it and no PCM passes this board. Merged exactly like a
 * block written here: the loudest since the last take, and marked fresh. */
void board_audio_level_put(uint16_t left, uint16_t right);

/* Playback volume, 0..100. The PCM5102 has no volume control, so this scales
 * the samples on their way to I2S - see audio_volume.h for what that costs.
 * 100 is bit-exact: the scaling is skipped rather than multiplied by one. */
void board_audio_set_volume(uint8_t percent);
uint8_t board_audio_volume(void);
