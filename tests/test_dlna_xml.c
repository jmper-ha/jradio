#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dlna_xml.h"

/* The documents this reads come off a media server on the LAN, so what matters
   is not that it parses well-formed XML - it will - but what it does with the
   shapes a real server produces: namespace prefixes it did not choose, a
   payload escaped inside another document, titles in languages that are not
   ASCII, and a read that was cut short. */

static void test_it_finds_an_element_whatever_prefix_the_server_uses(void)
{
    const char doc[] = "<item><dc:title>Deuce</dc:title><upnp:artist>KISS</upnp:artist></item>";
    char value[64];

    /* The local name is enough. Which prefix a server binds to Dublin Core is
       its own business, and hard-coding "dc:" is how a client works against
       one server and not the next. */
    assert(dlna_xml_element_text(doc, strlen(doc), "title", 0U, value, sizeof(value)));
    assert(strcmp(value, "Deuce") == 0);
    assert(dlna_xml_element_text(doc, strlen(doc), "dc:title", 0U, value, sizeof(value)));
    assert(strcmp(value, "Deuce") == 0);
    assert(dlna_xml_element_text(doc, strlen(doc), "artist", 0U, value, sizeof(value)));
    assert(strcmp(value, "KISS") == 0);

    /* A name that merely ends the same is a different name: asking for "title"
       must not answer with "subtitle". */
    const char lookalike[] = "<subtitle>no</subtitle>";
    assert(!dlna_xml_element_text(lookalike, strlen(lookalike), "title", 0U, value, sizeof(value)));

    /* And absent is absent. */
    assert(!dlna_xml_element_text(doc, strlen(doc), "album", 0U, value, sizeof(value)));
    assert(value[0] == '\0');
}

static void test_it_walks_siblings(void)
{
    /* How a listing is read: one element after another, each starting where
       the last one ended. */
    const char doc[] = "<r><i>1</i><i>2</i><i>3</i></r>";
    dlna_xml_element_t element;
    size_t from = 0U;
    int seen = 0;
    while (dlna_xml_find_element(doc, strlen(doc), "i", from, &element)) {
        ++seen;
        assert(element.body.length == 1U);
        assert(element.body.data[0] == (char)('0' + seen));
        from = element.next;
    }
    assert(seen == 3);
}

static void test_it_reads_attributes_without_being_fooled_by_longer_names(void)
{
    /* Taken from the server: an id and a parentID, one of which ends with the
       other's name. */
    const char doc[] = "<container id=\"abc123\" parentID=\"0\" restricted=\"1\"></container>";
    dlna_xml_element_t element;
    assert(dlna_xml_find_element(doc, strlen(doc), "container", 0U, &element));

    char value[64];
    assert(dlna_xml_attribute(element.attributes, "id", value, sizeof(value)));
    assert(strcmp(value, "abc123") == 0);
    assert(dlna_xml_attribute(element.attributes, "parentID", value, sizeof(value)));
    assert(strcmp(value, "0") == 0);
    assert(!dlna_xml_attribute(element.attributes, "searchable", value, sizeof(value)));
    assert(value[0] == '\0');

    /* Single quotes are as legal as double, and spaces around the '=' happen. */
    const char spaced[] = "<res protocolInfo = 'http-get:*:audio/mpeg:*'>u</res>";
    assert(dlna_xml_find_element(spaced, strlen(spaced), "res", 0U, &element));
    assert(dlna_xml_attribute(element.attributes, "protocolInfo", value, sizeof(value)));
    assert(strcmp(value, "http-get:*:audio/mpeg:*") == 0);
}

static void test_a_self_closing_element_has_attributes_and_no_text(void)
{
    /* Servers emit these for an empty field, and the '/' must not end up in
       the last attribute's value. */
    const char doc[] = "<didl><res id=\"7\"/><next>after</next></didl>";
    dlna_xml_element_t element;
    assert(dlna_xml_find_element(doc, strlen(doc), "res", 0U, &element));
    assert(element.body.length == 0U);
    char value[32];
    assert(dlna_xml_attribute(element.attributes, "id", value, sizeof(value)));
    assert(strcmp(value, "7") == 0);
    /* And the walk carries on past it rather than swallowing what follows. */
    assert(dlna_xml_element_text(doc, strlen(doc), "next", element.next, value, sizeof(value)));
    assert(strcmp(value, "after") == 0);
}

static void test_it_ignores_comments_and_declarations(void)
{
    const char doc[] = "<?xml version=\"1.0\"?><!-- <title>ghost</title> --><title>real</title>";
    char value[32];
    assert(dlna_xml_element_text(doc, strlen(doc), "title", 0U, value, sizeof(value)));
    assert(strcmp(value, "real") == 0);
}

