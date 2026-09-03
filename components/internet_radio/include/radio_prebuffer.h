#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decides how much to read from the network on each pass of the decode loop.
 *
 * The problem this solves: the input buffer is 64 KB, but measurements showed
 * `min_backlog` sitting at 1-32 bytes in *every* 10 s window, on a healthy
 * 128 kbps stream with the link delivering far above real time. The buffer
 * never filled, so the real tolerance to a network hiccup was the ~93 ms of
 * I2S DMA rather than the seconds the buffer implies - which is what produced
 * the occasional 80-180 ms dropouts.
 *
 * The cause was the shape of the loop, not the sizes. Reading was driven by
 * `need_input`, which is only true once the decoder has drained the buffer to
 * empty, so the backlog was refilled from zero rather than kept topped up. The
 * opportunistic top-up that was supposed to build a cushion was capped at two
 * chunks per pass and only ran on that same empty-buffer path, so it could
 * never get ahead.
 *
 * The fix is to read whenever the backlog is below target rather than only
 * when it is empty, and to bound each pass by *time* rather than by a chunk
 * count: what must not happen is spending so long reading that the DMA drains,
 * and that is a duration, not a number of bytes. A pass may read a lot while
 * the buffer is nearly empty (there is nothing to lose) and little once it is
 * comfortable. */

/* The most backlog the loop will ever chase, whatever the buffer's size.
 *
 * The target is a cushion, not a store, and it was written as half the buffer
 * back when the buffer was 64 KB. When the buffer grew to 256 KB the target
 * silently became 128 KB - eight seconds of an ordinary station - so the loop
 * spent the first seconds of every station reading hard, which is exactly the
 * moment the output has just opened and has nothing behind it but the ~93 ms
 * of I2S DMA. Measured on the same station across builds: 1-3 dropped DMA
 * buffers on the start before the buffer grew, 18-24 after, and 0-2 with this
 * cap in place. Stations whose backlog is limited by the server, which is
 * every high-bitrate one, never approach either number and are unaffected.
 *
 * A target measured in *time* was tried on 2026-09-03 and measured worse -
 * do not try it again without reading this. The reasoning was sound on paper:
 * 32 KB is 2.0 s of a 128 kbps station and 0.18 s of a 1442 kbps FLAC one, so
 * the stream with the least margin gets the smallest cushion. Asking for 2 s
 * everywhere (capped at three quarters of the buffer, 196 KB, 1.09 s of FLAC)
 * gave this, on the same station, same procedure, ten-second windows:
 *
 *   target 32 KB:  min_backlog 79-90 ms,  i2s_underruns 0 in every window
 *   target 196 KB: min_backlog 6-34 ms,   i2s_underruns 4,12,15,9,1,5,9
 *
 * Because the 79 ms is the *server's* pacing, not our target's doing. Radio
 * Paradise hands out about 14 KB of headroom and then sends at real time, so
 * a backlog below target can never reach it, and the top-up loop below spends
 * every pass waiting on a socket that has nothing yet - time the decoder
 * needed. A cushion can only be built from a server that is ahead of real
 * time; against one that is not, asking for more buffer costs playback. */
#define RADIO_PREBUFFER_TARGET_MAX 32768U

typedef struct {
    size_t capacity;
    size_t target;
    /* Below this the situation is urgent: the DMA is closer to running dry
     * than the buffer is to being useful, so a pass may read for longer. */
    size_t critical;
    uint32_t normal_budget_ms;
    uint32_t urgent_budget_ms;
} radio_prebuffer_config_t;

typedef struct {
    bool should_read;
    /* 0 means "do not block": poll the socket and take only what has already
     * arrived, so a comfortable backlog never stalls the decoder. */
    uint32_t budget_ms;
    size_t max_bytes;
} radio_prebuffer_plan_t;

void radio_prebuffer_config_init(radio_prebuffer_config_t *config, size_t capacity,
                                 size_t chunk);

/* `available` is the backlog the decoder still has; `need_input` is true when
 * it cannot proceed without more. */
radio_prebuffer_plan_t radio_prebuffer_plan(const radio_prebuffer_config_t *config,
                                            size_t available, bool need_input);

/* Milliseconds of audio a backlog represents, for logging. 0 when the bitrate
 * is unknown, which is normal before the first frame is decoded. */
uint32_t radio_prebuffer_millis(size_t available, uint32_t bitrate_kbps);

/* How full the input buffer is, 0..100, for display.
 *
 * Pass the level the source aims to hold - `config->target` for the radio, the
 * whole buffer for a file, which refills to the brim - so that 100 means "as
 * much in hand as this source ever keeps" whichever source is playing.
 *
 * It was read against the radio's *buffer* until 2026-09-03, and the user
 * reported the result: every station displayed 13% and nothing ever moved it.
 * The loop stops reading at the target, the target is capped at 32 KB and the
 * buffer is 256 KB, so 12.5% was the ceiling of a scale that ran to 100. The
 * number still carries what it is for - a dropout is the number falling - but
 * it now has the whole scale to fall through, and it separates the stations
 * the old one could not: a 128 kbps station holds its full 32 KB and reads
 * 100, while a FLAC station the server paces at real time holds 14 KB of it
 * and reads 44.
 *
 * RADIO_PREBUFFER_PERCENT_NONE means the source has no input backlog at all,
 * which is not the same as an empty one: the WAV path reads a chunk straight
 * into I2S and never holds a buffer, so reporting 0 would look like a fault.
 *
 * Rounds to nearest but keeps both ends exact: anything held at all shows at
 * least 1, so an almost-empty buffer is not displayed as an empty one. */
#define RADIO_PREBUFFER_PERCENT_NONE 0xFFU
uint8_t radio_prebuffer_percent(size_t available, size_t capacity);
