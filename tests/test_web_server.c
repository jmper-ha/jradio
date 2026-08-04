#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "web_server.h"

static void test_parse_accepts_ssid_and_password(void)
{
    wifi_network_t network;
    assert(web_server_parse_wifi_request("{\"ssid\":\"home\",\"password\":\"pw\"}",
                                         &network) == WEB_SERVER_PARSE_OK);
    assert(strcmp(network.ssid, "home") == 0);
    assert(strcmp(network.password, "pw") == 0);
}

static void test_parse_rejects_missing_or_empty_fields(void)
{
    wifi_network_t network;
    assert(web_server_parse_wifi_request("{\"ssid\":\"\"}", &network) ==
           WEB_SERVER_PARSE_INVALID);
    assert(web_server_parse_wifi_request("{\"ssid\":\"home\",\"password\":1}", &network) ==
           WEB_SERVER_PARSE_INVALID);
    assert(web_server_parse_wifi_request("{\"ssid\":\"home\",\"password\":\"pw\",}",
                                         &network) == WEB_SERVER_PARSE_INVALID);
}

int main(void)
{
    test_parse_accepts_ssid_and_password();
    test_parse_rejects_missing_or_empty_fields();
    puts("web_server tests passed");
    return 0;
}