static void test_it_unescapes_what_a_server_actually_sends(void)
{
    char out[128];

    /* An apostrophe in an album name, seen on this LAN in "Peter Lorre&apos;s
       Twentieth Century". */
    const char in[] = "Peter Lorre&apos;s &amp; &lt;friends&gt; &quot;live&quot;";
    assert(dlna_xml_unescape(in, strlen(in), out, sizeof(out)) == strlen(out));
    assert(strcmp(out, "Peter Lorre's & <friends> \"live\"") == 0);

    /* Numeric references, decimal and hex, expanded to UTF-8. */
    const char numeric[] = "&#1052;&#x443;&#x437;&#1099;&#x43A;&#1072;";
    dlna_xml_unescape(numeric, strlen(numeric), out, sizeof(out));
    assert(strcmp(out, "Музыка") == 0);

    /* Anything it does not know is a literal, because a title with a bare '&'
       in it is far likelier than a server inventing an entity - and dropping
       it would quietly corrupt a name. */
    const char unknown[] = "AT&T &nosuch; 100% & more";
    dlna_xml_unescape(unknown, strlen(unknown), out, sizeof(out));
    assert(strcmp(out, "AT&T &nosuch; 100% & more") == 0);

    /* Truncated at the end of the buffer: copied through, never read past. */
    const char cut[] = "done &am";
    dlna_xml_unescape(cut, strlen(cut), out, sizeof(out));
    assert(strcmp(out, "done &am") == 0);
}

static void test_unescaping_in_place_is_safe(void)
{
    /* The device unescapes a 4 KB DIDL payload inside the buffer it read it
       into rather than finding a second one, which is only allowed because an
       entity never expands to more bytes than it occupies. If that ever stops
       being true this is where it shows. */
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", "&lt;a&gt; &amp; &#x1F600; &quot;b&quot;");
    const size_t written = dlna_xml_unescape(buffer, strlen(buffer), buffer, sizeof(buffer));
    assert(strcmp(buffer, "<a> & \xF0\x9F\x98\x80 \"b\"") == 0);
    assert(written == strlen(buffer));
}

static void test_a_short_buffer_truncates_and_still_terminates(void)
{
    char small[8];
    const char in[] = "0123456789";
    const size_t written = dlna_xml_unescape(in, strlen(in), small, sizeof(small));
    assert(written == 7U);
    assert(strcmp(small, "0123456") == 0);

    /* A multi-byte expansion that does not fit is not written half way: a
       broken UTF-8 sequence on the panel is worse than a shorter title. */
    char tight[4];
    const char wide[] = "ab&#x1F600;";
    dlna_xml_unescape(wide, strlen(wide), tight, sizeof(tight));
    assert(strcmp(tight, "ab&") == 0);
}

static void test_a_truncated_document_fails_rather_than_inventing(void)
{
    char value[32];
    /* Opened and never closed - what a short read looks like. */
    const char cut[] = "<didl><title>Deu";
    assert(!dlna_xml_element_text(cut, strlen(cut), "title", 0U, value, sizeof(value)));

    /* The length is the authority, not the NUL: the buffer holds a whole
       element, the caller says the read stopped earlier. */
    const char whole[] = "<title>Deuce</title>";
    assert(!dlna_xml_element_text(whole, 10U, "title", 0U, value, sizeof(value)));

    /* Every prefix of a real document, to prove none of them reads off the
       end. The sanitizer decides whether this passed. */
    const char doc[] = "<item id=\"1\"><dc:title>a&amp;b</dc:title><res x=\"1\"/></item>";
    for (size_t length = 0U; length <= strlen(doc); ++length) {
        dlna_xml_element_t element;
        (void)dlna_xml_find_element(doc, length, "item", 0U, &element);
        (void)dlna_xml_find_element(doc, length, "res", 0U, &element);
        (void)dlna_xml_element_text(doc, length, "title", 0U, value, sizeof(value));
    }
}

static void test_escaping_an_object_id(void)
{
    char out[64];
    assert(dlna_xml_escape("abc123", out, sizeof(out)) == 6U);
    assert(strcmp(out, "abc123") == 0);

    assert(dlna_xml_escape("a&b<c>", out, sizeof(out)) > 0U);
    assert(strcmp(out, "a&amp;b&lt;c&gt;") == 0);

    /* Refused outright when it does not fit rather than clipped: a truncated
       id asks the server for a different object, and the caller has to be able
       to tell that apart from success. */
    char tiny[4];
    assert(dlna_xml_escape("a&b", tiny, sizeof(tiny)) == 0U);
    assert(tiny[0] == '\0');
}

static void test_nothing_at_all(void)
{
    char value[16];
    dlna_xml_element_t element;
    assert(!dlna_xml_find_element(NULL, 10U, "a", 0U, &element));
    assert(!dlna_xml_find_element("<a/>", 4U, NULL, 0U, &element));
    assert(!dlna_xml_find_element("<a/>", 4U, "a", 0U, NULL));
    assert(!dlna_xml_element_text("<a/>", 4U, "a", 0U, NULL, 0U));
    assert(dlna_xml_unescape(NULL, 5U, value, sizeof(value)) == 0U);
    assert(value[0] == '\0');
    assert(dlna_xml_escape(NULL, value, sizeof(value)) == 0U);
}

int main(void)
{
    test_it_finds_an_element_whatever_prefix_the_server_uses();
    test_it_walks_siblings();
    test_it_reads_attributes_without_being_fooled_by_longer_names();
    test_a_self_closing_element_has_attributes_and_no_text();
    test_it_ignores_comments_and_declarations();
    test_it_unescapes_what_a_server_actually_sends();
    test_unescaping_in_place_is_safe();
    test_a_short_buffer_truncates_and_still_terminates();
    test_a_truncated_document_fails_rather_than_inventing();
    test_escaping_an_object_id();
    test_nothing_at_all();
    puts("dlna_xml tests passed");
    return 0;
}
