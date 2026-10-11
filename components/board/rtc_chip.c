#include "rtc_chip.h"

#include "board_parts.h"

#define DS1307_CLOCK_HALT 0x80U
#define HOURS_12H 0x40U
#define DS3231_CENTURY 0x80U
// The oldest year a register may hold and still be taken for the time now.
#define RTC_CHIP_YEAR_MIN 2020

static uint8_t to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

/* -1 for a byte that is not two decimal digits: a chip that has never been
 * set, or a bus read gone wrong, is as likely to give 0xFF as anything. */
static int from_bcd(uint8_t value)
{
    const int high = value >> 4;
    const int low = value & 0x0F;
    if (high > 9 || low > 9) return -1;
    return high * 10 + low;
}

static bool known(unsigned chip)
{
    return chip == RTC_CHIP_DS3231 || chip == RTC_CHIP_DS1307;
}

bool rtc_chip_encode(unsigned chip, const struct tm *utc, uint8_t regs[RTC_CHIP_TIME_REGS])
{
    if (!known(chip) || utc == NULL || regs == NULL) return false;
    const int year = utc->tm_year + 1900;
    if (year < 2000 || year > 2099) return false;
    if (utc->tm_sec < 0 || utc->tm_sec > 60 || utc->tm_min < 0 || utc->tm_min > 59 ||
        utc->tm_hour < 0 || utc->tm_hour > 23 || utc->tm_mday < 1 || utc->tm_mday > 31 ||
        utc->tm_mon < 0 || utc->tm_mon > 11 || utc->tm_wday < 0 || utc->tm_wday > 6) {
        return false;
    }
    // A leap second has no register value; the next write puts it right.
    regs[0] = to_bcd(utc->tm_sec > 59 ? 59 : utc->tm_sec);  // CH clear: running
    regs[1] = to_bcd(utc->tm_min);
    regs[2] = to_bcd(utc->tm_hour);                          // 24-hour mode
    regs[3] = (uint8_t)(utc->tm_wday + 1);                   // 1..7, Sunday first
    regs[4] = to_bcd(utc->tm_mday);
    regs[5] = to_bcd(utc->tm_mon + 1);                       // century bit clear: 20xx
    regs[6] = to_bcd(year - 2000);
    return true;
}

static int days_in_month(int year, int month)
{
    static const int k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : k_days[month - 1];
}

bool rtc_chip_decode(unsigned chip, const uint8_t regs[RTC_CHIP_TIME_REGS], struct tm *utc)
{
    if (!known(chip) || regs == NULL || utc == NULL) return false;
    if (chip == RTC_CHIP_DS1307 && (regs[0] & DS1307_CLOCK_HALT) != 0U) return false;
    /* The time is only ever written in 24-hour mode here. A chip set in 12-hour
     * mode by something else is not worth a second decoder: the first sync
     * writes it back in 24. */
    if ((regs[2] & HOURS_12H) != 0U) return false;
    const int second = from_bcd(regs[0] & 0x7FU);
    const int minute = from_bcd(regs[1] & 0x7FU);
    const int hour = from_bcd(regs[2] & 0x3FU);
    const int weekday = regs[3] & 0x07U;
    const int day = from_bcd(regs[4] & 0x3FU);
    const int month = from_bcd(regs[5] & 0x1FU);
    const int yy = from_bcd(regs[6]);
    if (second < 0 || second > 59 || minute < 0 || minute > 59 || hour < 0 || hour > 23 ||
        month < 1 || month > 12 || yy < 0) {
        return false;
    }
    const int year = 2000 + yy +
                     (chip == RTC_CHIP_DS3231 && (regs[5] & DS3231_CENTURY) != 0U ? 100 : 0);
    if (year < RTC_CHIP_YEAR_MIN || day < 1 || day > days_in_month(year, month)) return false;
    (void)weekday;  // recomputed below rather than trusted: nothing else reads it

    *utc = (struct tm){0};
    utc->tm_sec = second;
    utc->tm_min = minute;
    utc->tm_hour = hour;
    utc->tm_mday = day;
    utc->tm_mon = month - 1;
    utc->tm_year = year - 1900;
    // 1970-01-01 was a Thursday.
    const int64_t days = rtc_chip_epoch(utc) / 86400;
    utc->tm_wday = (int)((days + 4) % 7);
    return true;
}

/* Howard Hinnant's days_from_civil: whole days from 1970-01-01, for any
 * Gregorian date, without a table or a loop. */
int64_t rtc_chip_epoch(const struct tm *utc)
{
    if (utc == NULL) return 0;
    int64_t year = (int64_t)utc->tm_year + 1900;
    const int64_t month = (int64_t)utc->tm_mon + 1;
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t year_of_era = year - era * 400;
    const int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + utc->tm_mday - 1;
    const int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const int64_t days = era * 146097 + day_of_era - 719468;
    return days * 86400 + (int64_t)utc->tm_hour * 3600 + (int64_t)utc->tm_min * 60 + utc->tm_sec;
}
