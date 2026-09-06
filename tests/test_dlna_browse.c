#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dlna_didl.h"
#include "dlna_soap.h"
#include "dlna_xml.h"

/* Browsing, tested against the bytes the media server on this LAN returned on
   2026-09-06. k_album_response below is one Browse reply copied off the wire
   unaltered, escaping and all - the point being that a listing arrives as a
   whole XML document escaped inside a field of another one, and that two-pass
   shape is the thing most likely to be got wrong.

   The rest are shapes other servers produce, which this device will meet the
   first time it is pointed at something that is not Plex. */

/* Browse of the album "KISS - Alive!", asking for the first two of its 16
   tracks. Verbatim. */
static const char k_album_response[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<s:Envelope s:encodingStyle=\"http://schemas."
    "xmlsoap.org/soap/encoding/\" xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
    "<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;D"
    "IDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/"
    "dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns:dlna=\"urn:"
    "schemas-dlna-org:metadata-1-0/\"&gt;&lt;item id=\"9901821ed5c66ed435f7\" parentID=\"92914b5"
    "b4d0889307de9\" restricted=\"1\"&gt;&lt;dc:title&gt;Deuce&lt;/dc:title&gt;&lt;dc:creator&gt"
    ";KISS&lt;/dc:creator&gt;&lt;dc:date&gt;2007-01-01&lt;/dc:date&gt;&lt;upnp:artist&gt;KISS&lt"
    ";/upnp:artist&gt;&lt;upnp:album&gt;Alive!&lt;/upnp:album&gt;&lt;upnp:genre&gt;Unknown&lt;/u"
    "pnp:genre&gt;&lt;upnp:albumArtURI dlna:profileID=\"JPEG_MED\"&gt;http://192.168.1.50:32469/"
    "proxy/180c041acf8643614a76/albumart.jpg&lt;/upnp:albumArtURI&gt;&lt;dc:description&gt;Deuce"
    "&lt;/dc:description&gt;&lt;upnp:icon&gt;http://192.168.1.50:32469/proxy/9d919a35316c1254594"
    "c/icon.jpg&lt;/upnp:icon&gt;&lt;upnp:originalTrackNumber&gt;1&lt;/upnp:originalTrackNumber&"
    "gt;&lt;res duration=\"0:03:46.000\" bitrate=\"40000\" sampleFrequency=\"44100\" nrAudioChan"
    "nels=\"2\" protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI="
    "0;DLNA.ORG_FLAGS=01500000000000000000000000000000\"&gt;http://192.168.1.50:32469/object/990"
    "1821ed5c66ed435f7/file.mp3&lt;/res&gt;&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt"
    ";/upnp:class&gt;&lt;/item&gt;&lt;item id=\"28e2ff68767123adf9bb\" parentID=\"92914b5b4d0889"
    "307de9\" restricted=\"1\"&gt;&lt;dc:title&gt;Strutter&lt;/dc:title&gt;&lt;dc:creator&gt;KIS"
    "S&lt;/dc:creator&gt;&lt;dc:date&gt;2007-01-01&lt;/dc:date&gt;&lt;upnp:artist&gt;KISS&lt;/up"
    "np:artist&gt;&lt;upnp:album&gt;Alive!&lt;/upnp:album&gt;&lt;upnp:genre&gt;Unknown&lt;/upnp:"
    "genre&gt;&lt;upnp:albumArtURI dlna:profileID=\"JPEG_MED\"&gt;http://192.168.1.50:32469/prox"
    "y/180c041acf8643614a76/albumart.jpg&lt;/upnp:albumArtURI&gt;&lt;dc:description&gt;Strutter&"
    "lt;/dc:description&gt;&lt;upnp:icon&gt;http://192.168.1.50:32469/proxy/9d919a35316c1254594c"
    "/icon.jpg&lt;/upnp:icon&gt;&lt;upnp:originalTrackNumber&gt;2&lt;/upnp:originalTrackNumber&g"
    "t;&lt;res duration=\"0:03:27.000\" bitrate=\"40000\" sampleFrequency=\"44100\" nrAudioChann"
    "els=\"2\" protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0"
    ";DLNA.ORG_FLAGS=01500000000000000000000000000000\"&gt;http://192.168.1.50:32469/object/28e2"
    "ff68767123adf9bb/file.mp3&lt;/res&gt;&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;"
    "/upnp:class&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>2</NumberReturned><T"
    "otalMatches>16</TotalMatches><UpdateID>89435398</UpdateID></u:BrowseResponse></s:Body></s:E"
    "nvelope>";

/* Reads a reply the way the device does: unwrap, unescape in place, parse. The
   in-place step is not an optimisation detail worth hiding - it is what lets a
   4 KB browse work with one buffer instead of two - so the test does it too. */
static size_t browse(const char *response, dlna_entry_t *entries, size_t capacity,
                     dlna_soap_browse_t *envelope, size_t *total_seen)
{
    static char buffer[8192];
    const size_t length = strlen(response);
    assert(length < sizeof(buffer));
    memcpy(buffer, response, length + 1U);

    if (!dlna_soap_parse_browse(buffer, length, envelope)) return 0U;
    char *payload = buffer + envelope->result_offset;
    const size_t unescaped =
        dlna_xml_unescape(payload, envelope->result_length, payload,
                          envelope->result_length + 1U);
    return dlna_didl_parse(payload, unescaped, entries, capacity, total_seen);
}

static void test_it_asks_for_what_this_server_answered(void)
{
    char body[1024];
    const size_t length = dlna_soap_build_browse(body, sizeof(body), "0", 0U, 25U);
    assert(length > 0U);
    assert(length == strlen(body));

    assert(strstr(body, "<ObjectID>0</ObjectID>") != NULL);
    assert(strstr(body, "<BrowseFlag>BrowseDirectChildren</BrowseFlag>") != NULL);
    assert(strstr(body, "<Filter>*</Filter>") != NULL);
    assert(strstr(body, "<StartingIndex>0</StartingIndex>") != NULL);
    assert(strstr(body, "<RequestedCount>25</RequestedCount>") != NULL);
    /* The action's namespace has to be the one in the SOAPAction header, or
       the server answers with a fault instead of a listing. */
    assert(strstr(body, "urn:schemas-upnp-org:service:ContentDirectory:1") != NULL);
    assert(strstr(DLNA_SOAP_ACTION_BROWSE,
                  "urn:schemas-upnp-org:service:ContentDirectory:1#Browse") != NULL);

    /* Paging: the second request differs only in where it starts. */
    assert(dlna_soap_build_browse(body, sizeof(body), "abe6121c", 25U, 25U) > 0U);
    assert(strstr(body, "<ObjectID>abe6121c</ObjectID>") != NULL);
    assert(strstr(body, "<StartingIndex>25</StartingIndex>") != NULL);

    /* An id is the server's to invent and opaque to us, so one carrying an
       ampersand must not be able to end the envelope early. */
    assert(dlna_soap_build_browse(body, sizeof(body), "a&b<c>", 0U, 25U) > 0U);
    assert(strstr(body, "<ObjectID>a&amp;b&lt;c&gt;</ObjectID>") != NULL);

    /* No room is no request: half an envelope is one the server rejects, and
       the caller has to be able to tell that from success. */
    char small[64];
    assert(dlna_soap_build_browse(small, sizeof(small), "0", 0U, 25U) == 0U);
    assert(small[0] == '\0');
    assert(dlna_soap_build_browse(body, sizeof(body), NULL, 0U, 25U) == 0U);
}

static void test_it_reads_the_album_this_server_sent(void)
{
    dlna_entry_t entries[8];
    dlna_soap_browse_t envelope;
    size_t seen = 0U;
    const size_t count = browse(k_album_response, entries, 8U, &envelope, &seen);

    assert(count == 2U);
    assert(seen == 2U);
    /* Two came back, sixteen exist: this is how the caller knows to ask again
       rather than believing the album is two tracks long. */
    assert(envelope.number_returned == 2U);
    assert(envelope.total_matches == 16U);

    assert(entries[0].kind == DLNA_ENTRY_ITEM);
    assert(strcmp(entries[0].id, "9901821ed5c66ed435f7") == 0);
    assert(strcmp(entries[0].title, "Deuce") == 0);
    assert(strcmp(entries[0].artist, "KISS") == 0);
    assert(strcmp(entries[0].album, "Alive!") == 0);
    assert(entries[0].playable);
    assert(entries[0].format == RADIO_STREAM_FORMAT_MP3);
    assert(strcmp(entries[0].url,
                  "http://192.168.1.50:32469/object/9901821ed5c66ed435f7/file.mp3") == 0);
    /* The smaller of the two pictures this server offers for the track.
       upnp:albumArtURI beside it is the same image as a 70 KB JPEG_MED, and
       upnp:icon is 21 KB - both measured. The panel's tile is 160 px, so the
       large one is bytes fetched to be thrown away, and the cover has to fit a
       buffer the device can spare while a track is starting. */
    assert(strcmp(entries[0].art_url,
                  "http://192.168.1.50:32469/proxy/9d919a35316c1254594c/icon.jpg") == 0);
    /* 3:46, off the resource rather than the item. */
    assert(entries[0].duration_ms == 226000U);

    assert(strcmp(entries[1].title, "Strutter") == 0);
    assert(entries[1].duration_ms == 207000U);
    assert(entries[1].playable);
}

static void test_the_order_is_the_servers(void)
{
    /* A folder holding sub-folders and loose tracks together. Reading all the
       containers and then all the items would show them in an order the server
       never used, and for an album that order is the track order. */
    const char didl[] =
        "<DIDL-Lite>"
        "<item id=\"i1\"><dc:title>One</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://h/1.mp3</res></item>"
        "<container id=\"c1\"><dc:title>Folder</dc:title>"
        "<upnp:class>object.container.storageFolder</upnp:class></container>"
        "<item id=\"i2\"><dc:title>Two</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://h/2.mp3</res></item>"
        "</DIDL-Lite>";
    dlna_entry_t entries[4];
    size_t seen = 0U;
    assert(dlna_didl_parse(didl, strlen(didl), entries, 4U, &seen) == 3U);
    assert(seen == 3U);
    assert(strcmp(entries[0].title, "One") == 0);
    assert(entries[1].kind == DLNA_ENTRY_CONTAINER);
    assert(strcmp(entries[1].title, "Folder") == 0);
    assert(strcmp(entries[2].title, "Two") == 0);
}

static void test_a_container_is_something_to_open_not_to_play(void)
{
    const char didl[] =
        "<DIDL-Lite><container id=\"abe6121c-1731-4683-815c-89e1dcd2bf11\" parentID=\"0\">"
        "<dc:title>Music</dc:title><dc:creator>Unknown</dc:creator>"
        "<upnp:class>object.container.storageFolder</upnp:class></container></DIDL-Lite>";
    dlna_entry_t entry;
    assert(dlna_didl_parse(didl, strlen(didl), &entry, 1U, NULL) == 1U);
    assert(entry.kind == DLNA_ENTRY_CONTAINER);
    assert(strcmp(entry.id, "abe6121c-1731-4683-815c-89e1dcd2bf11") == 0);
    assert(strcmp(entry.title, "Music") == 0);
    assert(!entry.playable);
    assert(entry.url[0] == '\0');
}

static void test_what_cannot_be_played_is_a_row_that_says_so(void)
{
    /* A video, a codec that is not built in, and an audio item the server
       described without a resource. All three are rows - greyed out - rather
       than rows quietly dropped, because a listing that hides what it cannot
       play looks exactly like a server with missing files. */
    const char didl[] =
        "<DIDL-Lite>"
        "<item id=\"v\"><dc:title>Film</dc:title>"
        "<upnp:class>object.item.videoItem.movie</upnp:class>"
        "<res protocolInfo=\"http-get:*:video/mp4:*\">http://h/v.mp4</res></item>"
        "<item id=\"w\"><dc:title>Odd codec</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/x-ms-wma:*\">http://h/w.wma</res></item>"
        "<item id=\"n\"><dc:title>No resource</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class></item>"
        "</DIDL-Lite>";
    dlna_entry_t entries[3];
    assert(dlna_didl_parse(didl, strlen(didl), entries, 3U, NULL) == 3U);
    for (size_t index = 0U; index < 3U; ++index) {
        assert(entries[index].kind == DLNA_ENTRY_ITEM);
        assert(!entries[index].playable);
        assert(entries[index].title[0] != '\0');
    }
}

static void test_it_takes_the_first_resource_it_can_decode(void)
{
    /* Servers offer alternatives: a transcode this build cannot open beside
       the original file it can. Taking the first <res> blindly would make a
       playable track unplayable. */
    const char didl[] =
        "<DIDL-Lite><item id=\"m\"><dc:title>Mixed</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"rtsp-rtp-udp:*:audio/mpeg:*\">rtsp://h/s</res>"
        "<res protocolInfo=\"http-get:*:audio/x-ms-wma:*\">http://h/m.wma</res>"
        "<res duration=\"0:01:00\" protocolInfo=\"http-get:*:audio/flac:*\">http://h/m.flac</res>"
        "</item></DIDL-Lite>";
    dlna_entry_t entry;
    assert(dlna_didl_parse(didl, strlen(didl), &entry, 1U, NULL) == 1U);
    assert(entry.playable);
    assert(entry.format == RADIO_STREAM_FORMAT_FLAC);
    assert(strcmp(entry.url, "http://h/m.flac") == 0);
    assert(entry.duration_ms == 60000U);
}

static void test_durations_as_servers_write_them(void)
{
    static const struct {
        const char *duration;
        uint32_t expected_ms;
    } cases[] = {
        {"0:03:46.000", 226000U},
        {"0:03:46", 226000U},
        {"1:00:00.000", 3600000U},
        {"0:00:01.500", 1500U},
        {"0:00:00.5", 500U},
        {"90", 90000U},
        {"", 0U},
        {"unknown", 0U},
        {"0:03:xx", 0U},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char didl[512];
        snprintf(didl, sizeof(didl),
                 "<DIDL-Lite><item id=\"d\"><dc:title>T</dc:title>"
                 "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
                 "<res duration=\"%s\" protocolInfo=\"http-get:*:audio/mpeg:*\">"
                 "http://h/t.mp3</res></item></DIDL-Lite>",
                 cases[index].duration);
        dlna_entry_t entry;
        assert(dlna_didl_parse(didl, strlen(didl), &entry, 1U, NULL) == 1U);
        assert(entry.playable);
        assert(entry.duration_ms == cases[index].expected_ms);
    }
}

static void test_titles_survive_the_languages_on_this_server(void)
{
    /* Both are on the LAN: a Japanese track title and an album name with an
       escaped apostrophe. The first arrives as UTF-8 bytes, the second as an
       entity, and a listing has to carry both onto the panel. */
    const char didl[] =
        "<DIDL-Lite>"
        "<container id=\"j\"><dc:title>\xe3\x82\xb9\xe3\x82\xa4\xe3\x83\xbc\xe3\x83\x84"
        "\xe3\x81\xaf\xe3\x81\x84\xe3\x81\x8b\xe3\x81\x8c?</dc:title>"
        "<upnp:class>object.container.album.musicAlbum</upnp:class></container>"
        "<container id=\"a\"><dc:title>Peter Lorre&apos;s Twentieth Century</dc:title>"
        "<upnp:class>object.container.album.musicAlbum</upnp:class></container>"
        "</DIDL-Lite>";
    dlna_entry_t entries[2];
    assert(dlna_didl_parse(didl, strlen(didl), entries, 2U, NULL) == 2U);
    assert(strcmp(entries[0].title,
                  "\xe3\x82\xb9\xe3\x82\xa4\xe3\x83\xbc\xe3\x83\x84"
                  "\xe3\x81\xaf\xe3\x81\x84\xe3\x81\x8b\xe3\x81\x8c?") == 0);
    assert(strcmp(entries[1].title, "Peter Lorre's Twentieth Century") == 0);
}

static void test_the_standard_art_field_is_used_when_there_is_no_icon(void)
{
    /* upnp:icon is what this server happens to offer; upnp:albumArtURI is the
       field the specification defines, and a server that sends only it must
       still get a cover - it just costs more to fetch. */
    const char didl[] =
        "<DIDL-Lite><item id=\"a\"><dc:title>T</dc:title>"
        "<upnp:albumArtURI>http://h/art.jpg</upnp:albumArtURI>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://h/t.mp3</res></item></DIDL-Lite>";
    dlna_entry_t entry;
    assert(dlna_didl_parse(didl, strlen(didl), &entry, 1U, NULL) == 1U);
    assert(strcmp(entry.art_url, "http://h/art.jpg") == 0);

    /* And a track with no picture at all leaves the field empty rather than
       borrowing one - the caller clears the tile on it. */
    const char bare[] =
        "<DIDL-Lite><item id=\"b\"><dc:title>T</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://h/t.mp3</res></item></DIDL-Lite>";
    assert(dlna_didl_parse(bare, strlen(bare), &entry, 1U, NULL) == 1U);
    assert(entry.art_url[0] == '\0');
}

static void test_creator_stands_in_for_a_missing_artist(void)
{
    /* Older servers send only dc:creator. It is the same answer under an
       earlier name, and without it those tracks play with no performer shown. */
    const char didl[] =
        "<DIDL-Lite><item id=\"c\"><dc:title>T</dc:title><dc:creator>Sting</dc:creator>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://h/t.mp3</res></item></DIDL-Lite>";
    dlna_entry_t entry;
    assert(dlna_didl_parse(didl, strlen(didl), &entry, 1U, NULL) == 1U);
    assert(strcmp(entry.artist, "Sting") == 0);
}

static void test_a_page_smaller_than_the_listing(void)
{
    /* What did not fit still has to be counted, or the caller cannot tell a
       full page from the end of the container. */
    const char didl[] =
        "<DIDL-Lite>"
        "<container id=\"1\"><dc:title>A</dc:title></container>"
        "<container id=\"2\"><dc:title>B</dc:title></container>"
        "<container id=\"3\"><dc:title>C</dc:title></container>"
        "</DIDL-Lite>";
    dlna_entry_t entries[2];
    size_t seen = 0U;
    assert(dlna_didl_parse(didl, strlen(didl), entries, 2U, &seen) == 2U);
    assert(seen == 3U);
    assert(strcmp(entries[0].title, "A") == 0);
    assert(strcmp(entries[1].title, "B") == 0);

    /* And counting with nowhere to put them is how a caller sizes a container
       before asking for it. */
    assert(dlna_didl_parse(didl, strlen(didl), NULL, 0U, &seen) == 0U);
    assert(seen == 3U);
}

static void test_an_entry_without_a_usable_id_is_not_a_row(void)
{
    /* An id is what a row is *for*: it is the only way to open a container or
       name a track. One missing, or too long to keep whole, means a row that
       would browse somewhere else - worse than no row at all. And what it
       counts must not depend on how much room the caller had. */
    char oversized[DLNA_OBJECT_ID_MAX + 32U];
    memset(oversized, 'x', sizeof(oversized) - 1U);
    oversized[sizeof(oversized) - 1U] = '\0';

    char didl[1024];
    snprintf(didl, sizeof(didl),
             "<DIDL-Lite>"
             "<container><dc:title>No id</dc:title></container>"
             "<container id=\"%s\"><dc:title>Long id</dc:title></container>"
             "<container id=\"good\"><dc:title>Kept</dc:title></container>"
             "</DIDL-Lite>",
             oversized);

    dlna_entry_t entries[4];
    size_t seen = 0U;
    assert(dlna_didl_parse(didl, strlen(didl), entries, 4U, &seen) == 1U);
    assert(seen == 1U);
    assert(strcmp(entries[0].title, "Kept") == 0);

    size_t seen_without_room = 0U;
    assert(dlna_didl_parse(didl, strlen(didl), NULL, 0U, &seen_without_room) == 0U);
    assert(seen_without_room == seen);
}

static void test_a_fault_is_told_apart_from_a_listing(void)
{
    /* 701 is a stale object id after the server re-scanned its library, and
       the answer is to browse again from the root. 401 is an action the server
       does not have, and no amount of retrying will help. The log has to be
       able to say which. */
    const char fault[] =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault>"
        "<faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring>"
        "<detail><UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
        "<errorCode>701</errorCode><errorDescription>No such object</errorDescription>"
        "</UPnPError></detail></s:Fault></s:Body></s:Envelope>";

    dlna_soap_browse_t envelope;
    assert(!dlna_soap_parse_browse(fault, strlen(fault), &envelope));
    assert(dlna_soap_fault_code(fault, strlen(fault)) == 701);

    /* A listing is not a fault. */
    assert(dlna_soap_fault_code(k_album_response, strlen(k_album_response)) == 0);
}

static void test_an_empty_container_is_not_a_failure(void)
{
    /* A folder with nothing in it parses, and says so. Treating it as an error
       would show "cannot read" where the honest answer is "nothing here". */
    const char empty[] =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<Result>&lt;DIDL-Lite&gt;&lt;/DIDL-Lite&gt;</Result>"
        "<NumberReturned>0</NumberReturned><TotalMatches>0</TotalMatches>"
        "</u:BrowseResponse></s:Body></s:Envelope>";
    dlna_entry_t entries[4];
    dlna_soap_browse_t envelope;
    size_t seen = 0U;
    assert(browse(empty, entries, 4U, &envelope, &seen) == 0U);
    assert(seen == 0U);
    assert(envelope.total_matches == 0U);
}

static void test_a_short_read_does_not_become_a_listing(void)
{
    dlna_soap_browse_t envelope;
    dlna_entry_t entries[4];

    /* Every prefix of the real reply. Some hold a whole Result and legitimately
       parse; the point is that none of them reads off the end, which the
       sanitizer decides. */
    const size_t length = strlen(k_album_response);
    for (size_t cut = 0U; cut < length; ++cut) {
        if (!dlna_soap_parse_browse(k_album_response, cut, &envelope)) continue;
        assert(envelope.result_offset + envelope.result_length <= cut);
        (void)dlna_didl_parse(k_album_response + envelope.result_offset,
                              envelope.result_length, entries, 4U, NULL);
    }

    assert(!dlna_soap_parse_browse(NULL, 10U, &envelope));
    assert(!dlna_soap_parse_browse(k_album_response, 0U, &envelope));
    assert(!dlna_soap_parse_browse(k_album_response, length, NULL));
    assert(dlna_soap_fault_code(NULL, 10U) == 0);
    assert(dlna_didl_parse(NULL, 10U, entries, 4U, NULL) == 0U);
}

int main(void)
{
    test_it_asks_for_what_this_server_answered();
    test_it_reads_the_album_this_server_sent();
    test_the_order_is_the_servers();
    test_a_container_is_something_to_open_not_to_play();
    test_what_cannot_be_played_is_a_row_that_says_so();
    test_it_takes_the_first_resource_it_can_decode();
    test_durations_as_servers_write_them();
    test_titles_survive_the_languages_on_this_server();
    test_the_standard_art_field_is_used_when_there_is_no_icon();
    test_creator_stands_in_for_a_missing_artist();
    test_a_page_smaller_than_the_listing();
    test_an_entry_without_a_usable_id_is_not_a_row();
    test_a_fault_is_told_apart_from_a_listing();
    test_an_empty_container_is_not_a_failure();
    test_a_short_read_does_not_become_a_listing();
    puts("dlna_browse tests passed");
    return 0;
}
