#include "dlna_ssdp.h"

#include <stdio.h>
#include <string.h>

static bool equal_ignoring_case(const char *a, const char *b, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        char left = a[index];
        char right = b[index];
        if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if (left != right) return false;
    }
    return true;
}

/* Finds one header's value in an HTTP-shaped datagram.
 *
 * Header names are matched without regard to case because SSDP is written by
 * hand in a hundred different stacks: the server here sends "Location", the
 * specification writes "LOCATION", and both are the same header. */
static bool header_value(const char *data, size_t length, const char *name,
                         const char **value, size_t *value_length)
{
    const size_t name_length = strlen(name);
    size_t index = 0U;

    /* From the second line: the first is the request or status line. */
    while (index < length && data[index] != '\n') ++index;
    if (index < length) ++index;

    while (index < length) {
        size_t end = index;
        while (end < length && data[end] != '\n') ++end;
        size_t line_end = end;
        if (line_end > index && data[line_end - 1U] == '\r') --line_end;

        if (line_end - index > name_length &&
            equal_ignoring_case(data + index, name, name_length) &&
            data[index + name_length] == ':') {
            size_t start = index + name_length + 1U;
            while (start < line_end && (data[start] == ' ' || data[start] == '\t')) ++start;
            while (line_end > start &&
                   (data[line_end - 1U] == ' ' || data[line_end - 1U] == '\t')) {
                --line_end;
            }
            *value = data + start;
            *value_length = line_end - start;
            return true;
        }
        index = end < length ? end + 1U : length;
    }
    return false;
}

static bool copy_value(const char *value, size_t length, char *out, size_t out_size)
{
    if (length == 0U || length + 1U > out_size) return false;
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

static bool mentions_media_server(const char *data, size_t length)
{
    /* A reply to a search carries ST; a server announcing itself carries NT.
     * Either one naming a MediaServer is the device we are looking for. A root
     * device announcement is not enough: it says a UPnP device is there, not
     * that it serves media. */
    static const char *const fields[] = {"ST", "NT"};
    for (size_t index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        const char *value = NULL;
        size_t value_length = 0U;
        if (!header_value(data, length, fields[index], &value, &value_length)) continue;
        const size_t target = strlen(DLNA_SSDP_SEARCH_TARGET);
        if (value_length == target && memcmp(value, DLNA_SSDP_SEARCH_TARGET, target) == 0) {
            return true;
        }
    }
    return false;
}

size_t dlna_ssdp_build_search(char *out, size_t out_size, unsigned mx_seconds)
{
    if (out == NULL || out_size == 0U) return 0U;
    const int written = snprintf(out, out_size,
                                 "M-SEARCH * HTTP/1.1\r\n"
                                 "HOST: %s:%u\r\n"
                                 "MAN: \"ssdp:discover\"\r\n"
                                 "MX: %u\r\n"
                                 "ST: %s\r\n"
                                 "\r\n",
                                 DLNA_SSDP_MULTICAST_ADDRESS, (unsigned)DLNA_SSDP_PORT,
                                 mx_seconds, DLNA_SSDP_SEARCH_TARGET);
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

bool dlna_ssdp_parse_response(const char *data, size_t length, dlna_ssdp_response_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (data == NULL || length == 0U) return false;

    /* A server on its way out repeats the location it used to be at. Taking it
     * would put a row on the screen that answers nothing. */
    const char *nts = NULL;
    size_t nts_length = 0U;
    if (header_value(data, length, "NTS", &nts, &nts_length)) {
        static const char byebye[] = "ssdp:byebye";
        if (nts_length == sizeof(byebye) - 1U && memcmp(nts, byebye, nts_length) == 0) {
            return false;
        }
    }

    if (!mentions_media_server(data, length)) return false;

    const char *location = NULL;
    size_t location_length = 0U;
    if (!header_value(data, length, "Location", &location, &location_length)) return false;
    if (!copy_value(location, location_length, out->location, sizeof(out->location))) {
        return false;
    }

    const char *usn = NULL;
    size_t usn_length = 0U;
    if (header_value(data, length, "USN", &usn, &usn_length)) {
        /* Too long to keep is not a reason to drop the server; it only means
         * the caller has a weaker key to de-duplicate on, and the location
         * below is a serviceable second one. */
        (void)copy_value(usn, usn_length, out->usn, sizeof(out->usn));
    }
    return true;
}
