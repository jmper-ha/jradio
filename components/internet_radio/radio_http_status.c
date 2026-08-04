#include "radio_http_status.h"

bool radio_http_status_is_redirect(int status_code)
{
    return status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 ||
           status_code == 308;
}
