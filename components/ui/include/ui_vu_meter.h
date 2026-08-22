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
 * And a meter that simply follows the signal flickers uselessly. It needs a
 * fast rise, so a swell registers, and a slower fall, so the eye can read the
 * level it reached - see the time constants below.
 */

/* Full scale is a 16-bit sample at its limit. */
#define UI_VU_FULL_SCALE 32768U
/* Ballistics, as time constants: each pass the bar closes `elapsed / constant`
 * of the distance to the reading, so movement is proportional to the gap and
 * the same however often the loop runs.
 *
 * Both numbers exist because of what the display is fed. A reading arrives
 * once per decoded block - 23 ms of MP3, 93 ms of FLAC - and they vary about
 * 14% block to block - a real property of the music, not noise - so a meter
 * that simply took each one flickered too fast to read. It has to show the
 * envelope, which means it needs a rise time as well as a fall time. Real
 * meters have always worked this way: a VU averages about 300 ms in both
 * directions, a peak meter rises in milliseconds and falls over a second.
 *
 * Asymmetric, because a swell should register at once and a gap should linger
 * long enough to see. The attack was previously instant, which was invisible
 * only because the UI loop was repainting the whole display and updating this
 * four times a second; at twenty-five it read as nervous.
 */
#define UI_VU_ATTACK_MS 90U
#define UI_VU_RELEASE_MS 340U

/* How long the meter holds the last reading before deciding the audio has
 * stopped rather than merely being between blocks.
 *
 * Readings arrive one per decoded block, and the UI loop runs every 10 ms, so
 * most passes have nothing new to show. Treating those as silence is what a
 * take-and-reset level looks like from up here, and it wrecked the meter on
 * FLAC: 93 ms between blocks is nine passes of decay against one of attack,
 * and music that truly sat at 45% of the bar was displayed at under 10. The
 * target is held between readings instead, and only falls to zero once the
 * gap is longer than any block could explain - 4608 samples at 22 kHz, the
 * slowest this decodes, is 209 ms. */
#define UI_VU_READING_GAP_MS 250U

typedef struct {
    uint8_t percent;
    /* The last reading, held between blocks - not the same thing as `percent`,
     * which is where the bar has animated to on its way there. */
    uint8_t target;
    uint32_t idle_ms;
} ui_vu_meter_t;

void ui_vu_meter_init(ui_vu_meter_t *meter);

/* Maps an RMS magnitude to 0..100 on an approximate dB scale running from
 * -5 dBFS down to -25 dBFS - the range real music was measured to occupy on
 * this device. Silence is 0, and so is anything below the floor, since a bar
 * that never quite empties reads as a stuck meter.
 *
 * RMS and not peak on purpose: mastered music holds its peaks within a couple
 * of dB of full scale continuously, so a peak-fed bar sits pinned at the top
 * and shows nothing at all. */
uint8_t ui_vu_percent_from_level(uint16_t level);

/* Advances the meter by `elapsed_ms` towards `target_percent`, returning the
 * value to display. Always closes at least one percent of a gap, so it settles
 * exactly on the target instead of creeping towards it forever. */
uint8_t ui_vu_meter_step(ui_vu_meter_t *meter, uint8_t target_percent,
                         uint32_t elapsed_ms);

/* One pass of the UI loop. `have_reading` says whether a PCM block was
 * measured since the last pass; `level` is that block's RMS magnitude and is
 * ignored otherwise, so the bar keeps travelling towards the last reading
 * instead of being pulled back to silence by every pass that had nothing to
 * report. Silence still empties the bar - either as a reading of zero, or
 * after UI_VU_READING_GAP_MS with no block at all. */
uint8_t ui_vu_meter_advance(ui_vu_meter_t *meter, bool have_reading, uint16_t level,
                            uint32_t elapsed_ms);

/* How many of `count` segments a level lights. Rounds to nearest so a signal
 * sitting between two segments does not always read low, but keeps the two
 * ends exact: silence lights nothing and full scale lights everything, which
 * are the readings a listener actually checks. */
uint8_t ui_vu_lit_segments(uint8_t percent, uint8_t count);

/* True for segments in the red zone at the top of the scale. */
bool ui_vu_segment_is_red(uint8_t index, uint8_t count);
