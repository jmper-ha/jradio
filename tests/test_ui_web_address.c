#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_web_address.h"

static void test_a_joined_network_shows_where_to_reach_the_box(void)
{
    char text[64];
    /* The whole point of the band: this address is otherwise only in the
     * serial log, which the user cannot see. */
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false, true,
                        text, sizeof(text));
    assert(strcmp(text, "http://192.168.1.182") == 0);
}

static void test_the_narrow_band_drops_the_scheme(void)
{
    /* Portrait gives the band 240 px and it still has to carry the hint on the
     * right. "http://" is seven characters that tell the reader nothing they
     * would not assume, and the address is the part they came for. */
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false, false,
                        text, sizeof(text));
    assert(strcmp(text, "192.168.1.182") == 0);
    /* The QR is untouched by the same choice: a code with no scheme in it
     * opens nothing when a camera reads it. */
    char payload[96];
    assert(ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home",
                             payload, sizeof(payload)));
    assert(strcmp(payload, "http://192.168.1.182") == 0);
    /* Setup mode names the network and stops there, whichever way round. */
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2", false, false,
                        text, sizeof(text));
    assert(strcmp(text, "Подключитесь к сети jradio-A1B2") == 0);
}

static void test_setup_mode_names_the_network_to_join(void)
{
    /* The case the previous screen got wrong twice: first it asked "are we
     * connected", answered no while the box ran its own access point, and hid
     * the one address that works there. Then it showed that address alone -
     * unreachable until the user knows which network to join, which nothing
     * else on the device says. The name is the whole answer here: the address
     * means nothing until the phone has joined, and the QR does the joining. */
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2", false, true,
                        text, sizeof(text));
    assert(strcmp(text, "Подключитесь к сети jradio-A1B2") == 0);
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2", true, true,
                        text, sizeof(text));
    assert(strcmp(text, "join jradio-A1B2") == 0);
}

static void test_setup_mode_without_an_address_still_says_what_to_join(void)
{
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "", "jradio-A1B2", false, true, text,
                        sizeof(text));
    assert(strcmp(text, "Подключитесь к сети jradio-A1B2") == 0);
}

static void test_connecting_never_shows_a_stale_address(void)
{
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", "home", false, true,
                        text, sizeof(text));
    assert(strcmp(text, "Подключение к сети...") == 0);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", "home", true, true,
                        text, sizeof(text));
    assert(strcmp(text, "connecting to Wi-Fi...") == 0);
}

static void test_a_missing_address_says_what_is_happening(void)
{
    /* Not "no network": the wait ends either with an address or with the setup
     * AP, and neither is worth telling the user to give up on. */
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "", "home", false, true, text,
                        sizeof(text));
    assert(strcmp(text, "Подключение к сети...") == 0);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, NULL, NULL, true, true, text,
                        sizeof(text));
    assert(strcmp(text, "connecting to Wi-Fi...") == 0);
}

static void test_setup_mode_without_a_name_falls_back_to_the_address(void)
{
    // The name is read from the same status the mode is; an empty one means
    // the AP has not published itself yet, and the address still works.
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "", false, true, text,
                        sizeof(text));
    assert(strcmp(text, "http://192.168.4.1") == 0);
}

static void test_bad_arguments_are_survivable(void)
{
    char text[8];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false, true,
                        NULL, 0U);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false, true,
                        text, 0U);
    /* A band too narrow for the address truncates rather than overruns. */
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false, true,
                        text, sizeof(text));
    assert(strlen(text) == sizeof(text) - 1U);
}

static void test_a_joined_network_encodes_the_url(void)
{
    /* The QR exists so a phone never has to be told an IP by hand. On a real
     * network that is all it takes: the camera opens the URL. */
    char payload[96];
    assert(ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home",
                             payload, sizeof(payload)));
    assert(strcmp(payload, "http://192.168.1.182") == 0);
}

static void test_setup_mode_encodes_the_join_instead(void)
{
    /* The case the URL cannot serve: the phone is not on the box's network,
     * so http://192.168.4.1 fails before the browser draws anything. The QR
     * hands over the network instead, and the AP is open - no password in a
     * code anyone in the room can photograph. */
    char payload[96];
    assert(ui_web_address_qr(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2",
                             payload, sizeof(payload)));
    assert(strcmp(payload, "WIFI:T:nopass;S:jradio-A1B2;;") == 0);
}

static void test_a_name_with_separators_in_it_is_escaped(void)
{
    /* The setup AP names itself jradio-XXXX and never needs this, but the name
     * arrives as text from the provisioning status: an unescaped ';' would end
     * the field and the phone would join something else, or nothing. */
    char payload[96];
    assert(ui_web_address_qr(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "a;b:c,d\\e\"f",
                             payload, sizeof(payload)));
    assert(strcmp(payload, "WIFI:T:nopass;S:a\\;b\\:c\\,d\\\\e\\\"f;;") == 0);
}

static void test_nothing_to_reach_means_no_code(void)
{
    /* A QR that sends a phone to an address the box does not answer on is
     * worse than no QR: the failure looks like the phone's fault. The band
     * asks this same question to decide whether to offer the code at all. */
    char payload[96];
    assert(!ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", "home",
                              payload, sizeof(payload)));
    assert(payload[0] == '\0');
    assert(!ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTED, "", "home", payload,
                              sizeof(payload)));
    assert(payload[0] == '\0');
    assert(!ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTED, NULL, NULL, payload,
                              sizeof(payload)));
    assert(payload[0] == '\0');
    assert(!ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home",
                              NULL, 0U));

    /* An AP that has not published its name yet still has an address, and the
     * band says the same thing there - so the code does too. */
    assert(ui_web_address_qr(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "", payload,
                             sizeof(payload)));
    assert(strcmp(payload, "http://192.168.4.1") == 0);
}

static void test_a_payload_that_would_not_fit_is_refused_whole(void)
{
    /* Truncation is the one failure a QR hides: a cut-short payload still
     * scans, and hands the phone half an address. */
    char payload[8];
    assert(!ui_web_address_qr(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home",
                              payload, sizeof(payload)));
    assert(payload[0] == '\0');
    assert(!ui_web_address_qr(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2",
                              payload, sizeof(payload)));
    assert(payload[0] == '\0');
}

int main(void)
{
    test_a_joined_network_shows_where_to_reach_the_box();
    test_the_narrow_band_drops_the_scheme();
    test_setup_mode_names_the_network_to_join();
    test_setup_mode_without_an_address_still_says_what_to_join();
    test_connecting_never_shows_a_stale_address();
    test_a_missing_address_says_what_is_happening();
    test_setup_mode_without_a_name_falls_back_to_the_address();
    test_bad_arguments_are_survivable();
    test_a_joined_network_encodes_the_url();
    test_setup_mode_encodes_the_join_instead();
    test_a_name_with_separators_in_it_is_escaped();
    test_nothing_to_reach_means_no_code();
    test_a_payload_that_would_not_fit_is_refused_whole();
    printf("ui_web_address tests passed\n");
    return 0;
}
