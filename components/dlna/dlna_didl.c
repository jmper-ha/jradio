#include "dlna_didl.h"

#include <string.h>

#include "dlna_xml.h"

static void copy_text(const char *body, size_t length, const char *name,
                      char *out, size_t out_size)
{
    out[0] = '\0';
    (void)dlna_xml_element_text(body, length, name, 0U, out, out_size);
}

/* "0:03:46.000" into milliseconds. Also accepts "0:03:46" and a bare seconds
 * count, both of which servers emit. Zero for anything else - a duration is a
 * nicety on the row, and refusing the whole track over it would be absurd. */
static uint32_t parse_duration_ms(const char *text)
{
    if (text == NULL || text[0] == '\0') return 0U;

    uint32_t parts[3] = {0U, 0U, 0U};
    size_t count = 0U;
    uint32_t value = 0U;
    bool any_digit = false;
    size_t index = 0U;

    for (; text[index] != '\0' && text[index] != '.'; ++index) {
        if (text[index] >= '0' && text[index] <= '9') {
            /* A duration long enough to overflow is a broken field. */
            if (value > 100000U) return 0U;
            value = value * 10U + (uint32_t)(text[index] - '0');
            any_digit = true;
        } else if (text[index] == ':') {
            if (count >= 3U) return 0U;
            parts[count++] = value;
            value = 0U;
            any_digit = false;
        } else {
            return 0U;
        }
    }
    if (!any_digit) return 0U;
    if (count >= 3U) return 0U;
    parts[count++] = value;

    uint32_t seconds = 0U;
    for (size_t part = 0U; part < count; ++part) {
        seconds = seconds * 60U + parts[part];
    }

    uint32_t milliseconds = 0U;
    if (text[index] == '.') {
        /* Fractions come as three digits here, but a server writing one or two
         * means tenths and hundredths, so the place matters. */
        uint32_t scale = 100U;
        for (size_t digit = index + 1U; text[digit] != '\0' && scale > 0U; ++digit) {
            if (text[digit] < '0' || text[digit] > '9') break;
            milliseconds += (uint32_t)(text[digit] - '0') * scale;
            scale /= 10U;
        }
    }
    return seconds * 1000U + milliseconds;
}

/* The MIME type out of a protocolInfo: the third of its four colon-separated
 * fields, as in "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;...". */
static bool protocol_mime(const char *protocol_info, char *out, size_t out_size)
{
    out[0] = '\0';
    if (protocol_info == NULL) return false;

    /* Only what a server delivers over plain HTTP. The other transports UPnP
     * allows - RTP, internal file paths - are not something this device can
     * open, and taking the MIME type off one would produce a row that looks
     * playable and is not. */
    static const char http_get[] = "http-get:";
    if (strncmp(protocol_info, http_get, sizeof(http_get) - 1U) != 0) return false;

    const char *network = protocol_info + sizeof(http_get) - 1U;
    const char *mime = strchr(network, ':');
    if (mime == NULL) return false;
    ++mime;
    const char *end = strchr(mime, ':');
    const size_t length = end != NULL ? (size_t)(end - mime) : strlen(mime);
    if (length == 0U || length + 1U > out_size) return false;
    memcpy(out, mime, length);
    out[length] = '\0';
    return true;
}

/* Picks the resource to play out of an item.
 *
 * An item can carry several: a server may offer the original file and a
 * transcode beside it, and only some of them are formats this build decodes.
 * The first one this device can open wins, which on the measured server is the
 * original file - the one that costs the server nothing to serve. */
static bool select_resource(const char *body, size_t length, dlna_entry_t *entry)
{
    dlna_xml_element_t resource;
    size_t from = 0U;
    while (dlna_xml_find_element(body, length, "res", from, &resource)) {
        from = resource.next;

        char protocol_info[160];
        if (!dlna_xml_attribute(resource.attributes, "protocolInfo",
                                protocol_info, sizeof(protocol_info))) {
            continue;
        }
        char mime[64];
        if (!protocol_mime(protocol_info, mime, sizeof(mime))) continue;

        radio_stream_format_t format;
        if (!radio_stream_format_from_content_type(mime, &format)) continue;

        if (resource.body.length == 0U || resource.body.length + 1U > sizeof(entry->url)) {
            continue;
        }
        (void)dlna_xml_unescape(resource.body.data, resource.body.length,
                                entry->url, sizeof(entry->url));
        if (entry->url[0] == '\0') continue;

        entry->format = format;
        /* The duration hangs off the resource, not the item, because two
         * resources of one item can differ in length. */
        char duration[24];
        if (dlna_xml_attribute(resource.attributes, "duration", duration, sizeof(duration))) {
            entry->duration_ms = parse_duration_ms(duration);
        }
        return true;
    }
    return false;
}

