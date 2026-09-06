#include "dlna_xml.h"

#include <string.h>

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* True when the tag written in the document, `tag`, is the element the caller
 * asked for. Either the names are equal, or the document's carries a namespace
 * prefix in front of the name asked for. */
static bool name_matches(const char *tag, size_t tag_length, const char *name)
{
    const size_t name_length = strlen(name);
    if (tag_length == name_length && memcmp(tag, name, name_length) == 0) return true;
    if (tag_length <= name_length) return false;
    const size_t prefix = tag_length - name_length;
    if (tag[prefix - 1U] != ':') return false;
    if (memchr(tag, ':', prefix - 1U) != NULL) return false;
    return memcmp(tag + prefix, name, name_length) == 0;
}

/* Past a comment, processing instruction or doctype starting at `at`, which is
 * the '<'. Returns the offset of the character after it, or `length` when the
 * document ends inside one. */
static size_t skip_non_element(const char *document, size_t length, size_t at)
{
    if (at + 4U <= length && memcmp(document + at, "<!--", 4U) == 0) {
        for (size_t index = at + 4U; index + 3U <= length; ++index) {
            if (memcmp(document + index, "-->", 3U) == 0) return index + 3U;
        }
        return length;
    }
    for (size_t index = at + 1U; index < length; ++index) {
        if (document[index] == '>') return index + 1U;
    }
    return length;
}

bool dlna_xml_find_element(const char *document, size_t length, const char *name,
                           size_t from, dlna_xml_element_t *out)
{
    if (document == NULL || name == NULL || out == NULL) return false;

    size_t index = from;
    while (index < length) {
        if (document[index] != '<') {
            ++index;
            continue;
        }
        if (index + 1U < length &&
            (document[index + 1U] == '/' || document[index + 1U] == '?' ||
             document[index + 1U] == '!')) {
            index = skip_non_element(document, length, index);
            continue;
        }

        const size_t tag_start = index + 1U;
        size_t tag_end = tag_start;
        while (tag_end < length && !is_space(document[tag_end]) &&
               document[tag_end] != '>' && document[tag_end] != '/') {
            ++tag_end;
        }
        if (tag_end >= length) return false;

        /* The end of the opening tag, and whether it closed itself on the way. */
        size_t open_end = tag_end;
        while (open_end < length && document[open_end] != '>') ++open_end;
        if (open_end >= length) return false;
        const bool self_closing = open_end > tag_start && document[open_end - 1U] == '/';

        if (!name_matches(document + tag_start, tag_end - tag_start, name)) {
            index = open_end + 1U;
            continue;
        }

        out->start = index;
        out->attributes.data = document + tag_end;
        out->attributes.length = (self_closing ? open_end - 1U : open_end) - tag_end;

        if (self_closing) {
            out->body.data = document + open_end;
            out->body.length = 0U;
            out->next = open_end + 1U;
            return true;
        }

        /* The closing tag is looked for by the name as the document wrote it,
         * prefix and all, so a document mixing prefixes for the same element
         * cannot make one element's end close another's. */
        const char *tag = document + tag_start;
        const size_t tag_length = tag_end - tag_start;
        const size_t body_start = open_end + 1U;
        for (size_t close = body_start; close + tag_length + 3U <= length; ++close) {
            if (document[close] != '<' || document[close + 1U] != '/') continue;
            if (memcmp(document + close + 2U, tag, tag_length) != 0) continue;
            size_t after = close + 2U + tag_length;
            while (after < length && is_space(document[after])) ++after;
            if (after >= length || document[after] != '>') continue;
            out->body.data = document + body_start;
            out->body.length = close - body_start;
            out->next = after + 1U;
            return true;
        }
        /* Opened and never closed: a truncated read, and there is nothing
         * usable after it either, so stop rather than resync. */
        return false;
    }
    return false;
}

bool dlna_xml_attribute(dlna_xml_slice_t attributes, const char *name,
                        char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return false;
    out[0] = '\0';
    if (attributes.data == NULL || name == NULL) return false;

    const size_t name_length = strlen(name);
    if (name_length == 0U) return false;

    for (size_t index = 0U; index + name_length < attributes.length; ++index) {
        /* Only at the start of an attribute name, so `id=` is not found inside
         * `parentID=`. */
        if (index != 0U && !is_space(attributes.data[index - 1U])) continue;
        if (memcmp(attributes.data + index, name, name_length) != 0) continue;

        size_t at = index + name_length;
        while (at < attributes.length && is_space(attributes.data[at])) ++at;
        if (at >= attributes.length || attributes.data[at] != '=') continue;
        ++at;
        while (at < attributes.length && is_space(attributes.data[at])) ++at;
        if (at >= attributes.length) return false;
        const char quote = attributes.data[at];
        if (quote != '"' && quote != '\'') continue;
        ++at;
        const size_t value_start = at;
        while (at < attributes.length && attributes.data[at] != quote) ++at;
        if (at >= attributes.length) return false;
        (void)dlna_xml_unescape(attributes.data + value_start, at - value_start, out, out_size);
        return true;
    }
    return false;
}

