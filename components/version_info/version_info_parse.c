#include "version_info.h"

#include <string.h>

/* The stamp is one flat object written by tools/stamp_version.py, so this
 * reads it by hand rather than pulling a JSON library into a component that
 * otherwise needs nothing. Thirty lines against a dependency, and it is the
 * half of this module that can be tested without a device.
 *
 * It is still fed a file off a filesystem, so it is written to survive a
 * truncated or garbled one: every scan is bounded by `length`, and anything
 * that does not parse comes back as "no version", which the caller already
 * has to handle for an image flashed before the stamp existed.
 */

/* Copies the string value of `key` into `out`. False when the key is absent,
 * when what follows it is not a string, or when the value does not fit -
 * a value too long for the field is a value we cannot report, and reporting
 * half of a version string would be worse than reporting none.
 *
 * Escapes are not decoded. The writer emits a git describe string and a date,
 * neither of which can contain a quote or a backslash; a file that does would
 * be cut short at the escape rather than misread past the end of the value. */
static bool read_string_field(const char *json, size_t length, const char *key,
                              char *out, size_t out_size)
{
    out[0] = '\0';
    const size_t key_length = strlen(key);
    /* The quotes around the name, the colon and the two around the value:
     * anything shorter cannot hold this field at all. */
    if (length < key_length + 5U) return false;

    for (size_t start = 0U; start + key_length + 1U < length; ++start) {
        if (json[start] != '"') continue;
        if (memcmp(json + start + 1U, key, key_length) != 0) continue;
        size_t at = start + 1U + key_length;
        if (at >= length || json[at] != '"') continue;
        ++at;
        while (at < length && (json[at] == ' ' || json[at] == '\t' ||
                               json[at] == '\n' || json[at] == '\r')) {
            ++at;
        }
        if (at >= length || json[at] != ':') continue;
        ++at;
        while (at < length && (json[at] == ' ' || json[at] == '\t' ||
                               json[at] == '\n' || json[at] == '\r')) {
            ++at;
        }
        /* A key that is there but holds a number, an object or null is not a
         * different key - stop rather than carry on looking for another one
         * with the same name. */
        if (at >= length || json[at] != '"') return false;
        ++at;
        const size_t value_start = at;
        while (at < length && json[at] != '"') ++at;
        /* No closing quote: the file was cut off mid-value. */
        if (at >= length) return false;
        const size_t value_length = at - value_start;
        if (value_length == 0U || value_length >= out_size) return false;
        memcpy(out, json + value_start, value_length);
        out[value_length] = '\0';
        return true;
    }
    return false;
}

bool version_info_parse(const char *json, size_t length, version_info_part_t *part)
{
    if (part == NULL) return false;
    memset(part, 0, sizeof(*part));
    if (json == NULL || length == 0U) return false;

    if (!read_string_field(json, length, "version", part->version, sizeof(part->version))) {
        part->version[0] = '\0';
        return false;
    }
    /* The date is optional in a way the version is not: a stamp that names
     * what was built is useful even if it cannot say when. */
    (void)read_string_field(json, length, "built", part->built, sizeof(part->built));
    part->present = true;
    return true;
}

bool version_info_matched(const version_info_t *info)
{
    if (info == NULL) return false;
    /* A web stamp that could not be read is not a match. The device says
     * "unknown" for it, and calling that agreement would hide exactly the
     * case this module exists to show. */
    if (!info->firmware.present || !info->web.present) return false;
    return strcmp(info->firmware.version, info->web.version) == 0;
}
