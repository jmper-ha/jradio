#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* The SSM3582 on I2C0, for a board whose board.csv names it as the DAC. The
 * register words are ssm3582.h's; this is the bus, the order and the lock.
 *
 * The chip is told the format and the gain once, at boot, and is then only
 * powered up and down - the mute - and moved between rate bands. Taken
 * from PR #3 (nikkov), which brought the part up on a ChipDip RDC2-0059
 * board: the transfers, the retries and the identity check are its; the
 * values are the datasheet's. */

/* Opens the chip at `address`, checks it is one, resets it and writes the
 * format, `gain_code` (ANA_GAIN, 0..3) and the rate band for `sample_rate`,
 * leaving it powered down until the first sound. An error leaves the board
 * playing into a chip that was never set up - silence - and says so in the
 * log; the rest of the firmware runs on. */
esp_err_t board_ssm3582_init(uint8_t address, uint8_t gain_code, uint32_t sample_rate);

// Whether init found the chip and set it up.
bool board_ssm3582_present(void);

/* SPWDN: down before the bit clock stops, up after it starts - the
 * datasheet's click-free order. */
void board_ssm3582_set_powered(bool powered);

/* The rate band for a new rate, while the clock is stopped. A rate below
 * 32 kHz is written all the same, with a warning: the chip wants 100 x Fs or
 * more there, which the S3's standard mode does not make. */
void board_ssm3582_set_rate(uint32_t sample_rate);
