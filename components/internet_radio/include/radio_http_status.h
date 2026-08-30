#pragma once

#include <stdbool.h>

bool radio_http_status_is_redirect(int status_code);

/* What a response says about where its body starts.
 *
 * A stream is normally opened whole and 200 is the only answer worth having.
 * A track resumed after a pause is opened with a Range instead, and then the
 * two successful answers mean different things: 206 continues where the pause
 * stopped, while a 200 is a server that ignored the Range and is sending the
 * track from its beginning - the caller has to throw away what it had buffered
 * rather than splice the start of the track onto the middle of it. */
typedef enum {
    RADIO_HTTP_BODY_FAILED = 0,
    // 200: the body starts at byte zero, whatever was asked for.
    RADIO_HTTP_BODY_WHOLE,
    // 206: the body starts where the Range asked it to.
    RADIO_HTTP_BODY_PARTIAL,
} radio_http_body_t;

radio_http_body_t radio_http_body_kind(int status_code);
