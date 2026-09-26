/* Prints board_config_to_csv(board_config_compiled()) - what the firmware
 * falls back to - so tests/run_host_tests.sh can hold tools/board_bin.py,
 * which writes the same text for the build's board.bin, to it. */
#include <stdio.h>

#include "board_config.h"

int main(void)
{
    board_config_t config;
    board_config_compiled(&config);
    static char text[4096];
    if (board_config_to_csv(&config, text, sizeof(text)) >= sizeof(text)) return 1;
    fputs(text, stdout);
    return 0;
}
