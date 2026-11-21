#include "radio_overlap.h"

#include <string.h>

void radio_overlap_init(radio_overlap_t *overlap)
{
    if (overlap == NULL) return;
    overlap->searching = false;
    overlap->fresh = 0U;
    overlap->searched = 0U;
}

void radio_overlap_begin(radio_overlap_t *overlap, const uint8_t *unconsumed, size_t available)
{
    if (overlap == NULL) return;
    radio_overlap_init(overlap);
    if (unconsumed == NULL || available < RADIO_OVERLAP_TAIL) return;
    memcpy(overlap->tail, unconsumed + available - RADIO_OVERLAP_TAIL, RADIO_OVERLAP_TAIL);
    overlap->searching = true;
}

size_t radio_overlap_take(radio_overlap_t *overlap, uint8_t *unconsumed, size_t *available,
                          size_t appended)
{
    if (overlap == NULL || !overlap->searching || unconsumed == NULL || available == NULL) {
        return 0U;
    }
    overlap->fresh += appended;
    if (overlap->fresh > *available) {
        overlap->searching = false;
        return 0U;
    }
    uint8_t *fresh = unconsumed + *available - overlap->fresh;
    // Back by a tail's length less one: a match may straddle two appends.
    size_t at = overlap->searched >= RADIO_OVERLAP_TAIL - 1U
                    ? overlap->searched - (RADIO_OVERLAP_TAIL - 1U)
                    : 0U;
    overlap->searched = overlap->fresh;
    while (at + RADIO_OVERLAP_TAIL <= overlap->fresh) {
        const uint8_t *hit = memchr(fresh + at, overlap->tail[0],
                                    overlap->fresh - RADIO_OVERLAP_TAIL + 1U - at);
        if (hit == NULL) break;
        at = (size_t)(hit - fresh);
        if (memcmp(hit, overlap->tail, RADIO_OVERLAP_TAIL) == 0) {
            const size_t cut = at + RADIO_OVERLAP_TAIL;
            memmove(fresh, fresh + cut, overlap->fresh - cut);
            *available -= cut;
            overlap->searching = false;
            return cut;
        }
        ++at;
    }
    return 0U;
}
