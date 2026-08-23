#include "ui_web_address.h"

#include <stdio.h>

void ui_web_address_text(wifi_provisioning_mode_t mode, const char *ipv4, const char *ssid,
                         bool english, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';

    /* The setup AP's address is useless on its own: reaching it means joining
     * the box's own network first, and nothing else on the device says what
     * that network is called. This is where a user with no working Wi-Fi ends
     * up, so it is the one place the name has to appear. */
    if (mode == WIFI_PROVISIONING_AP_SETUP && ssid != NULL && ssid[0] != '\0') {
        if (ipv4 != NULL && ipv4[0] != '\0') {
            snprintf(out, out_size, english ? "join %s, %s" : "Сеть %s, %s", ssid, ipv4);
        } else {
            snprintf(out, out_size, english ? "join %s" : "Подключитесь к сети %s", ssid);
        }
        return;
    }

    /* An address without a network behind it sends the user to a URL that
     * cannot answer, which is worse than admitting there is none. While
     * connecting there is no address yet, and the previous one may belong to a
     * network the box has already left. Saying what is happening beats "no
     * network": the wait ends either with an address or with the setup AP
     * above, and both are worth waiting for. */
    if (ipv4 == NULL || ipv4[0] == '\0' || mode == WIFI_PROVISIONING_STA_CONNECTING) {
        snprintf(out, out_size, english ? "connecting to Wi-Fi..." : "Подключение к сети...");
        return;
    }
    snprintf(out, out_size, "http://%s", ipv4);
}
