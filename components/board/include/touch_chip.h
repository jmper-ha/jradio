#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The FT6336U's touch report and where it lands on the screen. Pure, so both
 * are tested on the host; board_touch.c does the talking. */

#define TOUCH_CHIP_I2C_ADDRESS 0x38U
#define TOUCH_CHIP_REG_REPORT 0x02U
#define TOUCH_CHIP_REG_MODE 0xA4U
#define TOUCH_CHIP_REG_ID 0xA3U
#define TOUCH_CHIP_ID_FT6336U 0x64U
/* Interrupt line held low for as long as a finger is down, read as a level.
 * The chip comes up in its other mode, a pulse per report, which would take an
 * interrupt handler to catch. Seen on the bench 2026-11-01. */
#define TOUCH_CHIP_MODE_LEVEL 0x00U
/* TD_STATUS, then the first point's XH, XL, YH, YL. */
#define TOUCH_CHIP_REPORT_REGS 5U

typedef struct {
    uint16_t x;
    uint16_t y;
} touch_chip_point_t;

/* True with the point in the chip's own axes while a finger is down. False for
 * no points, for a count the chip cannot have (it reads 0x0F before its first
 * scan), and for a point whose event says the finger has lifted. */
bool touch_chip_decode(const uint8_t regs[TOUCH_CHIP_REPORT_REGS], touch_chip_point_t *point);

/* The chip's point in the screen's pixels: `width` by `height` as drawn, with
 * the display's swap and its two effective mirrors, the same three the panel
 * was given. Off the edge is pulled back to the edge - the glass reports a
 * little past what the panel shows. */
touch_chip_point_t touch_chip_place(touch_chip_point_t chip, uint16_t width, uint16_t height,
                                    bool swap_xy, bool mirror_x, bool mirror_y);
