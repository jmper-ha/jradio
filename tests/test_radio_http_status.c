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
    puts("radio_http_status tests passed");
    return 0;
}
