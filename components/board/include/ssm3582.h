#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* An SSM3582 class-D amplifier on I2C0 - the bus half of its driver; the
 * registers and their encodings are ssm3582_chip.h.
 *
 * The part is selected by name in board_options.h (AUDIO_DAC =
 * DAC_SSM3582) and board.c is the only caller: it hands this driver the
 * same signals it would hand an amplifier's MUTE pin (open while a source
 * plays), the volume setting, and every sample-rate change. All of it is
 * safe to call on a board the chip never arrived on: setup says so once,
 * and the rest do nothing until it answers - the board keeps its volume
 * in the samples instead. */

/* Find the chip on I2C0 - the bus is made on the ask, from the FM_I2C_*
 * pins, exactly as the tuner and the clock do - and set it up for the
 * format this firmware drives: Philips I2S, 16-bit samples in 32-bit
 * slots, BCLK = 64 x Fs, at `sample_rate`, listening at
 * `volume_percent`. The chip is left powered down; ssm3582_set_playing()
 * opens it when a source starts, and the pre-roll of silence that clocks
 * ahead of the first sample covers its 10 ms power-up many times over.
 *
 * Asking for the chip opens its log gate too: from this call on, the tag
 * logs at debug, so every register write over the bus is in the log. Where
 * no chip answers, the line still says so at warn. */
esp_err_t ssm3582_setup(uint32_t sample_rate, uint8_t volume_percent);

/* Whether a chip was found and set up. Where it is not, board.c keeps
 * the digital volume it has always had. */
bool ssm3582_ready(void);

/* Open and close the amplifier: the chip's own soft power-down and
 * power-up, click-free by construction, which is why the firmware asks
 * for them before a clock stops and after one starts. */
void ssm3582_set_playing(bool play);

/* The listening level, in the chip's soft-ramped volume register: a turn
 * of the knob is a ramp there, not a step in the samples. */
void ssm3582_set_volume(uint8_t percent);

/* Tell the chip which band the clock it is being given belongs to - its
 * BCLK rate is autodetected, its sample rate is not. A rate the chip has
 * no band for, or one the 64 x Fs geometry cannot carry, is reported and
 * left alone; the chip stays quiet for it rather than playing something
 * wrong. */
void ssm3582_set_sample_rate(uint32_t sample_rate);