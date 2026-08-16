#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_settings_view.h"

static void row(const ui_settings_info_t *info, ui_settings_row_t which,
                char *out, size_t size)
{
    ui_settings_row_text(which, info, out, size);
}

static void test_a_connected_device_shows_where_to_reach_it(void)
{
    const ui_settings_info_t info = {
        .connected = true,
        .ssid = "my_home_ap",
        .ipv4 = "192.168.1.182",
        .rssi_valid = true,
        .rssi_dbm = -68,
        .web_ready = true,
    };
    char text[96];

    row(&info, UI_SETTINGS_ROW_WIFI, text, sizeof(text));
    assert(strcmp(text, "Wi-Fi: my_home_ap") == 0);
    /* The whole point of the screen: the address is otherwise only in the
     * serial log, which the user cannot see. */
    row(&info, UI_SETTINGS_ROW_ADDRESS, text, sizeof(text));
    assert(strcmp(text, "Веб-интерфейс: http://192.168.1.182") == 0);
    row(&info, UI_SETTINGS_ROW_SIGNAL, text, sizeof(text));
    assert(strcmp(text, "Сигнал: -68 дБм") == 0);
}

static void test_a_disconnected_device_says_so_on_every_row(void)
{
    /* A stale SSID or address would be worse than nothing: it sends the user
     * to a URL that cannot answer. */
    const ui_settings_info_t info = {
        .connected = false,
        .ssid = "my_home_ap",
        .ipv4 = "192.168.1.182",
        .rssi_valid = true,
        .rssi_dbm = -68,
        .web_ready = true,
    };
    char text[96];

    row(&info, UI_SETTINGS_ROW_WIFI, text, sizeof(text));
    assert(strstr(text, "my_home_ap") == NULL);
    row(&info, UI_SETTINGS_ROW_ADDRESS, text, sizeof(text));
    assert(strstr(text, "192.168") == NULL);
    row(&info, UI_SETTINGS_ROW_SIGNAL, text, sizeof(text));
    assert(strcmp(text, "Сигнал: --") == 0);
}

static void test_an_address_without_a_server_is_not_offered(void)
{
    const ui_settings_info_t info = {
        .connected = true,
        .ssid = "ap",
        .ipv4 = "10.0.0.5",
        .web_ready = false,
    };
    char text[96];
    row(&info, UI_SETTINGS_ROW_ADDRESS, text, sizeof(text));
    assert(strstr(text, "10.0.0.5") == NULL);
}

static void test_missing_strings_do_not_produce_a_blank_row(void)
{
    /* Connected but the SSID has not propagated yet - a blank row reads as a
     * broken screen, so it gets a placeholder. */
    const ui_settings_info_t info = {.connected = true, .ssid = NULL, .ipv4 = NULL};
    char text[96];

    row(&info, UI_SETTINGS_ROW_WIFI, text, sizeof(text));
    assert(strlen(text) > strlen("Wi-Fi: "));
    row(&info, UI_SETTINGS_ROW_ADDRESS, text, sizeof(text));
    assert(strlen(text) > 0U);

    const ui_settings_info_t empty = {.connected = true, .ssid = "", .ipv4 = ""};
    row(&empty, UI_SETTINGS_ROW_WIFI, text, sizeof(text));
    assert(strlen(text) > strlen("Wi-Fi: "));
}

static void test_signal_prints_a_negative_value_correctly(void)
{
    ui_settings_info_t info = {.connected = true, .rssi_valid = true, .rssi_dbm = -100};
    char text[96];
    row(&info, UI_SETTINGS_ROW_SIGNAL, text, sizeof(text));
    assert(strcmp(text, "Сигнал: -100 дБм") == 0);

    info.rssi_dbm = 0;
    row(&info, UI_SETTINGS_ROW_SIGNAL, text, sizeof(text));
    assert(strcmp(text, "Сигнал: 0 дБм") == 0);
}

static void test_every_row_is_filled_so_none_reads_as_broken(void)
{
    /* A caller loops over the rows blindly; each must yield something. */
    const ui_settings_info_t info = {.connected = true, .ssid = "ap", .ipv4 = "1.2.3.4",
                                     .web_ready = true};
    for (int which = 0; which < UI_SETTINGS_ROW_COUNT; ++which) {
        char text[96];
        row(&info, (ui_settings_row_t)which, text, sizeof(text));
        assert(strlen(text) > 0U);
    }
}

static void test_a_short_buffer_truncates_instead_of_overflowing(void)
{
    const ui_settings_info_t info = {.connected = true, .ssid = "a_very_long_network_name",
                                     .ipv4 = "192.168.100.200", .web_ready = true};
    char text[10];
    row(&info, UI_SETTINGS_ROW_ADDRESS, text, sizeof(text));
    assert(strlen(text) < sizeof(text));
}

static void test_the_formatter_tolerates_missing_arguments(void)
{
    char text[16] = "unchanged";
    ui_settings_row_text(UI_SETTINGS_ROW_WIFI, NULL, text, sizeof(text));
    assert(text[0] == '\0');

    const ui_settings_info_t info = {.connected = true};
    ui_settings_row_text(UI_SETTINGS_ROW_WIFI, &info, NULL, 16U);
    ui_settings_row_text(UI_SETTINGS_ROW_WIFI, &info, text, 0U);
    /* An out-of-range row is empty rather than rejected. */
    ui_settings_row_text(UI_SETTINGS_ROW_COUNT, &info, text, sizeof(text));
    assert(text[0] == '\0');
}

int main(void)
{
    test_a_connected_device_shows_where_to_reach_it();
    test_a_disconnected_device_says_so_on_every_row();
    test_an_address_without_a_server_is_not_offered();
    test_missing_strings_do_not_produce_a_blank_row();
    test_signal_prints_a_negative_value_correctly();
    test_every_row_is_filled_so_none_reads_as_broken();
    test_a_short_buffer_truncates_instead_of_overflowing();
    test_the_formatter_tolerates_missing_arguments();
    puts("ui_settings_view tests passed");
    return 0;
}
