#include "touch_chip.h"

#define TOUCH_CHIP_POINTS_MAX 2U
#define TOUCH_CHIP_EVENT_LIFT 1U
#define TOUCH_CHIP_EVENT_NONE 3U

bool touch_chip_decode(const uint8_t regs[TOUCH_CHIP_REPORT_REGS], touch_chip_point_t *point)
{
    const unsigned count = regs[0] & 0x0FU;
    if (count == 0U || count > TOUCH_CHIP_POINTS_MAX) return false;
    const unsigned event = regs[1] >> 6;
    if (event == TOUCH_CHIP_EVENT_LIFT || event == TOUCH_CHIP_EVENT_NONE) return false;
    point->x = (uint16_t)(((regs[1] & 0x0FU) << 8) | regs[2]);
    point->y = (uint16_t)(((regs[3] & 0x0FU) << 8) | regs[4]);
    return true;
}

static uint16_t clamped(uint16_t value, uint16_t size)
{
    return value < size ? value : (uint16_t)(size - 1U);
}

touch_chip_point_t touch_chip_place(touch_chip_point_t chip, uint16_t width, uint16_t height,
                                    bool swap_xy, bool mirror_x, bool mirror_y)
{
    /* The glass, in the panel's native upright: its x runs against the chip's
     * and its y with it. Read off the four corners of an ST7796S module on
     * 2026-11-01; another module may have its touch layer the other way. */
    const uint16_t native_width = swap_xy ? height : width;
    const uint16_t native_height = swap_xy ? width : height;
    const uint16_t glass_x = (uint16_t)(native_width - 1U - clamped(chip.x, native_width));
    const uint16_t glass_y = clamped(chip.y, native_height);

    touch_chip_point_t screen = {swap_xy ? glass_y : glass_x, swap_xy ? glass_x : glass_y};
    /* With the axes swapped the panel's MX turns the picture top to bottom and
     * its MY left to right: a weather icon held under a finger through each
     * flip on 2026-11-01 said so, whatever the switches are called. */
    if (swap_xy ? mirror_y : mirror_x) screen.x = (uint16_t)(width - 1U - screen.x);
    if (swap_xy ? mirror_x : mirror_y) screen.y = (uint16_t)(height - 1U - screen.y);
    return screen;
}
