#include <stdio.h>
#include <string.h>

#include "yandex_json_reader.h"
#include "yandex_link.h"
#include "yandex_md5.h"

/* The salt every unofficial client uses, and the only reason the signature is
 * computable at all. Not a secret of ours and not an account credential. */
static const char YANDEX_SIGN_SALT[] = "XGRlBW9FXlekgbPrRHuSiA";

yandex_link_result_t yandex_link_pick_variant(const char *json, char *url, size_t url_size,
                                              uint16_t *bitrate_kbps)
{
    if (json == NULL || url == NULL || url_size == 0U) return YANDEX_LINK_ERR_FORMAT;
    url[0] = '\0';
    if (bitrate_kbps != NULL) *bitrate_kbps = 0U;

    json_reader_t reader = {.cursor = json};
    json_reader_t probe = reader;
    if (json_enter_object_key(&probe, "result")) {
        reader = probe;
    }
    if (!json_accept(&reader, '[')) return YANDEX_LINK_ERR_FORMAT;
    json_skip_whitespace(&reader);
    if (json_accept(&reader, ']')) return YANDEX_LINK_ERR_FORMAT;

    uint32_t best_bitrate = 0U;
    bool saw_variant = false;
    bool saw_playable = false;
    bool too_long = false;

    while (true) {
        json_reader_t variant = reader;
        char codec[16] = {0};
        uint32_t bitrate = 0U;
        bool preview = true;
        bool have_url = false;

        if (json_accept(&variant, '{')) {
            while (true) {
                char key[32];
                if (!json_read_string(&variant, key, sizeof(key))) key[0] = '\0';
                if (!json_accept(&variant, ':')) break;
                if (strcmp(key, "codec") == 0) {
                    if (!json_read_string(&variant, codec, sizeof(codec))) break;
                } else if (strcmp(key, "bitrateInKbps") == 0) {
                    if (!json_read_uint(&variant, &bitrate)) break;
                } else if (strcmp(key, "preview") == 0) {
                    if (!json_read_bool(&variant, &preview)) break;
                } else if (strcmp(key, "downloadInfoUrl") == 0) {
                    have_url = true;
                    /* Only copied once this variant is known to be the best so
                     * far; here the reader just has to get past it. */
                    if (!json_skip_value(&variant)) break;
                } else if (!json_skip_value(&variant)) {
                    break;
                }
                json_skip_whitespace(&variant);
                if (json_accept(&variant, ',')) continue;
                break;
            }
        }

        saw_variant = saw_variant || have_url;
        if (have_url && !preview) {
            saw_playable = true;
            if (strcmp(codec, "mp3") == 0 && bitrate > best_bitrate) {
                json_reader_t copy = reader;
                char candidate[YANDEX_LINK_URL_MAX + 1U];
                if (json_enter_object_key(&copy, "downloadInfoUrl") &&
                    json_read_string(&copy, candidate, sizeof(candidate))) {
                    if (strlen(candidate) >= url_size) {
                        too_long = true;
                    } else {
                        strcpy(url, candidate);
                        best_bitrate = bitrate;
                    }
                }
            }
        }

        if (!json_skip_value(&reader)) break;
        json_skip_whitespace(&reader);
        if (json_accept(&reader, ',')) continue;
        break;
    }

    if (url[0] != '\0') {
        if (bitrate_kbps != NULL) *bitrate_kbps = (uint16_t)best_bitrate;
        return YANDEX_LINK_OK;
    }
    if (too_long) return YANDEX_LINK_ERR_TOO_LONG;
    /* Variants exist, every one of them a preview: the subscription, not the
     * format, is what is missing. */
    if (saw_variant && !saw_playable) return YANDEX_LINK_ERR_PREVIEW_ONLY;
    return YANDEX_LINK_ERR_FORMAT;
}

/* Copies the text of <tag>...</tag>. The answer is one flat element with no
 * attributes, no namespaces and no nesting, so a real XML parser would be
 * several kilobytes spent on a shape that has none of the hard parts. */
static bool xml_element_text(const char *xml, const char *tag, char *output, size_t capacity,
                             bool *too_long)
{
    char open[16];
    char close[16];
    if ((size_t)snprintf(open, sizeof(open), "<%s>", tag) >= sizeof(open)) return false;
    if ((size_t)snprintf(close, sizeof(close), "</%s>", tag) >= sizeof(close)) return false;

    const char *start = strstr(xml, open);
    if (start == NULL) return false;
    start += strlen(open);
    const char *end = strstr(start, close);
    if (end == NULL) return false;

    const size_t length = (size_t)(end - start);
    if (length == 0U) return false;
    if (length >= capacity) {
        *too_long = true;
        return false;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    return true;
}

yandex_link_result_t yandex_link_from_xml(const char *xml, char *url, size_t url_size)
{
    if (xml == NULL || url == NULL || url_size == 0U) return YANDEX_LINK_ERR_FORMAT;
    url[0] = '\0';

    char host[64];
    char path[YANDEX_LINK_URL_MAX + 1U];
    char timestamp[32];
    char salt[64];
    bool too_long = false;

    if (!xml_element_text(xml, "host", host, sizeof(host), &too_long) ||
        !xml_element_text(xml, "path", path, sizeof(path), &too_long) ||
        !xml_element_text(xml, "ts", timestamp, sizeof(timestamp), &too_long) ||
        !xml_element_text(xml, "s", salt, sizeof(salt), &too_long)) {
        return too_long ? YANDEX_LINK_ERR_TOO_LONG : YANDEX_LINK_ERR_FORMAT;
    }
    /* The signature is over the path without its leading slash, so a path that
     * does not start with one would sign something else entirely. */
    if (path[0] != '/') return YANDEX_LINK_ERR_FORMAT;

    char signature[YANDEX_MD5_HEX_SIZE];
    yandex_md5_hex_of(signature, YANDEX_SIGN_SALT, path + 1, salt, NULL);

    const int written = snprintf(url, url_size, "https://%s/get-mp3/%s/%s%s", host, signature,
                                 timestamp, path);
    if (written < 0) return YANDEX_LINK_ERR_FORMAT;
    if ((size_t)written >= url_size) {
        url[0] = '\0';
        return YANDEX_LINK_ERR_TOO_LONG;
    }
    return YANDEX_LINK_OK;
}

static bool cover_url_sized(const char *cover_uri, const char *size_tag,
                            char *url, size_t url_size)
{
    if (cover_uri == NULL || url == NULL || url_size == 0U) return false;
    url[0] = '\0';
    if (cover_uri[0] == '\0') return false;

    const char *marker = strstr(cover_uri, "%%");
    if (marker == NULL) return false;
    const size_t prefix = (size_t)(marker - cover_uri);
    const int written = snprintf(url, url_size, "http://%.*s%s%s",
                                 (int)prefix, cover_uri, size_tag, marker + 2);
    if (written < 0 || (size_t)written >= url_size) {
        url[0] = '\0';
        return false;
    }
    return true;
}

bool yandex_cover_url(const char *cover_uri, char *url, size_t url_size)
{
    return cover_url_sized(cover_uri, YANDEX_COVER_SIZE_TAG, url, url_size);
}

bool yandex_cover_url_web(const char *cover_uri, char *url, size_t url_size)
{
    return cover_url_sized(cover_uri, YANDEX_COVER_WEB_SIZE_TAG, url, url_size);
}
