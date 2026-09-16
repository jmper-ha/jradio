#include "board_amplifier.h"

bool board_amplifier_should_play(bool i2s_output_enabled, bool bus_released_to_module,
                                 bool dac_muted)
{
    if (dac_muted) return false;
    return i2s_output_enabled || bus_released_to_module;
}
