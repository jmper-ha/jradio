#include "dlna_soap.h"

#include <stdio.h>
#include <string.h>

#include "dlna_xml.h"

size_t dlna_soap_build_browse(char *out, size_t out_size, const char *object_id,
                              size_t starting_index, size_t requested_count)
{
    if (out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    if (object_id == NULL) return 0U;

    /* Object ids are the server's to choose and are opaque to us. The ones
     * here are hex strings and UUIDs, but nothing promises that, and an id
     * carrying an '&' would otherwise end the envelope early. */
    char escaped[192];
    if (dlna_xml_escape(object_id, escaped, sizeof(escaped)) == 0U && object_id[0] != '\0') {
        return 0U;
    }

    const int written = snprintf(
        out, out_size,
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
        " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>%s</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        /* Everything the server has on each object. Naming fields instead
         * would save a little bandwidth and cost the album art, which several
         * stacks omit unless it is asked for by name - and "*" is what every
         * other client sends, so it is the path servers are tested on. */
        "<Filter>*</Filter>"
        "<StartingIndex>%u</StartingIndex>"
        "<RequestedCount>%u</RequestedCount>"
        "<SortCriteria></SortCriteria>"
        "</u:Browse>"
        "</s:Body>"
        "</s:Envelope>",
        escaped, (unsigned)starting_index, (unsigned)requested_count);

    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

/* Reads a decimal number out of an element. Absent or unreadable comes back as
 * 0, which is the honest answer for a count the server did not give. */
static size_t element_count(const char *xml, size_t length, const char *name)
{
    char text[24];
    if (!dlna_xml_element_text(xml, length, name, 0U, text, sizeof(text))) return 0U;
    size_t value = 0U;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') return 0U;
        value = value * 10U + (size_t)(text[index] - '0');
    }
    return value;
}

bool dlna_soap_parse_browse(const char *xml, size_t length, dlna_soap_browse_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (xml == NULL || length == 0U) return false;

    dlna_xml_element_t result;
    if (!dlna_xml_find_element(xml, length, "Result", 0U, &result)) return false;

    out->result_offset = (size_t)(result.body.data - xml);
    out->result_length = result.body.length;
    out->number_returned = element_count(xml, length, "NumberReturned");
    out->total_matches = element_count(xml, length, "TotalMatches");

    /* A server that returns entries without saying how many there are in all
     * still has to be pageable, and the honest floor is what we have seen. */
    if (out->total_matches < out->number_returned) {
        out->total_matches = out->number_returned;
    }
    return true;
}

int dlna_soap_fault_code(const char *xml, size_t length)
{
    if (xml == NULL || length == 0U) return 0;
    dlna_xml_element_t fault;
    if (!dlna_xml_find_element(xml, length, "Fault", 0U, &fault)) return 0;

    /* The UPnP code lives in the fault's detail, under a name of its own. The
     * SOAP faultcode above it says only "Client", which is true of every one
     * of them and tells the log nothing. */
    char text[16];
    if (!dlna_xml_element_text(xml, length, "errorCode", 0U, text, sizeof(text))) return -1;
    int value = 0;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') return -1;
        value = value * 10 + (text[index] - '0');
    }
    return value != 0 ? value : -1;
}
