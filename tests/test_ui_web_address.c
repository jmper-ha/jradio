#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_web_address.h"

static void test_a_joined_network_shows_where_to_reach_the_box(void)
{
    char text[64];
    /* The whole point of the band: this address is otherwise only in the
     * serial log, which the user cannot see. */
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", false,
                        text, sizeof(text));
    assert(strcmp(text, "http://192.168.1.182") == 0);
}

static void test_setup_mode_shows_its_own_address(void)
{
    /* The case the previous screen got wrong: it asked "are we connected",
     * answered no while the box ran its own access point, and hid the one
     * address that works there. */
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_AP_SETUP, "192.168.4.1", false, text, sizeof(text));
    assert(strcmp(text, "http://192.168.4.1") == 0);
}

static void test_connecting_never_shows_a_stale_address(void)
{
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", false,
                        text, sizeof(text));
    assert(strcmp(text, "нет сети") == 0);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTING, "192.168.1.182", true,
                        text, sizeof(text));
    assert(strcmp(text, "no network") == 0);
}

static void test_a_missing_address_says_so(void)
{
    char text[64];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "", false, text, sizeof(text));
    assert(strcmp(text, "нет сети") == 0);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, NULL, true, text, sizeof(text));
    assert(strcmp(text, "no network") == 0);
}

static void test_bad_arguments_are_survivable(void)
{
    char text[8];
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", false, NULL, 0U);
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", false, text, 0U);
    /* A band too narrow for the address truncates rather than overruns. */
    ui_web_address_text(WIFI_PROVISIONING_STA_CONNECTED, "192.168.1.182", false,
                        text, sizeof(text));
    assert(strlen(text) == sizeof(text) - 1U);
}

int main(void)
{
    test_a_joined_network_shows_where_to_reach_the_box();
    test_setup_mode_shows_its_own_address();
    test_connecting_never_shows_a_stale_address();
    test_a_missing_address_says_so();
    test_bad_arguments_are_survivable();
    printf("ui_web_address tests passed\n");
    return 0;
}