static void read_entry(const char *body, size_t length, dlna_xml_slice_t attributes,
                       dlna_entry_kind_t kind, dlna_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    entry->kind = kind;

    (void)dlna_xml_attribute(attributes, "id", entry->id, sizeof(entry->id));
    copy_text(body, length, "title", entry->title, sizeof(entry->title));
    copy_text(body, length, "artist", entry->artist, sizeof(entry->artist));
    copy_text(body, length, "album", entry->album, sizeof(entry->album));
    /* The picture, and the smaller of the two the server offers.
     *
     * upnp:albumArtURI is the standard field and is meant to be shown at size:
     * the one on this LAN is a 70 KB JPEG_MED. upnp:icon beside it is the same
     * picture at 21 KB. The panel's tile is 160 px, so the large one is bytes
     * fetched to be thrown away - and a cover has to fit in a buffer the
     * device can spare while a track is starting. Servers that send only the
     * standard field still work; they just cost more. */
    copy_text(body, length, "icon", entry->art_url, sizeof(entry->art_url));
    if (entry->art_url[0] == '\0') {
        copy_text(body, length, "albumArtURI", entry->art_url, sizeof(entry->art_url));
    }

    /* dc:creator where there is no upnp:artist: the same answer under the
     * older name, and some servers send only that one. */
    if (entry->artist[0] == '\0') {
        copy_text(body, length, "creator", entry->artist, sizeof(entry->artist));
    }

    if (kind != DLNA_ENTRY_ITEM) return;

    /* A video or a photo has a URL and a duration and everything else a track
     * has, so what separates them is the class the server gives it. Without
     * this check the Video folder lists as a set of playable rows. */
    char class_name[64];
    copy_text(body, length, "class", class_name, sizeof(class_name));
    if (strstr(class_name, "audioItem") == NULL) return;

    entry->playable = select_resource(body, length, entry);
}

size_t dlna_didl_parse(const char *didl, size_t length, dlna_entry_t *entries,
                       size_t capacity, size_t *total_seen)
{
    if (total_seen != NULL) *total_seen = 0U;
    if (didl == NULL || length == 0U) return 0U;

    size_t written = 0U;
    size_t seen = 0U;
    size_t from = 0U;

    for (;;) {
        dlna_xml_element_t container;
        dlna_xml_element_t item;
        const bool has_container =
            dlna_xml_find_element(didl, length, "container", from, &container);
        const bool has_item = dlna_xml_find_element(didl, length, "item", from, &item);
        if (!has_container && !has_item) break;

        /* Whichever the server wrote first. The two kinds are interleaved on
         * some servers - a folder holding both sub-folders and loose tracks -
         * and reading all of one kind and then all of the other would show
         * them in an order the server never used. */
        const bool take_container =
            has_container && (!has_item || container.start < item.start);
        const dlna_xml_element_t *element = take_container ? &container : &item;
        const dlna_entry_kind_t kind =
            take_container ? DLNA_ENTRY_CONTAINER : DLNA_ENTRY_ITEM;

        /* The id is read before anything else and decides whether this is a
         * row at all, so that what `total_seen` counts does not depend on how
         * much room the caller had. One byte of slack catches an id too long
         * to keep: a truncated one names a different object, or none, and a
         * row that browses somewhere else is worse than a missing row. */
        char id[DLNA_OBJECT_ID_MAX + 1U];
        if (!dlna_xml_attribute(element->attributes, "id", id, sizeof(id)) ||
            id[0] == '\0' || strlen(id) >= DLNA_OBJECT_ID_MAX) {
            from = element->next;
            continue;
        }

        ++seen;
        if (written < capacity && entries != NULL) {
            read_entry(element->body.data, element->body.length, element->attributes,
                       kind, &entries[written]);
            ++written;
        }
        from = element->next;
    }

    if (total_seen != NULL) *total_seen = seen;
    return written;
}
