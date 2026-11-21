#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Cuts out what a live station sends twice across a background reconnect.
 *
 * A dropped connection is reopened while the backlog plays on, and the new
 * stream goes on the end of it. An Icecast server opens every connection with
 * a burst of the last seconds it sent, and those seconds are already in the
 * backlog: spliced on as they came, they were heard again - "a repeat of the
 * last second or two" (forum, v1.13.1, radiorecord.hostingradio.ru, which
 * dropped its TLS connection every minute or two). Two connections to that
 * server 12 s apart carried the same bytes at an offset, so the seam is found
 * by content: the last RADIO_OVERLAP_TAIL bytes of the old stream are looked
 * for in the new one, and everything up to the end of the match is dropped.
 *
 * Compared after the ICY layer has taken its metadata out, so the two
 * connections' intervals do not have to line up. No match - the server lost
 * its place, or the reconnect took longer than its burst reaches back - keeps
 * every byte, which is what happened before this existed. */

#define RADIO_OVERLAP_TAIL 512U

typedef struct {
    uint8_t tail[RADIO_OVERLAP_TAIL];
    bool searching;
    // New-stream bytes appended since the splice, all still in the buffer.
    size_t fresh;
    // How far into them the tail has been looked for.
    size_t searched;
} radio_overlap_t;

void radio_overlap_init(radio_overlap_t *overlap);

/* At the splice: `unconsumed` is the backlog, `available` long, whose end the
 * old stream reached. Less than a tail of it is not enough to recognise. */
void radio_overlap_begin(radio_overlap_t *overlap, const uint8_t *unconsumed, size_t available);

/* After `appended` bytes of the new stream went on the end of the backlog.
 * When the old stream's tail turns up in them, the bytes up to its end are
 * cut out of the buffer in place and `*available` shrinks by as many; the
 * return value is that count, 0 while still looking or after giving up. The
 * search gives up once the decoder has reached the new bytes: from there on
 * they are being played, and cutting them would only cut sound. */
size_t radio_overlap_take(radio_overlap_t *overlap, uint8_t *unconsumed, size_t *available,
                          size_t appended);
