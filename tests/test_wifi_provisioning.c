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

static void test_disconnect_reconnect_policy(void)
{
    assert(wifi_provisioning_should_reconnect(WIFI_PROVISIONING_AP_SETUP) == false);
    assert(wifi_provisioning_should_reconnect(WIFI_PROVISIONING_STA_CONNECTING) == true);
    assert(wifi_provisioning_should_reconnect(WIFI_PROVISIONING_STA_CONNECTED) == true);
}

static void test_reset_saved_state_forgets_pending_and_committed_networks(void)
{
    wifi_settings_t current = {.count = 2};
    wifi_settings_t committed = {.count = 1};
    bool pending_commit = true;

    wifi_provisioning_reset_saved_state(&current, &committed, &pending_commit);

    assert(current.count == 0);
    assert(committed.count == 0);
    assert(!pending_commit);
}

int main(void)
{
    test_initial_mode_depends_on_saved_networks();
    test_network_indices_follow_saved_order();
    test_disconnect_reconnect_policy();
    test_reset_saved_state_forgets_pending_and_committed_networks();
    puts("wifi_provisioning tests passed");
    return 0;
}
