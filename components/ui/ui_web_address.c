#include "ui_web_address.h"

#include <stdio.h>

void ui_web_address_text(wifi_provisioning_mode_t mode, const char *ipv4, const char *ssid,
                         bool english, bool with_scheme, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';

    /* The setup AP's address is useless on its own: reaching it means joining
     * the box's own network first, and nothing else on the device says what
     * that network is called. This is where a user with no working Wi-Fi ends
     * up, so it is the one place the name has to appear - and the only thing
     * that appears. The address used to be printed beside it and was noise:
     * it cannot be typed anywhere until the phone has joined, and by then the
     * QR behind this band has already opened it. */
    if (mode == WIFI_PROVISIONING_AP_SETUP && ssid != NULL && ssid[0] != '\0') {
        snprintf(out, out_size, english ? "join %s" : "Подключитесь к сети %s", ssid);
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
    /* The setup-AP line above already prints the address bare, beside the
     * network name - there has never been room for a scheme there either. */
    snprintf(out, out_size, with_scheme ? "http://%s" : "%s", ipv4);
}

/* Appends with the bounds check the whole payload depends on: a QR that got
 * cut short still scans, and hands the phone a truncated SSID or half a URL. */
static bool append_text(char *out, size_t out_size, size_t *length, const char *text,
                        bool escape)
{
    for (const char *ch = text; *ch != '\0'; ++ch) {
        /* The WIFI: form separates its fields with these, so an SSID
         * containing one has to say it is not a separator. The setup AP names
         * itself jradio-XXXX and needs none of it, but the name arrives as
         * text from the provisioning status, and a payload that quietly means
         * something else is worse than the few lines it takes to be right. */
        if (escape && (*ch == '\\' || *ch == ';' || *ch == ',' || *ch == ':' || *ch == '"')) {
            if (*length + 1U >= out_size) return false;
            out[(*length)++] = '\\';
        }
        if (*length + 1U >= out_size) return false;
        out[(*length)++] = *ch;
    }
    out[*length] = '\0';
    return true;
}

bool ui_web_address_qr(wifi_provisioning_mode_t mode, const char *ipv4, const char *ssid,
                       char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return false;
    out[0] = '\0';

    if (mode == WIFI_PROVISIONING_AP_SETUP && ssid != NULL && ssid[0] != '\0') {
        size_t length = 0U;
        if (append_text(out, out_size, &length, "WIFI:T:nopass;S:", false) &&
            append_text(out, out_size, &length, ssid, true) &&
            append_text(out, out_size, &length, ";;", false)) {
            return true;
        }
        out[0] = '\0';
        return false;
    }

    /* The same refusals the band's text makes, for the same reason: a QR that
     * sends a phone to an address the box does not answer on is worse than no
     * QR, because the failure looks like the phone's fault. */
    if (ipv4 == NULL || ipv4[0] == '\0' || mode == WIFI_PROVISIONING_STA_CONNECTING) {
        return false;
    }
    size_t length = 0U;
    if (append_text(out, out_size, &length, "http://", false) &&
        append_text(out, out_size, &length, ipv4, false)) {
        return true;
    }
    out[0] = '\0';
    return false;
}
