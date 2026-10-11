#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* The time registers of a DS3231 or DS1307 real-time clock, both ways.
 *
 * The two chips keep the time the same way - seven BCD registers from 0x00:
 * seconds, minutes, hours, weekday, date, month, year - and differ in the
 * edges: the DS1307 stops its oscillator while bit 7 of the seconds is set
 * (CH, "clock halt"), which is how a fresh one comes out of the bag; the
 * DS3231 has no such bit, keeps a century bit on the month instead, and says
 * in its status register (OSF) when its oscillator has stopped since that was
 * last cleared - a flat battery, say - and its time is not to be trusted.
 *
 * The time kept is UTC. A zone change is then the clock's business alone, and
 * a radio carried across a border, or a zone set wrong and put right, never
 * has to rewrite the chip. Pure, and tested on the host. */

/* Both chips answer at the same address, so a board has one or the other. */
#define RTC_CHIP_I2C_ADDRESS 0x68U
#define RTC_CHIP_TIME_REGS 7U
// DS3231 only: the status register and its oscillator-stop flag.
#define RTC_CHIP_DS3231_STATUS 0x0FU
#define RTC_CHIP_DS3231_OSF 0x80U
/* The control register, and its oscillator-off-on-battery bit: set, the
 * chip stops counting the moment it is left on its cell - exactly the board
 * being switched off, which is what the cell is for. Clear from the factory,
 * but a board that had the chip before may have set it. */
#define RTC_CHIP_DS3231_CONTROL 0x0EU
#define RTC_CHIP_DS3231_EOSC 0x80U

/* `chip` is RTC_CHIP_DS3231 or RTC_CHIP_DS1307 (board_parts.h). False for a
 * time the chip cannot hold - before 2000 or past 2099 - or a chip it does not
 * know. The DS1307's clock-halt bit is written clear, which starts a halted
 * oscillator. */
bool rtc_chip_encode(unsigned chip, const struct tm *utc, uint8_t regs[RTC_CHIP_TIME_REGS]);

/* The registers back as a UTC time (tm_isdst 0, tm_yday unset). False when
 * they hold no time worth setting the clock from: a halted DS1307, a field out
 * of its range or not BCD at all, or a year before 2020 - the chip's power-on
 * 2000-01-01 is a time, but not this one, and the clock reads unset rather
 * than twenty-odd years out. */
bool rtc_chip_decode(unsigned chip, const uint8_t regs[RTC_CHIP_TIME_REGS], struct tm *utc);

/* Seconds since the epoch for a UTC broken-down time, which mktime() cannot
 * give - it takes the local zone - and newlib has no timegm(). */
int64_t rtc_chip_epoch(const struct tm *utc);
