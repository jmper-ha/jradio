#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wifi_settings.h"

static bool bytes_contain(const void *memory, size_t memory_size, const char *needle)
{
    const unsigned char *bytes = memory;
    const size_t needle_size = strlen(needle);
    if (needle_size == 0U || needle_size > memory_size) return false;
    for (size_t offset = 0U; offset <= memory_size - needle_size; ++offset) {
        if (memcmp(bytes + offset, needle, needle_size) == 0) return true;
    }
    return false;
}

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

/* Order is priority, so removal has to close the gap rather than leave a hole:
   the reader walks the list from the top and stops at the count. */
static void test_remove_closes_the_gap(void)
{
    wifi_settings_t settings = {0};

    assert(wifi_settings_upsert(&settings, "home", "one") == WIFI_SETTINGS_OK);
    assert(wifi_settings_upsert(&settings, "guest", "two") == WIFI_SETTINGS_OK);
    assert(wifi_settings_upsert(&settings, "shop", "three") == WIFI_SETTINGS_OK);

    assert(wifi_settings_remove(&settings, "guest"));
    assert(settings.count == 2);
    assert(strcmp(settings.networks[0].ssid, "home") == 0);
    assert(strcmp(settings.networks[1].ssid, "shop") == 0);
    assert(strcmp(settings.networks[1].password, "three") == 0);

    assert(!wifi_settings_remove(&settings, "guest"));
    assert(!wifi_settings_remove(&settings, ""));
    assert(settings.count == 2);
}

/* The whole structure travels by value between tasks, so a password left in the
   slot past the count would be copied along with it. */
static void test_remove_wipes_the_freed_slot(void)
{
    wifi_settings_t settings = {0};

    assert(wifi_settings_upsert(&settings, "home", "topsecret42") == WIFI_SETTINGS_OK);
    assert(wifi_settings_remove(&settings, "home"));
    assert(settings.count == 0);
    assert(!bytes_contain(&settings, sizeof(settings), "topsecret42"));
}

static void test_promote_moves_to_the_front_and_keeps_the_rest_in_order(void)
{
    wifi_settings_t settings = {0};

    assert(wifi_settings_upsert(&settings, "home", "one") == WIFI_SETTINGS_OK);
    assert(wifi_settings_upsert(&settings, "guest", "two") == WIFI_SETTINGS_OK);
    assert(wifi_settings_upsert(&settings, "shop", "three") == WIFI_SETTINGS_OK);

    assert(wifi_settings_promote(&settings, "shop"));
    assert(settings.count == 3);
    assert(strcmp(settings.networks[0].ssid, "shop") == 0);
    assert(strcmp(settings.networks[0].password, "three") == 0);
    assert(strcmp(settings.networks[1].ssid, "home") == 0);
    assert(strcmp(settings.networks[2].ssid, "guest") == 0);

    /* Promoting the one that is already first is a no-op, not a reshuffle. */
    assert(wifi_settings_promote(&settings, "shop"));
    assert(strcmp(settings.networks[0].ssid, "shop") == 0);
    assert(strcmp(settings.networks[1].ssid, "home") == 0);
    assert(strcmp(settings.networks[2].ssid, "guest") == 0);

    assert(!wifi_settings_promote(&settings, "missing"));
}

int main(void)
{
    test_upsert_preserves_order_and_updates_matching_ssid();
    test_upsert_rejects_sixth_network();
    test_remove_closes_the_gap();
    test_remove_wipes_the_freed_slot();
    test_promote_moves_to_the_front_and_keeps_the_rest_in_order();
    puts("wifi_settings tests passed");
    return 0;
}
