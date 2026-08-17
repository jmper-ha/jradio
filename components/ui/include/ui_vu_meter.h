#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Level meter ballistics and scale.
 *
 * Two things a naive meter gets wrong, both handled here.
 *
 * The scale is not linear in sample value. Hearing is roughly logarithmic, so
 * a linear bar spends most of its travel in the top few dB and sits nearly
 * dead for everything quiet - real music would hover around a tenth of the
 * bar and look broken. The table below approximates dB without pulling in
 * libm, which keeps this testable on the host and floating point off the
 * device.
 *
 * And a meter that simply follows the signal flickers uselessly at 44 kHz. It
 * needs a fast attack, so a swell registers at full height, and a slower
 * release, so the eye can read the level it reached.
 */

/* Full scale is a 16-bit sample at its limit. */
#define UI_VU_FULL_SCALE 32768U
/* Percent per second the bar falls when the signal drops. About two thirds of
 * a second from top to bottom, near classic VU ballistics - slow enough to
 * read, fast enough to follow the music down instead of hanging above it. */
#define UI_VU_RELEASE_PERCENT_PER_SEC 160U

typedef struct {
    uint8_t percent;
} ui_vu_meter_t;

void ui_vu_meter_init(ui_vu_meter_t *meter);

/* Maps an RMS magnitude to 0..100 on an approximate dB scale, topping out at
 * -6 dBFS. Silence is 0; anything below about -54 dB is also 0, since a bar
 * that never quite empties reads as a stuck meter.
 *
 * RMS and not peak on purpose: mastered music holds its peaks within a couple
 * of dB of full scale continuously, so a peak-fed bar sits pinned at the top
 * and shows nothing at all. */
uint8_t ui_vu_percent_from_level(uint16_t level);

/* Advances the meter by `elapsed_ms` towards `target_percent`. Rises to the
 * target at once and falls gradually, returning the value to display. */
uint8_t ui_vu_meter_step(ui_vu_meter_t *meter, uint8_t target_percent,
                         uint32_t elapsed_ms);

/* How many of `count` segments a level lights. Rounds to nearest so a signal
 * sitting between two segments does not always read low, but keeps the two
 * ends exact: silence lights nothing and full scale lights everything, which
 * are the readings a listener actually checks. */
uint8_t ui_vu_lit_segments(uint8_t percent, uint8_t count);

/* True for segments in the red zone at the top of the scale. */
bool ui_vu_segment_is_red(uint8_t index, uint8_t count);
