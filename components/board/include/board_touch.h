#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* The touch panel the wiring names (`touch` in board.csv, TOUCH_CHIP in
 * board_options.h): an FT6336U on I2C0, beside the tuner and the clock. */

typedef struct {
    uint16_t x;
    uint16_t y;
} board_touch_point_t;

/* Finds the chip and sets it up. Not finding it costs the touch and nothing
 * else; board_touch_read() then says no finger, always. */
esp_err_t board_touch_init(void);

/* True with the point in screen pixels while a finger is down. For one reader:
 * it keeps the last answer, and hands it back when the chip misses a read, so
 * a spoilt transfer in the middle of a press does not read as a lift.
 *
 * With the interrupt line wired, a screen nobody touches costs a pin read and
 * no bus traffic. */
bool board_touch_read(board_touch_point_t *point);

/* The display's two effective mirrors, from board_display_set_rotation(): the
 * picture moved, so a point on it has to move too. */
void board_touch_set_mirror(bool mirror_x, bool mirror_y);
