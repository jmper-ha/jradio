#include <assert.h>
#include <stdio.h>

#include "radio_http_status.h"

int main(void)
{
    assert(radio_http_status_is_redirect(301));
    assert(radio_http_status_is_redirect(302));
    assert(radio_http_status_is_redirect(303));
    assert(radio_http_status_is_redirect(307));
    assert(radio_http_status_is_redirect(308));
    assert(!radio_http_status_is_redirect(200));
    assert(!radio_http_status_is_redirect(404));

    assert(radio_http_body_kind(200) == RADIO_HTTP_BODY_WHOLE);
    // 206 is what a track resumed after a pause asks for: the body picks up
    // where the pause stopped, so what was buffered is still its continuation.
    assert(radio_http_body_kind(206) == RADIO_HTTP_BODY_PARTIAL);
    /* A server that ignores the Range answers 200, and the caller has to know:
     * splicing the start of the track onto the middle of it is worse than
     * starting the track again. */
    assert(radio_http_body_kind(404) == RADIO_HTTP_BODY_FAILED);
    assert(radio_http_body_kind(403) == RADIO_HTTP_BODY_FAILED);
    // A redirect is neither: it is followed before a body is read at all.
    assert(radio_http_body_kind(302) == RADIO_HTTP_BODY_FAILED);
    assert(radio_http_body_kind(416) == RADIO_HTTP_BODY_FAILED);

    puts("radio_http_status tests passed");
    return 0;
}
