#include "radio_http_status.h"

bool radio_http_status_is_redirect(int status_code)
{
    return status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 ||
           status_code == 308;
}

radio_http_body_t radio_http_body_kind(int status_code)
{
    if (status_code == 200) return RADIO_HTTP_BODY_WHOLE;
    if (status_code == 206) return RADIO_HTTP_BODY_PARTIAL;
    return RADIO_HTTP_BODY_FAILED;
}
