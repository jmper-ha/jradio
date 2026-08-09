#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wifi_provisioning.h"

static bool bytes_contain(const void *memory, size_t memory_size,
                          const char *needle)
{
    const unsigned char *bytes = memory;
    const size_t needle_size = strlen(needle);
    if (needle_size == 0U || needle_size > memory_size) return false;
    for (size_t offset = 0U; offset <= memory_size - needle_size; ++offset) {
        if (memcmp(bytes + offset, needle, needle_size) == 0) return true;
    }
    return false;
}

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

static void test_public_saved_network_view_contains_only_ssids(void)
{
    wifi_settings_t settings = {.count = 2};
    strcpy(settings.networks[0].ssid, "home");
    strcpy(settings.networks[0].password, "topsecret42");
    strcpy(settings.networks[1].ssid, "guest");
    strcpy(settings.networks[1].password, "guest-secret");
    wifi_provisioning_saved_ssids_t public_view;

    wifi_provisioning_extract_ssids(&settings, &public_view);

    assert(public_view.count == 2);
    assert(strcmp(public_view.ssids[0], "home") == 0);
    assert(strcmp(public_view.ssids[1], "guest") == 0);
    assert(!bytes_contain(&public_view, sizeof(public_view), "topsecret42"));
    assert(!bytes_contain(&public_view, sizeof(public_view), "guest-secret"));
}

int main(void)
{
    test_initial_mode_depends_on_saved_networks();
    test_network_indices_follow_saved_order();
    test_disconnect_reconnect_policy();
    test_reset_saved_state_forgets_pending_and_committed_networks();
    test_public_saved_network_view_contains_only_ssids();
    puts("wifi_provisioning tests passed");
    return 0;
}
