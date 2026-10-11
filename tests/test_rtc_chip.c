#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board_parts.h"
#include "rtc_chip.h"

static struct tm moment(int year, int month, int day, int hour, int minute, int second)
{
    struct tm utc = {0};
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    utc.tm_sec = second;
    // The weekday the caller would have from gmtime_r.
    utc.tm_wday = (int)((rtc_chip_epoch(&utc) / 86400 + 4) % 7);
    return utc;
}

static void test_epoch_matches_known_instants(void)
{
    struct tm utc = moment(1970, 1, 1, 0, 0, 0);
    assert(rtc_chip_epoch(&utc) == 0);
    utc = moment(2000, 3, 1, 0, 0, 0);
    assert(rtc_chip_epoch(&utc) == 951868800);
    utc = moment(2026, 10, 11, 12, 34, 56);
    assert(rtc_chip_epoch(&utc) == 1791722096);
    utc = moment(2099, 12, 31, 23, 59, 59);
    assert(rtc_chip_epoch(&utc) == 4102444799LL);
    // 2026-10-11 is a Sunday.
    assert(moment(2026, 10, 11, 0, 0, 0).tm_wday == 0);
}

static void test_registers_are_bcd_in_24_hours(void)
{
    const struct tm utc = moment(2026, 10, 11, 21, 7, 45);
    uint8_t regs[RTC_CHIP_TIME_REGS];
    assert(rtc_chip_encode(RTC_CHIP_DS3231, &utc, regs));
    const uint8_t expected[RTC_CHIP_TIME_REGS] = {0x45, 0x07, 0x21, 0x01, 0x11, 0x10, 0x26};
    assert(memcmp(regs, expected, sizeof(expected)) == 0);
    // The DS1307 the same, with its clock-halt bit clear: running.
    assert(rtc_chip_encode(RTC_CHIP_DS1307, &utc, regs));
    assert(memcmp(regs, expected, sizeof(expected)) == 0);
}

static void test_round_trip_both_chips(void)
{
    const struct tm cases[] = {
        moment(2020, 1, 1, 0, 0, 0),   moment(2024, 2, 29, 23, 59, 59),
        moment(2026, 10, 11, 9, 5, 7), moment(2099, 12, 31, 23, 59, 59),
    };
    const unsigned chips[] = {RTC_CHIP_DS3231, RTC_CHIP_DS1307};
    for (size_t c = 0; c < sizeof(chips) / sizeof(chips[0]); ++c) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            uint8_t regs[RTC_CHIP_TIME_REGS];
            struct tm back;
            assert(rtc_chip_encode(chips[c], &cases[i], regs));
            assert(rtc_chip_decode(chips[c], regs, &back));
            assert(rtc_chip_epoch(&back) == rtc_chip_epoch(&cases[i]));
            assert(back.tm_wday == cases[i].tm_wday);
        }
    }
}

static void test_encode_refuses_what_the_chip_cannot_hold(void)
{
    uint8_t regs[RTC_CHIP_TIME_REGS];
    struct tm utc = moment(1999, 12, 31, 23, 59, 59);
    assert(!rtc_chip_encode(RTC_CHIP_DS3231, &utc, regs));
    utc = moment(2100, 1, 1, 0, 0, 0);
    assert(!rtc_chip_encode(RTC_CHIP_DS3231, &utc, regs));
    utc = moment(2026, 10, 11, 0, 0, 0);
    assert(!rtc_chip_encode(RTC_CHIP_NONE, &utc, regs));
    assert(!rtc_chip_encode(RTC_CHIP_DS3231, NULL, regs));
    // A leap second is written as the second before it.
    utc = moment(2026, 12, 31, 23, 59, 59);
    utc.tm_sec = 60;
    assert(rtc_chip_encode(RTC_CHIP_DS3231, &utc, regs) && regs[0] == 0x59);
}

static void test_decode_refuses_no_time(void)
{
    struct tm utc;
    // A DS1307 out of the bag: clock halted, and 2000-01-01 besides.
    const uint8_t fresh_1307[RTC_CHIP_TIME_REGS] = {0x80, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00};
    assert(!rtc_chip_decode(RTC_CHIP_DS1307, fresh_1307, &utc));
    // A halted DS1307 is not trusted even with a good date in it.
    const uint8_t halted[RTC_CHIP_TIME_REGS] = {0x80 | 0x12, 0x30, 0x10, 0x01, 0x11, 0x10, 0x26};
    assert(!rtc_chip_decode(RTC_CHIP_DS1307, halted, &utc));
    // The same bit means nothing on a DS3231, whose seconds never set it.
    const uint8_t ds3231_power_on[RTC_CHIP_TIME_REGS] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00};
    assert(!rtc_chip_decode(RTC_CHIP_DS3231, ds3231_power_on, &utc));  // year 2000
    // A bus that read 0xFF.
    const uint8_t ones[RTC_CHIP_TIME_REGS] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    assert(!rtc_chip_decode(RTC_CHIP_DS3231, ones, &utc));
    // 12-hour mode, set by something else.
    const uint8_t twelve[RTC_CHIP_TIME_REGS] = {0x00, 0x00, 0x40 | 0x09, 0x01, 0x11, 0x10, 0x26};
    assert(!rtc_chip_decode(RTC_CHIP_DS3231, twelve, &utc));
    // February the 30th.
    const uint8_t feb30[RTC_CHIP_TIME_REGS] = {0x00, 0x00, 0x00, 0x01, 0x30, 0x02, 0x26};
    assert(!rtc_chip_decode(RTC_CHIP_DS3231, feb30, &utc));
    // 2025 is no leap year; 2024 is.
    const uint8_t feb29_2025[RTC_CHIP_TIME_REGS] = {0x00, 0x00, 0x00, 0x01, 0x29, 0x02, 0x25};
    assert(!rtc_chip_decode(RTC_CHIP_DS3231, feb29_2025, &utc));
    const uint8_t feb29_2024[RTC_CHIP_TIME_REGS] = {0x00, 0x00, 0x00, 0x01, 0x29, 0x02, 0x24};
    assert(rtc_chip_decode(RTC_CHIP_DS3231, feb29_2024, &utc));
    assert(!rtc_chip_decode(RTC_CHIP_NONE, feb29_2024, &utc));
}

static void test_ds3231_century_bit(void)
{
    struct tm utc;
    const uint8_t century[RTC_CHIP_TIME_REGS] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x80 | 0x01, 0x05};
    assert(rtc_chip_decode(RTC_CHIP_DS3231, century, &utc));
    assert(utc.tm_year + 1900 == 2105);
}

int main(void)
{
    test_epoch_matches_known_instants();
    test_registers_are_bcd_in_24_hours();
    test_round_trip_both_chips();
    test_encode_refuses_what_the_chip_cannot_hold();
    test_decode_refuses_no_time();
    test_ds3231_century_bit();
    printf("rtc chip tests passed\n");
    return 0;
}
