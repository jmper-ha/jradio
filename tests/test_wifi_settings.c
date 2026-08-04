#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wifi_settings.h"

static void test_upsert_preserves_order_and_updates_matching_ssid(void)
{
    wifi_settings_t settings = {0};

    assert(wifi_settings_upsert(&settings, "home", "secret") == WIFI_SETTINGS_OK);
    assert(wifi_settings_upsert(&settings, "guest", "guestpass") == WIFI_SETTINGS_OK);
    assert(settings.count == 2);
    assert(wifi_settings_upsert(&settings, "home", "newsecret") == WIFI_SETTINGS_OK);
    assert(settings.count == 2);
    assert(strcmp(settings.networks[0].ssid, "home") == 0);
    assert(strcmp(settings.networks[0].password, "newsecret") == 0);
    assert(strcmp(settings.networks[1].ssid, "guest") == 0);
}

static void test_upsert_rejects_sixth_network(void)
{
    wifi_settings_t settings = {0};
    const char *ssids[] = {"one", "two", "three", "four", "five"};

    for (size_t index = 0; index < WIFI_SETTINGS_MAX_NETWORKS; ++index) {
        assert(wifi_settings_upsert(&settings, ssids[index], "password") == WIFI_SETTINGS_OK);
    }
    assert(wifi_settings_upsert(&settings, "six", "password") == WIFI_SETTINGS_FULL);
}

int main(void)
{
    test_upsert_preserves_order_and_updates_matching_ssid();
    test_upsert_rejects_sixth_network();
    puts("wifi_settings tests passed");
    return 0;
}
