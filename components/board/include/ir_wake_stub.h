#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ir_decode.h"

/* Waking on the first press.
 *
 * The receiver's pin wakes the chip on the first mark of a frame, and the
 * boot that follows takes long enough that the frame is over before the
 * receiver is listening again: without this, the key that actually woke the
 * board was always the second press. The ESP32-S3 runs a wake stub out of
 * RTC memory within a millisecond or two of the wake - still inside NEC's
 * 9 ms leader mark - so the stub reads the rest of the frame straight off
 * the pin, bit by bit, and leaves the 32 bits in RTC memory for the boot to
 * find. Arm it before every deep sleep; ask for the frame at boot. */

/* Points the chip at the stub for the next wake and clears what it kept.
 *
 * `power_bits`, when not NULL, is the NEC frame of the remote's Power key
 * (ir_code_nec_frame). With it the stub can tell a wake worth a boot from one
 * that is not, and goes back to sleep on the second kind without one: noise
 * on the receiver - which on the bench woke the board every twenty seconds
 * through a sleep - or another key. NULL - Power not learned, or learned off
 * a remote that is not NEC, which the stub cannot read - boots on every wake
 * as before and leaves it to remote_control_wake_check(). `timer` says the
 * sleep has the alarm's timer as well, which a sleep resumed from the stub
 * has to keep. */
void ir_wake_stub_arm(const uint32_t *power_bits, bool timer);

/* How many wakes the stub slept through since this was last asked, for the
 * log of the boot that finally came. Reading it resets it. */
uint32_t ir_wake_stub_slept_through(void);

/* The frame the stub read on this wake, decoded like any other; false when
 * this boot was not a wake, the wake was not the receiver's, or the stub
 * arrived too late for a whole frame. Reading it consumes it. */
bool ir_wake_stub_take(ir_code_t *code);
