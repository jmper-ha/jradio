#include "ui_web_address.h"

#include <stdio.h>

void ui_web_address_text(wifi_provisioning_mode_t mode, const char *ipv4, bool english,
                         char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';

    /* An address without a network behind it sends the user to a URL that
     * cannot answer, which is worse than admitting there is none. While
     * connecting there is no address yet, and the previous one may belong to a
     * network the box has already left. */
    if (ipv4 == NULL || ipv4[0] == '\0' || mode == WIFI_PROVISIONING_STA_CONNECTING) {
        snprintf(out, out_size, "%s", english ? "no network" : "нет сети");
        return;
    }
    snprintf(out, out_size, "http://%s", ipv4);
}
