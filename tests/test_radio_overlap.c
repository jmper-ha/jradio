#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "radio_overlap.h"

// A stand-in for a station's bytes: no run repeats within the test's span.
static uint8_t stream_byte(size_t position)
{
    return (uint8_t)((position * 2654435761U) >> 13);
}

static void fill(uint8_t *out, size_t from, size_t count)
{
    for (size_t i = 0U; i < count; ++i) out[i] = stream_byte(from + i);
}

static uint8_t buffer[64 * 1024];

/* The backlog holds stream bytes [old_from, old_to); the new connection starts
 * at stream byte `new_from` and is appended in `step`-byte pieces. Returns the
 * cut and checks the buffer is one unbroken run of the stream afterwards. */
static size_t splice(size_t old_from, size_t old_to, size_t new_from, size_t new_count, size_t step)
{
    radio_overlap_t overlap;
    radio_overlap_init(&overlap);
    size_t available = old_to - old_from;
    fill(buffer, old_from, available);
    radio_overlap_begin(&overlap, buffer, available);
    size_t cut = 0U;
    for (size_t sent = 0U; sent < new_count; sent += step) {
        const size_t piece = new_count - sent < step ? new_count - sent : step;
        fill(buffer + available, new_from + sent, piece);
        available += piece;
        cut += radio_overlap_take(&overlap, buffer, &available, piece);
    }
    assert(available == old_to - old_from + new_count - cut);
    return cut;
}

static void assert_continuous(size_t old_from, size_t length)
{
    for (size_t i = 0U; i < length; ++i) assert(buffer[i] == stream_byte(old_from + i));
}

int main(void)
{
    /* The burst reaches 3000 bytes back past the end of the backlog: those
     * 3000 are cut, and what is left runs straight on. */
    size_t cut = splice(10000U, 20000U, 17000U, 8000U, 1460U);
    assert(cut == 3000U);
    assert_continuous(10000U, 10000U + 8000U - 3000U);

    // A match split across two appends is still found.
    cut = splice(10000U, 20000U, 19000U, 4000U, 100U);
    assert(cut == 1000U);
    assert_continuous(10000U, 10000U + 4000U - 1000U);

    /* An overlap shorter than the tail cannot be told from chance and is kept:
     * 300 bytes is about 30 ms at this station's rate. */
    cut = splice(10000U, 20000U, 19700U, 4000U, 1000U);
    assert(cut == 0U);

    // Starting exactly where the old stream stopped: nothing to cut.
    cut = splice(10000U, 20000U, 20000U, 4000U, 1000U);
    assert(cut == 0U);
    for (size_t i = 0U; i < 14000U; ++i) assert(buffer[i] == stream_byte(10000U + i));

    /* The reconnect took longer than the burst reaches back: the new stream
     * starts after a gap, and every byte of it is kept. */
    cut = splice(10000U, 20000U, 25000U, 4000U, 1000U);
    assert(cut == 0U);

    // A backlog shorter than the tail is not enough to recognise anything.
    cut = splice(10000U, 10300U, 10000U, 4000U, 1000U);
    assert(cut == 0U);

    /* Once the decoder has eaten into the new bytes it is playing them, and
     * cutting them would cut sound: the search gives up. */
    {
        radio_overlap_t overlap;
        size_t available = 2000U;
        fill(buffer, 0U, available);
        radio_overlap_begin(&overlap, buffer, available);
        // The decoder consumed the whole backlog and 100 of the new bytes.
        fill(buffer, 1000U, 1000U);
        available = 900U;
        assert(radio_overlap_take(&overlap, buffer + 100U, &available, 1000U) == 0U);
        assert(!overlap.searching);
        assert(available == 900U);
    }

    // Not begun, or begun and done: appends pass straight through.
    {
        radio_overlap_t overlap;
        radio_overlap_init(&overlap);
        size_t available = 4000U;
        assert(radio_overlap_take(&overlap, buffer, &available, 4000U) == 0U);
        assert(available == 4000U);
        radio_overlap_begin(NULL, buffer, available);
        assert(radio_overlap_take(NULL, buffer, &available, 10U) == 0U);
    }

    puts("radio_overlap tests passed");
    return 0;
}
