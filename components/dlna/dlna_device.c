#include "dlna_device.h"

#include <string.h>

#include "dlna_xml.h"

/* The service that answers Browse. Matched by prefix rather than in full so a
 * server offering ContentDirectory:2 or :3 is usable: the later versions add
 * actions, and the Browse this device sends is unchanged in all of them. */
static const char k_content_directory[] = "urn:schemas-upnp-org:service:ContentDirectory:";

bool dlna_device_parse_description(const char *xml, size_t length, const char *base_url,
                                   dlna_device_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (xml == NULL || base_url == NULL || length == 0U) return false;

    /* A description may name the base its relative URLs hang off, overriding
     * where the document was fetched from. Rare - the server on this LAN sends
     * no URLBase - but a server that does send one means it, and ignoring it
     * would address every service on the wrong port. */
    char base[DLNA_URL_MAX];
    if (!dlna_xml_element_text(xml, length, "URLBase", 0U, base, sizeof(base)) ||
        base[0] == '\0') {
        if (strlen(base_url) + 1U > sizeof(base)) return false;
        memcpy(base, base_url, strlen(base_url) + 1U);
    }

    /* The first friendlyName is the root device's. Sub-devices carry their own
     * further down, and a server that embeds one would otherwise be named
     * after its own component. */
    (void)dlna_xml_element_text(xml, length, "friendlyName", 0U,
                                out->friendly_name, sizeof(out->friendly_name));

    dlna_xml_element_t service;
    size_t from = 0U;
    while (dlna_xml_find_element(xml, length, "service", from, &service)) {
        from = service.next;

        char type[128];
        if (!dlna_xml_element_text(service.body.data, service.body.length, "serviceType",
                                   0U, type, sizeof(type))) {
            continue;
        }
        if (strncmp(type, k_content_directory, sizeof(k_content_directory) - 1U) != 0) {
            continue;
        }

        /* Searched inside this service only. A description lists three or four
         * services and every one of them has a controlURL; taking the first in
         * the document would send Browse to the connection manager. */
        char reference[DLNA_URL_MAX];
        if (!dlna_xml_element_text(service.body.data, service.body.length, "controlURL",
                                   0U, reference, sizeof(reference))) {
            continue;
        }
        if (!dlna_url_resolve(base, reference, out->control_url, sizeof(out->control_url))) {
            continue;
        }
        return true;
    }

    /* Nothing to browse. The name found above goes back too, so a caller can
     * say which server it was that could not be used. */
    out->control_url[0] = '\0';
    return false;
}
