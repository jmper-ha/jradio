#include "dlna_url.h"

#include <string.h>

/* The length of "scheme://host:port", or 0 when `url` is not absolute. */
static size_t authority_length(const char *url)
{
    const char *separator = strstr(url, "://");
    if (separator == NULL) return 0U;
    const char *slash = strchr(separator + 3, '/');
    return slash != NULL ? (size_t)(slash - url) : strlen(url);
}

static bool copy(const char *text, size_t length, char *out, size_t out_size)
{
    if (length + 1U > out_size) return false;
    memcpy(out, text, length);
    out[length] = '\0';
    return true;
}

bool dlna_url_resolve(const char *base, const char *reference, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return false;
    out[0] = '\0';
    if (base == NULL || reference == NULL) return false;

    if (strstr(reference, "://") != NULL) {
        return copy(reference, strlen(reference), out, out_size);
    }

    const size_t authority = authority_length(base);
    if (authority == 0U) return false;

    if (reference[0] == '\0') {
        return copy(base, strlen(base), out, out_size);
    }

    size_t prefix;
    if (reference[0] == '/') {
        prefix = authority;
    } else {
        /* Relative to the directory the base names, so the base's own file
         * name is dropped. With no slash after the host there is no file name
         * to drop and the whole authority stands. */
        const char *last = strrchr(base + authority, '/');
        prefix = last != NULL ? (size_t)(last - base) + 1U : authority;
        if (last == NULL) {
            /* No path at all: the reference hangs off the root. */
            if (authority + 1U + strlen(reference) + 1U > out_size) return false;
            memcpy(out, base, authority);
            out[authority] = '/';
            memcpy(out + authority + 1U, reference, strlen(reference) + 1U);
            return true;
        }
    }

    const size_t length = strlen(reference);
    if (prefix + length + 1U > out_size) return false;
    memcpy(out, base, prefix);
    memcpy(out + prefix, reference, length + 1U);
    return true;
}
