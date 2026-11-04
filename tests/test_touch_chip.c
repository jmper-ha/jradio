#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "touch_chip.h"

static void test_decode(void)
{
    touch_chip_point_t point = {0, 0};
    const uint8_t pressed[TOUCH_CHIP_REPORT_REGS] = {0x01, 0x01, 0x23, 0x01, 0xC8};
    assert(touch_chip_decode(pressed, &point));
    assert(point.x == 0x123 && point.y == 0x1C8);

    // Contact, the event a held finger reports, and a second finger beside it.
    const uint8_t held[TOUCH_CHIP_REPORT_REGS] = {0x02, 0x80, 0x10, 0x00, 0x20};
    assert(touch_chip_decode(held, &point));
    assert(point.x == 0x10 && point.y == 0x20);

    const uint8_t none[TOUCH_CHIP_REPORT_REGS] = {0x00, 0x40, 0x10, 0x00, 0x20};
    assert(!touch_chip_decode(none, &point));
    const uint8_t unscanned[TOUCH_CHIP_REPORT_REGS] = {0x0F, 0xFF, 0xFF, 0xFF, 0xFF};
    assert(!touch_chip_decode(unscanned, &point));
    const uint8_t lifted[TOUCH_CHIP_REPORT_REGS] = {0x01, 0x40, 0x10, 0x00, 0x20};
    assert(!touch_chip_decode(lifted, &point));
    const uint8_t no_event[TOUCH_CHIP_REPORT_REGS] = {0x01, 0xC0, 0x10, 0x00, 0x20};
    assert(!touch_chip_decode(no_event, &point));
}

static void assert_placed(touch_chip_point_t screen, uint16_t x, uint16_t y)
{
    assert(screen.x == x);
    assert(screen.y == y);
}

// The four corners as the bench's ST7796S reported them, MX and MY both clear.
static void test_landscape_measured(void)
{
    const uint16_t w = 480U;
    const uint16_t h = 320U;
    assert_placed(touch_chip_place((touch_chip_point_t){291, 26}, w, h, true, false, false), 26, 28);
    assert_placed(touch_chip_place((touch_chip_point_t){292, 461}, w, h, true, false, false), 461, 27);
    assert_placed(touch_chip_place((touch_chip_point_t){9, 449}, w, h, true, false, false), 449, 310);
    assert_placed(touch_chip_place((touch_chip_point_t){10, 30}, w, h, true, false, false), 30, 309);
}

static void test_mirrors_follow_the_picture(void)
{
    const touch_chip_point_t chip = {10, 30};
    assert_placed(touch_chip_place(chip, 480U, 320U, true, true, false), 30, 10);
    assert_placed(touch_chip_place(chip, 480U, 320U, true, false, true), 449, 309);
    assert_placed(touch_chip_place(chip, 480U, 320U, true, true, true), 449, 10);
    assert_placed(touch_chip_place(chip, 320U, 480U, false, false, false), 309, 30);
    assert_placed(touch_chip_place(chip, 320U, 480U, false, true, false), 10, 30);
    assert_placed(touch_chip_place(chip, 320U, 480U, false, false, true), 309, 449);
    assert_placed(touch_chip_place(chip, 320U, 480U, false, true, true), 10, 449);
}

static void test_off_the_edge_stays_on_screen(void)
{
    const touch_chip_point_t chip = {4000, 4000};
    assert_placed(touch_chip_place(chip, 480U, 320U, true, false, false), 479, 0);
    assert_placed(touch_chip_place(chip, 320U, 480U, false, false, false), 0, 479);
}

int main(void)
{
    test_decode();
    test_landscape_measured();
    test_mirrors_follow_the_picture();
    test_off_the_edge_stays_on_screen();
    puts("touch_chip tests passed");
    return 0;
}
