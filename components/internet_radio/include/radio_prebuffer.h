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
