#pragma once

#include <stdbool.h>

/* Whether the amplifier should be passing sound, from the three things that
 * decide it. Its own file because getting it wrong is silent - an amplifier
 * muted while a phone plays over Bluetooth is a device that looks like it is
 * working - and because none of it needs the chip.
 *
 * `i2s_output_enabled` - this chip is clocking the DAC;
 * `bus_released_to_module` - the I2S pins are in the Bluetooth module's hands,
 *   so the sound comes from it instead and the amplifier is needed just the
 *   same;
 * `dac_muted` - the DAC's own mute is asserted, which is how the sound is sent
 *   to a Bluetooth speaker rather than to the speakers on this board. */
bool board_amplifier_should_play(bool i2s_output_enabled, bool bus_released_to_module,
                                 bool dac_muted);
