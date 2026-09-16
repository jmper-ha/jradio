#include "board_amplifier.h"

#include <assert.h>
#include <stdio.h>

static void test_the_amplifier_follows_this_chip_s_output(void)
{
    assert(board_amplifier_should_play(false, false, false) == false);
    assert(board_amplifier_should_play(true, false, false) == true);
}

/* The case that is silent when it is wrong: in Bluetooth sink mode this chip
 * has given the pins away and its own output is disabled, but the module is
 * clocking the same DAC and the speakers still have to work. */
static void test_the_module_playing_counts_as_playing(void)
{
    assert(board_amplifier_should_play(false, true, false) == true);
}

/* The DAC's mute is how sound is sent to a Bluetooth speaker instead of the
 * board's own; the amplifier then has nothing to amplify either way, and
 * leaving it awake only adds its hiss to a quiet room. */
static void test_a_muted_dac_mutes_the_amplifier(void)
{
    assert(board_amplifier_should_play(true, false, true) == false);
    assert(board_amplifier_should_play(false, true, true) == false);
}

int main(void)
{
    test_the_amplifier_follows_this_chip_s_output();
    test_the_module_playing_counts_as_playing();
    test_a_muted_dac_mutes_the_amplifier();
    puts("board_amplifier tests passed");
    return 0;
}
