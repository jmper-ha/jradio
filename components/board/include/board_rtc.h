#pragma once

#include <stdbool.h>

/* The real-time clock the wiring names (`rtc` in board.csv, RTC_CHIP in
 * board_options.h): a DS3231 or a DS1307 on I2C0, keeping the time on its
 * coin cell while the board is off.
 *
 * Nothing here decides anything about the time: the chip is read once at boot
 * into the system clock, and written from it after every time-server answer.
 * Between those the system clock is the one every reader uses - see
 * device_clock.h - so a board without the chip behaves exactly as before. */

/* Sets the system clock from the chip. True when it did; false - and a line in
 * the log saying why - when there is no chip in the wiring, it does not answer
 * (unpowered: on a board whose peripheral rail feeds it, before board_init()
 * raises the rail), or it holds no time worth having: never set, its
 * oscillator stopped (a flat cell), or a year before 2020. Safe to call more
 * than once; the later call wins, which is the point of calling it again
 * after the rail is up. */
bool board_rtc_restore(void);

/* Writes the system clock into the chip, if there is one and the system clock
 * has been set. Meant to be called on each time-server answer
 * (device_clock_on_sync); quiet when there is nothing to do. */
void board_rtc_store(void);
