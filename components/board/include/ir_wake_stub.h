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

/* Points the chip at the stub for the next wake and clears what it kept. */
void ir_wake_stub_arm(void);

/* The frame the stub read on this wake, decoded like any other; false when
 * this boot was not a wake, the wake was not the receiver's, or the stub
 * arrived too late for a whole frame. Reading it consumes it. */
bool ir_wake_stub_take(ir_code_t *code);
