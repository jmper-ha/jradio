#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Just enough XML to read what a UPnP server says, and nothing more.
 *
 * Three documents arrive from a media server - the device description, the
 * SOAP envelope, and the DIDL-Lite payload inside it - and all three are read
 * the same way: find an element, take its text, take an attribute. That is the
 * whole requirement, so this is a scanner over a buffer rather than a parser:
 * no tree, no allocation, no document object that has to be freed on every
 * error path in a task that is also holding an HTTP connection open.
 *
 * The device gets these documents in one buffer it already owns, so everything
 * here works on (pointer, length) and never assumes a NUL. Nothing is copied
 * except where a caller asks for a copy.
 *
 * What it deliberately cannot do: nesting of same-named elements. Finding the
 * end of an element means finding its first closing tag, so an element that
 * contains another of its own name reads back short. None of the three
 * documents nests that way at the points we read - DIDL containers and items
 * are siblings of one list, and a <service> holds no services - and a real
 * parser is a lot of code to carry for a case that does not occur. If a server
 * ever turns up that needs it, this comment is the reason it broke. */

typedef struct {
    /* Not NUL-terminated: it points into the caller's document. */
    const char *data;
    size_t length;
} dlna_xml_slice_t;

typedef struct {
    /* Offset of the element's '<' in the document. A listing holds containers
     * and items as siblings and the order they came in is the order to show
     * them, so a caller reading both has to be able to say which came first. */
    size_t start;
    /* Inside the opening tag, after the name: ` id="x" parentID="0"`. */
    dlna_xml_slice_t attributes;
    /* Between the tags. Empty for a self-closing element. */
    dlna_xml_slice_t body;
    /* Offset just past the element, so a caller can ask for the next one. */
    size_t next;
} dlna_xml_element_t;

/* Finds the first `name` element at or after `from`.
 *
 * A name matches either exactly or after a namespace prefix, so asking for
 * "title" finds `<dc:title>` and asking for "dc:title" finds it too. The
 * prefixes servers use are theirs to choose - the same field is `dc:title` on
 * one and `ns0:title` on another - and matching the local name is what makes
 * this survive that without the caller knowing which server it is talking to.
 *
 * Comments, processing instructions and doctypes are skipped, so a tag that
 * appears only inside a comment is not found. */
bool dlna_xml_find_element(const char *document, size_t length, const char *name,
                           size_t from, dlna_xml_element_t *out);

/* Copies one attribute's value, unescaped. False when the attribute is absent,
 * which is not the same as present and empty. */
bool dlna_xml_attribute(dlna_xml_slice_t attributes, const char *name,
                        char *out, size_t out_size);

/* Finds `name` and copies its text, unescaped. The common case, and the reason
 * most callers never touch dlna_xml_element_t at all. */
bool dlna_xml_element_text(const char *document, size_t length, const char *name,
                           size_t from, char *out, size_t out_size);

/* Replaces XML entities with the characters they stand for.
 *
 * `out` may be `in`: an entity is never shorter than what it expands to - the
 * shortest is four bytes for one - so the write pointer can never overtake the
 * read pointer. The device relies on that to unescape a 4 KB DIDL payload
 * inside the buffer it already read it into, rather than finding a second one.
 *
 * Returns the number of bytes written, not counting the NUL it always adds
 * when there is room. An entity this does not know is copied through as
 * written, because a title containing a literal "&" is far more likely than a
 * server inventing an entity, and dropping it would silently corrupt a name. */
size_t dlna_xml_unescape(const char *in, size_t length, char *out, size_t out_size);

/* The other direction, for the one thing the device sends: an object id in a
 * SOAP request. Returns the length written, or 0 when it does not fit - a
 * truncated id would ask the server for a different object, so the caller has
 * to be able to tell. */
size_t dlna_xml_escape(const char *in, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
