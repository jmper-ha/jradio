#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_web_address.h"

static void test_a_joined_network_shows_where_to_reach_the_box(void)
{
    char text[64];
    /* The whole point of the band: this address is otherwise only in the
     * serial log, which the user cannot see. */
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false,
                        text, sizeof(text));
    assert(strcmp(text, "http://192.168.1.182") == 0);
}

static void test_setup_mode_names_the_network_to_join(void)
{
    /* The case the previous screen got wrong twice: first it asked "are we
     * connected", answered no while the box ran its own access point, and hid
     * the one address that works there. Then it showed that address alone -
     * unreachable until the user knows which network to join, which nothing
     * else on the device says. */
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2", false,
                        text, sizeof(text));
    assert(strcmp(text, "Сеть jradio-A1B2, 192.168.4.1") == 0);
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "jradio-A1B2", true,
                        text, sizeof(text));
    assert(strcmp(text, "join jradio-A1B2, 192.168.4.1") == 0);
}

static void test_setup_mode_without_an_address_still_says_what_to_join(void)
{
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "", "jradio-A1B2", false, text,
                        sizeof(text));
    assert(strcmp(text, "Подключитесь к сети jradio-A1B2") == 0);
}

static void test_connecting_never_shows_a_stale_address(void)
{
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", "home", false,
                        text, sizeof(text));
    assert(strcmp(text, "Подключение к сети...") == 0);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", "home", true,
                        text, sizeof(text));
    assert(strcmp(text, "connecting to Wi-Fi...") == 0);
}

static void test_a_missing_address_says_what_is_happening(void)
{
    /* Not "no network": the wait ends either with an address or with the setup
     * AP, and neither is worth telling the user to give up on. */
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "", "home", false, text,
                        sizeof(text));
    assert(strcmp(text, "Подключение к сети...") == 0);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, NULL, NULL, true, text,
                        sizeof(text));
    assert(strcmp(text, "connecting to Wi-Fi...") == 0);
}

static void test_setup_mode_without_a_name_falls_back_to_the_address(void)
{
    // The name is read from the same status the mode is; an empty one means
    // the AP has not published itself yet, and the address still works.
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", "", false, text,
                        sizeof(text));
    assert(strcmp(text, "http://192.168.4.1") == 0);
}

static void test_bad_arguments_are_survivable(void)
{
    char text[8];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false,
                        NULL, 0U);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false,
                        text, 0U);
    /* A band too narrow for the address truncates rather than overruns. */
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", "home", false,
                        text, sizeof(text));
    assert(strlen(text) == sizeof(text) - 1U);
}

int main(void)
{
    test_a_joined_network_shows_where_to_reach_the_box();
    test_setup_mode_names_the_network_to_join();
    test_setup_mode_without_an_address_still_says_what_to_join();
    test_connecting_never_shows_a_stale_address();
    test_a_missing_address_says_what_is_happening();
    test_setup_mode_without_a_name_falls_back_to_the_address();
    test_bad_arguments_are_survivable();
    printf("ui_web_address tests passed\n");
    return 0;
}
