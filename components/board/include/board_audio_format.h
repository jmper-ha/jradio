#pragma once

#include "board_options.h"

/* Everything that follows from the AUDIO_DAC selection: the sample format the
 * I2S link is fixed at, and the DMA geometry sized around this DAC's
 * behaviour. Shared rather than private to board.c because the players compute
 * against it - real-time byte rates, mono-to-stereo expansion - and a second
 * copy of these numbers would be a copy that drifts.
 *
 * Not in board_options.h: none of it is a wiring choice. Selecting a different
 * DAC would replace the whole set at once. */

#if AUDIO_DAC == DAC_PCM5102 || AUDIO_DAC == DAC_UDA1334A || AUDIO_DAC == DAC_SSM3582

/* The samples are fixed 16-bit stereo: mono sources are duplicated into
 * both channels by the players rather than reconfigured, and the sample
 * width has to match the slot width unless the selected part asks for
 * wider slots around the same 16-bit samples - see I2S_SLOTS_ALWAYS_WIDE. */
#define AUDIO_DEFAULT_SAMPLE_RATE 44100
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_CHANNEL_COUNT 2
#define I2S_SLOT_BIT_WIDTH 16

/* 8 x 512 frames provide about 92.9 ms of buffering at 44.1 kHz.
 * This geometry is deliberate: it fixed the audible clicking (see the
 * first-PCM I2S start design), so do not shrink it to reclaim the ~16 KB of
 * internal DMA RAM it holds - find that headroom elsewhere. It is also the
 * real tolerance to a source hiccup, which the radio prebuffer is sized
 * against. */
#define I2S_DMA_DESC_NUM 8
#define I2S_DMA_FRAME_NUM 512
/* Half of it each way while the input runs as well, for FM: the two rings
 * then hold the same 16 KB of internal RAM the output alone does, and the
 * input's could be found right after an internet station - its full 16 KB
 * on top was not there for five seconds. FM's samples arrive at the pace the
 * DAC takes them, so there is no source hiccup for the depth to cover. */
#define I2S_DMA_DESC_NUM_DUPLEX 4

/* Start output with real samples after a silent clock pre-roll instead of
 * enabling TX into an empty ring: 93 ms of zeros exceeds this DAC's zero-data
 * detect and would engage its analog mute. */
#define I2S_PRELOAD_SILENCE 1

/* The slots around the samples. The two line-level DACs take their
 * 16-bit stereo in 16-bit slots, BCLK = 32 x Fs, and only an ADC on
 * the bus forces the wider geometry (see board.c). The SSM3582 wants it
 * always: its modulator needs a BCLK no shorter than 2.048 MHz, and
 * 32 x Fs is under that at every rate this radio plays - two 32-bit
 * slots, 64 x Fs, is what its table lists from 32 kHz upwards. The
 * samples stay 16 bits, left-aligned in the slot, exactly as they
 * already are when an ADC widens them. */
#if AUDIO_DAC == DAC_SSM3582
#define I2S_SLOTS_ALWAYS_WIDE 1
#else
#define I2S_SLOTS_ALWAYS_WIDE 0
#endif

#if AUDIO_DAC == DAC_PCM5102
#define BOARD_DAC_NAME "PCM5102"
/* This DAC mutes itself when the clock errs or stops, so output may be
 * stopped by disabling the channel: whatever step the dropped DMA tail would
 * have left never reaches its output. Left at 0 deliberately - this is the
 * stop path every board here has been listening to, and the decay below is
 * not worth changing it for. */
#define AUDIO_DAC_STOP_DECAY 0
#elif AUDIO_DAC == DAC_UDA1334A
#define BOARD_DAC_NAME "UDA1334A"
/* No internal mute on a lost clock: disabling the channel outright drops up
 * to 92.9 ms of queued music mid-waveform and takes BCLK with it, and the
 * step that leaves on the output is the click two users heard on every
 * station and track change. So the stop decays the output to zero and waits
 * for the DMA to clock it out before the channel goes down. */
#define AUDIO_DAC_STOP_DECAY 1
#else
#define BOARD_DAC_NAME "SSM3582"
/* The output stage lives in the same chip as the volume control, and it is
 * closed - soft-ramped, click-free by its own design - before the clock is
 * ever allowed to stop; see ssm3582_set_playing() where board_amp_drive()
 * decides an amplifier has nothing to amplify. What still must not drop
 * mid-waveform is the queue behind that: the chip may only power down over
 * silence, so the stop decays the tail and lets the DMA clock it out
 * first, as it does for the UDA1334A. */
#define AUDIO_DAC_STOP_DECAY 1
#endif

#else
#error "unsupported AUDIO_DAC - see board_parts.h for the parts with drivers"
#endif
