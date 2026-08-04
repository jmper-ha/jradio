#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "wifi_provisioning.h"

static void test_initial_mode_depends_on_saved_networks(void)
{
    const wifi_settings_t empty = {0};
    const wifi_settings_t configured = {.count = 1};

    assert(wifi_provisioning_initial_mode(&empty) == WIFI_PROVISIONING_AP_SETUP);
    assert(wifi_provisioning_initial_mode(&configured) == WIFI_PROVISIONING_STA_CONNECTING);
}

static void test_network_indices_follow_saved_order(void)
{
    const wifi_settings_t settings = {.count = 3};

    assert(wifi_provisioning_network_index(&settings, 0) == 0);
    assert(wifi_provisioning_network_index(&settings, 1) == 1);
    assert(wifi_provisioning_network_index(&settings, 2) == 2);
    assert(wifi_provisioning_network_index(&settings, 3) == WIFI_SETTINGS_MAX_NETWORKS);

    const wifi_settings_t empty = {0};
    assert(wifi_provisioning_network_index(&empty, 0) == WIFI_SETTINGS_MAX_NETWORKS);
}

int main(void)
{
    test_initial_mode_depends_on_saved_networks();
    test_network_indices_follow_saved_order();
    puts("wifi_provisioning tests passed");
    return 0;
}
