#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dlna_device.h"
#include "dlna_ssdp.h"
#include "dlna_url.h"

/* Discovery, tested against what the media server on this LAN actually put on
   the wire on 2026-09-06 - a Plex Media Server behind a Platinum UPnP stack.
   The datagram and the description below are its bytes, trimmed only of the
   icon list, because a client written against the specification and never
   against a server is a client that works on paper. */

static const char k_search_reply[] =
    "HTTP/1.1 200 OK\r\n"
    "Location: http://192.168.1.50:32469/DeviceDescription.xml\r\n"
    "Cache-Control: max-age=1800\r\n"
    "Server: UPnP/1.0 DLNADOC/1.50 Platinum/1.0.5.13\r\n"
    "EXT: \r\n"
    "BOOTID.UPNP.ORG: 1788698730\r\n"
    "CONFIGID.UPNP.ORG: 6442036\r\n"
    "USN: uuid:a91a1edd-868d-8743-34d7-7ead6bfee64c::"
    "urn:schemas-upnp-org:device:MediaServer:1\r\n"
    "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
    "Date: Sun, 06 Sep 2026 15:05:36 GMT\r\n"
    "\r\n";

static const char k_description[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<root configId=\"6442036\" xmlns=\"urn:schemas-upnp-org:device-1-0\">"
    "<specVersion><major>1</major><minor>1</minor></specVersion>"
    "<device>"
    "<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>"
    "<friendlyName>Plex Media Server: nas4free</friendlyName>"
    "<manufacturer>Plex, Inc.</manufacturer>"
    "<modelName>Plex Media Server</modelName>"
    "<UDN>uuid:a91a1edd-868d-8743-34d7-7ead6bfee64c</UDN>"
    "<serviceList>"
    "<service>"
    "<serviceType>urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1</serviceType>"
    "<serviceId>urn:microsoft.com:serviceId:X_MS_MediaReceiverRegistrar</serviceId>"
    "<SCPDURL>/X_MS_MediaReceiverRegistrar/a91a1edd/scpd.xml</SCPDURL>"
    "<controlURL>/X_MS_MediaReceiverRegistrar/a91a1edd/control.xml</controlURL>"
    "</service>"
    "<service>"
    "<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
    "<serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>"
    "<SCPDURL>/ContentDirectory/a91a1edd-868d-8743-34d7-7ead6bfee64c/scpd.xml</SCPDURL>"
    "<controlURL>/ContentDirectory/a91a1edd-868d-8743-34d7-7ead6bfee64c/control.xml</controlURL>"
    "</service>"
    "<service>"
    "<serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
    "<controlURL>/ConnectionManager/a91a1edd/control.xml</controlURL>"
    "</service>"
    "</serviceList>"
    "</device>"
    "</root>";

static void test_the_search_is_the_one_that_got_answers(void)
{
    char datagram[256];
    const size_t length = dlna_ssdp_build_search(datagram, sizeof(datagram), 2U);
    assert(length > 0U);
    assert(length == strlen(datagram));

    /* Every line the server answered to. MAN must carry its quotes and the
       datagram must end on a blank line, or a strict stack ignores it. */
    assert(strstr(datagram, "M-SEARCH * HTTP/1.1\r\n") == datagram);
    assert(strstr(datagram, "HOST: 239.255.255.250:1900\r\n") != NULL);
    assert(strstr(datagram, "MAN: \"ssdp:discover\"\r\n") != NULL);
    assert(strstr(datagram, "MX: 2\r\n") != NULL);
    assert(strstr(datagram, "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n") != NULL);
    assert(strcmp(datagram + length - 4, "\r\n\r\n") == 0);

    /* No room is no datagram, rather than half a request. */
    char tiny[16];
    assert(dlna_ssdp_build_search(tiny, sizeof(tiny), 2U) == 0U);
    assert(tiny[0] == '\0');
}

static void test_it_reads_the_reply_this_server_sent(void)
{
    dlna_ssdp_response_t response;
    assert(dlna_ssdp_parse_response(k_search_reply, strlen(k_search_reply), &response));
    assert(strcmp(response.location, "http://192.168.1.50:32469/DeviceDescription.xml") == 0);
    /* The USN is what tells one server from two answers by the same server:
       this one replied twice to a single search. */
    assert(strstr(response.usn, "uuid:a91a1edd-868d-8743-34d7-7ead6bfee64c") == response.usn);
}

static void test_it_takes_headers_however_they_are_spelled(void)
{
    /* Same datagram, the case a different stack would use, and the lone LF
       some of them send. */
    const char shouted[] =
        "HTTP/1.1 200 OK\n"
        "LOCATION:http://192.168.1.9:8200/rootDesc.xml\n"
        "st: urn:schemas-upnp-org:device:MediaServer:1\n"
        "usn: uuid:4d696e69-444c-164e-9d41-3c7c3f1b2d55::"
        "urn:schemas-upnp-org:device:MediaServer:1\n"
        "\n";
    dlna_ssdp_response_t response;
    assert(dlna_ssdp_parse_response(shouted, strlen(shouted), &response));
    assert(strcmp(response.location, "http://192.168.1.9:8200/rootDesc.xml") == 0);
}

static void test_it_refuses_what_is_not_a_media_server(void)
{
    dlna_ssdp_response_t response;

    /* The rest of a home network: a printer, a router, a light. Each of them
       would otherwise become a row on the screen with nothing behind it. */
    const char printer[] =
        "HTTP/1.1 200 OK\r\n"
        "Location: http://192.168.1.7:80/ipp.xml\r\n"
        "ST: urn:schemas-upnp-org:device:Printer:1\r\n"
        "USN: uuid:printer::urn:schemas-upnp-org:device:Printer:1\r\n"
        "\r\n";
    assert(!dlna_ssdp_parse_response(printer, strlen(printer), &response));

    /* A root-device announcement says a UPnP device is there, not that it
       serves media. */
    const char root_device[] =
        "NOTIFY * HTTP/1.1\r\n"
        "Location: http://192.168.1.50:32469/DeviceDescription.xml\r\n"
        "NT: upnp:rootdevice\r\n"
        "NTS: ssdp:alive\r\n"
        "\r\n";
    assert(!dlna_ssdp_parse_response(root_device, strlen(root_device), &response));

    /* Announcing itself unasked is as good as answering, though. */
    const char alive[] =
        "NOTIFY * HTTP/1.1\r\n"
        "Location: http://192.168.1.50:32469/DeviceDescription.xml\r\n"
        "NT: urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "NTS: ssdp:alive\r\n"
        "USN: uuid:a91a1edd::urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "\r\n";
    assert(dlna_ssdp_parse_response(alive, strlen(alive), &response));

    /* And a server leaving repeats the address it used to be at. Taking it
       would put back the row that is going away. */
    const char byebye[] =
        "NOTIFY * HTTP/1.1\r\n"
        "Location: http://192.168.1.50:32469/DeviceDescription.xml\r\n"
        "NT: urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "NTS: ssdp:byebye\r\n"
        "\r\n";
    assert(!dlna_ssdp_parse_response(byebye, strlen(byebye), &response));

    /* A media server that says where it is not: nothing to fetch. */
    const char no_location[] =
        "HTTP/1.1 200 OK\r\n"
        "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "\r\n";
    assert(!dlna_ssdp_parse_response(no_location, strlen(no_location), &response));
    assert(response.location[0] == '\0');

    /* Rubbish off the multicast group, at every length, reading past nothing.
       The sanitizer decides whether this passed. */
    for (size_t length = 0U; length <= strlen(k_search_reply); ++length) {
        (void)dlna_ssdp_parse_response(k_search_reply, length, &response);
    }
    assert(!dlna_ssdp_parse_response(NULL, 10U, &response));
    assert(!dlna_ssdp_parse_response("", 0U, &response));
}

static void test_it_joins_a_relative_service_url_to_where_it_came_from(void)
{
    char out[DLNA_URL_MAX];
    const char base[] = "http://192.168.1.50:32469/DeviceDescription.xml";

    /* The shape this server sends: rooted at the host. */
    assert(dlna_url_resolve(base, "/ContentDirectory/x/control.xml", out, sizeof(out)));
    assert(strcmp(out, "http://192.168.1.50:32469/ContentDirectory/x/control.xml") == 0);

    /* Relative to the description's own directory. */
    assert(dlna_url_resolve("http://192.168.1.9:8200/desc/root.xml", "ctl.xml",
                            out, sizeof(out)));
    assert(strcmp(out, "http://192.168.1.9:8200/desc/ctl.xml") == 0);

    /* Already absolute: left alone, port and all. */
    assert(dlna_url_resolve(base, "http://10.0.0.2:80/c.xml", out, sizeof(out)));
    assert(strcmp(out, "http://10.0.0.2:80/c.xml") == 0);

    /* A base with no path at all still resolves to something fetchable. */
    assert(dlna_url_resolve("http://192.168.1.50:32469", "ctl.xml", out, sizeof(out)));
    assert(strcmp(out, "http://192.168.1.50:32469/ctl.xml") == 0);
    assert(dlna_url_resolve("http://192.168.1.50:32469", "/ctl.xml", out, sizeof(out)));
    assert(strcmp(out, "http://192.168.1.50:32469/ctl.xml") == 0);

    /* No host to hang it off, and no room for the answer: both refused rather
       than half a URL, which would be a request to somewhere else. */
    assert(!dlna_url_resolve("not a url", "/ctl.xml", out, sizeof(out)));
    char small[16];
    assert(!dlna_url_resolve(base, "/ContentDirectory/x/control.xml", small, sizeof(small)));
    assert(small[0] == '\0');
    assert(!dlna_url_resolve(NULL, "/x", out, sizeof(out)));
    assert(!dlna_url_resolve(base, NULL, out, sizeof(out)));
}

static void test_it_finds_the_service_that_answers_browse(void)
{
    dlna_device_t device;
    assert(dlna_device_parse_description(k_description, strlen(k_description),
                                         "http://192.168.1.50:32469/DeviceDescription.xml",
                                         &device));
    assert(strcmp(device.friendly_name, "Plex Media Server: nas4free") == 0);
    /* The ContentDirectory's control URL, not the Microsoft registrar's, which
       is listed first and has a controlURL of its own. */
    assert(strcmp(device.control_url,
                  "http://192.168.1.50:32469/ContentDirectory/"
                  "a91a1edd-868d-8743-34d7-7ead6bfee64c/control.xml") == 0);
}

static void test_a_later_content_directory_version_is_still_usable(void)
{
    /* Browse is unchanged in ContentDirectory:2 and :3 - the later versions
       only add actions - so refusing them would lock the device out of servers
       it can talk to perfectly well. */
    const char xml[] =
        "<root><device><friendlyName>Newer</friendlyName><serviceList><service>"
        "<serviceType>urn:schemas-upnp-org:service:ContentDirectory:3</serviceType>"
        "<controlURL>/cd/control</controlURL>"
        "</service></serviceList></device></root>";
    dlna_device_t device;
    assert(dlna_device_parse_description(xml, strlen(xml), "http://host:80/d.xml", &device));
    assert(strcmp(device.control_url, "http://host:80/cd/control") == 0);
}

static void test_a_url_base_overrides_where_it_was_fetched_from(void)
{
    /* Some stacks say which base their relative URLs hang off, and a server
       that says so means it: ignoring it addresses every service on the wrong
       port. */
    const char xml[] =
        "<root><URLBase>http://192.168.1.50:8895/</URLBase><device>"
        "<friendlyName>Rebased</friendlyName><serviceList><service>"
        "<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
        "<controlURL>/cd/control</controlURL>"
        "</service></serviceList></device></root>";
    dlna_device_t device;
    assert(dlna_device_parse_description(xml, strlen(xml), "http://192.168.1.50:32469/d.xml",
                                         &device));
    assert(strcmp(device.control_url, "http://192.168.1.50:8895/cd/control") == 0);
}

static void test_a_server_with_nothing_to_browse_is_refused(void)
{
    /* It parses, it has a name, and there is no way to ask it for anything.
       Saying so here keeps that case out of every caller. */
    const char xml[] =
        "<root><device><friendlyName>Renderer only</friendlyName><serviceList><service>"
        "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
        "<controlURL>/av/control</controlURL>"
        "</service></serviceList></device></root>";
    dlna_device_t device;
    assert(!dlna_device_parse_description(xml, strlen(xml), "http://host:80/d.xml", &device));
    assert(device.control_url[0] == '\0');
    /* The name still comes back, so a caller can say which server it was. */
    assert(strcmp(device.friendly_name, "Renderer only") == 0);

    /* And every prefix of a real description, for the short read. */
    dlna_device_t ignored;
    for (size_t length = 0U; length <= strlen(k_description); ++length) {
        (void)dlna_device_parse_description(k_description, length,
                                            "http://192.168.1.50:32469/d.xml", &ignored);
    }
    assert(!dlna_device_parse_description(NULL, 10U, "http://h/d", &ignored));
    assert(!dlna_device_parse_description(k_description, strlen(k_description), NULL, &ignored));
}

int main(void)
{
    test_the_search_is_the_one_that_got_answers();
    test_it_reads_the_reply_this_server_sent();
    test_it_takes_headers_however_they_are_spelled();
    test_it_refuses_what_is_not_a_media_server();
    test_it_joins_a_relative_service_url_to_where_it_came_from();
    test_it_finds_the_service_that_answers_browse();
    test_a_later_content_directory_version_is_still_usable();
    test_a_url_base_overrides_where_it_was_fetched_from();
    test_a_server_with_nothing_to_browse_is_refused();
    puts("dlna_discovery tests passed");
    return 0;
}