bool dlna_xml_element_text(const char *document, size_t length, const char *name,
                           size_t from, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return false;
    out[0] = '\0';
    dlna_xml_element_t element;
    if (!dlna_xml_find_element(document, length, name, from, &element)) return false;
    (void)dlna_xml_unescape(element.body.data, element.body.length, out, out_size);
    return true;
}

/* Writes one code point as UTF-8. Returns the bytes it needs, whether or not
 * they were written, so the caller can stop cleanly when they did not fit. */
static size_t write_utf8(unsigned long value, char *out, size_t room)
{
    size_t needed;
    if (value < 0x80UL) needed = 1U;
    else if (value < 0x800UL) needed = 2U;
    else if (value < 0x10000UL) needed = 3U;
    else needed = 4U;
    if (needed > room) return needed;

    switch (needed) {
    case 1U:
        out[0] = (char)value;
        break;
    case 2U:
        out[0] = (char)(0xC0UL | (value >> 6));
        out[1] = (char)(0x80UL | (value & 0x3FUL));
        break;
    case 3U:
        out[0] = (char)(0xE0UL | (value >> 12));
        out[1] = (char)(0x80UL | ((value >> 6) & 0x3FUL));
        out[2] = (char)(0x80UL | (value & 0x3FUL));
        break;
    default:
        out[0] = (char)(0xF0UL | (value >> 18));
        out[1] = (char)(0x80UL | ((value >> 12) & 0x3FUL));
        out[2] = (char)(0x80UL | ((value >> 6) & 0x3FUL));
        out[3] = (char)(0x80UL | (value & 0x3FUL));
        break;
    }
    return needed;
}

/* Reads the entity starting at `in[0]`, which the caller has checked is '&'.
 * On success `*consumed` is its length in the input and the expansion has been
 * written. Returns false for anything that is not an entity this knows, and
 * for one that does not fit, leaving the caller to copy the '&' through. */
static bool expand_entity(const char *in, size_t available, char *out, size_t room,
                          size_t *consumed, size_t *written)
{
    static const struct {
        const char *name;
        size_t length;
        char value;
    } named[] = {
        {"&lt;", 4U, '<'},
        {"&gt;", 4U, '>'},
        {"&amp;", 5U, '&'},
        {"&quot;", 6U, '"'},
        {"&apos;", 6U, '\''},
    };

    for (size_t index = 0U; index < sizeof(named) / sizeof(named[0]); ++index) {
        if (available < named[index].length) continue;
        if (memcmp(in, named[index].name, named[index].length) != 0) continue;
        if (room < 1U) return false;
        out[0] = named[index].value;
        *consumed = named[index].length;
        *written = 1U;
        return true;
    }

    if (available < 4U || in[1] != '#') return false;
    size_t at = 2U;
    int base = 10;
    if (in[at] == 'x' || in[at] == 'X') {
        base = 16;
        ++at;
    }
    const size_t digits_start = at;
    unsigned long value = 0UL;
    while (at < available && in[at] != ';') {
        const char c = in[at];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10U;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10U;
        else return false;
        /* Past what a code point can be. Refuse rather than wrap: a number
         * this large is a broken document, not a character. */
        if (value > 0x10FFFFUL) return false;
        value = value * (unsigned long)base + digit;
        ++at;
    }
    if (at >= available || at == digits_start || value > 0x10FFFFUL) return false;

    const size_t needed = write_utf8(value, out, room);
    if (needed > room) return false;
    *consumed = at + 1U;
    *written = needed;
    return true;
}

size_t dlna_xml_unescape(const char *in, size_t length, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return 0U;
    if (in == NULL) {
        out[0] = '\0';
        return 0U;
    }

    size_t read = 0U;
    size_t written = 0U;
    while (read < length && written + 1U < out_size) {
        if (in[read] == '&') {
            size_t consumed = 0U;
            size_t produced = 0U;
            if (expand_entity(in + read, length - read, out + written,
                              out_size - 1U - written, &consumed, &produced)) {
                read += consumed;
                written += produced;
                continue;
            }
        }
        out[written++] = in[read++];
    }
    out[written] = '\0';
    return written;
}

size_t dlna_xml_escape(const char *in, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    if (in == NULL) return 0U;

    size_t written = 0U;
    for (size_t read = 0U; in[read] != '\0'; ++read) {
        const char *replacement = NULL;
        switch (in[read]) {
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        case '&': replacement = "&amp;"; break;
        case '"': replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        default: break;
        }
        const size_t needed = replacement != NULL ? strlen(replacement) : 1U;
        if (written + needed + 1U > out_size) {
            out[0] = '\0';
            return 0U;
        }
        if (replacement != NULL) memcpy(out + written, replacement, needed);
        else out[written] = in[read];
        written += needed;
    }
    out[written] = '\0';
    return written;
}
