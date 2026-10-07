#include "ota_tar.h"

#include <string.h>

#define NAME_AT 0U
#define NAME_LENGTH 100U
#define SIZE_AT 124U
#define SIZE_LENGTH 12U
#define CHECKSUM_AT 148U
#define CHECKSUM_LENGTH 8U
#define TYPE_AT 156U
#define MAGIC_AT 257U
#define PREFIX_AT 345U

void ota_tar_init(ota_tar_t *tar)
{
    memset(tar, 0, sizeof(*tar));
}

static bool is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

ota_tar_entry_t ota_tar_entry(const char *path, const char **file_name)
{
    if (file_name != NULL) *file_name = NULL;
    if (strcmp(path, "version.json") == 0) {
        if (file_name != NULL) *file_name = path;
        return OTA_TAR_ENTRY_VERSION;
    }
    if (strncmp(path, "www/", 4U) != 0) return OTA_TAR_ENTRY_INVALID;
    const char *name = path + 4;
    const size_t length = strlen(name);
    /* No leading dot: that rules out "." and ".." and hidden files in one. */
    if (length == 0U || length > OTA_TAR_NAME_MAX || name[0] == '.') {
        return OTA_TAR_ENTRY_INVALID;
    }
    for (size_t index = 0U; index < length; ++index) {
        if (!is_name_char(name[index])) return OTA_TAR_ENTRY_INVALID;
    }
    if (file_name != NULL) *file_name = name;
    return OTA_TAR_ENTRY_WEB;
}

/* An octal field, NUL- or space-terminated. False for anything else. */
static bool read_octal(const uint8_t *field, size_t length, size_t *value)
{
    size_t result = 0U;
    size_t index = 0U;
    while (index < length && field[index] == ' ') ++index;
    bool digits = false;
    for (; index < length; ++index) {
        const uint8_t c = field[index];
        if (c == '\0' || c == ' ') break;
        if (c < '0' || c > '7') return false;
        if (result > (SIZE_MAX >> 3)) return false;
        result = result * 8U + (size_t)(c - '0');
        digits = true;
    }
    *value = result;
    return digits;
}

static bool block_is_zero(const uint8_t *block)
{
    for (size_t index = 0U; index < OTA_TAR_BLOCK; ++index) {
        if (block[index] != 0U) return false;
    }
    return true;
}

static ota_tar_result_t take_header(ota_tar_t *tar, const ota_tar_sink_t *sink)
{
    const uint8_t *header = tar->header;
    if (block_is_zero(header)) {
        tar->ended = true;
        return OTA_TAR_OK;
    }
    if (memcmp(header + MAGIC_AT, "ustar", 5U) != 0) return OTA_TAR_BAD_HEADER;
    size_t stored = 0U;
    if (!read_octal(header + CHECKSUM_AT, CHECKSUM_LENGTH, &stored)) return OTA_TAR_BAD_HEADER;
    size_t sum = 0U;
    for (size_t index = 0U; index < OTA_TAR_BLOCK; ++index) {
        sum += (index >= CHECKSUM_AT && index < CHECKSUM_AT + CHECKSUM_LENGTH) ? (size_t)' '
                                                                               : header[index];
    }
    if (sum != stored) return OTA_TAR_BAD_HEADER;
    /* Regular files only: a link or a device written into the file system
     * would be a way out of the directory the names are kept inside. */
    if (header[TYPE_AT] != '0' && header[TYPE_AT] != '\0') return OTA_TAR_BAD_NAME;
    /* A name long enough to need the prefix field is longer than any we take. */
    if (header[PREFIX_AT] != '\0') return OTA_TAR_BAD_NAME;
    char path[NAME_LENGTH + 1U];
    memcpy(path, header + NAME_AT, NAME_LENGTH);
    path[NAME_LENGTH] = '\0';
    const char *name = NULL;
    const ota_tar_entry_t kind = ota_tar_entry(path, &name);
    if (kind == OTA_TAR_ENTRY_INVALID) return OTA_TAR_BAD_NAME;
    /* The stamp first: it says which version the files are, and that is
     * checked before any of them is kept. */
    if ((kind == OTA_TAR_ENTRY_VERSION) != (tar->entries == 0U)) return OTA_TAR_NO_VERSION;
    size_t size = 0U;
    if (!read_octal(header + SIZE_AT, SIZE_LENGTH, &size)) return OTA_TAR_BAD_HEADER;
    if (size > OTA_TAR_FILE_MAX) return OTA_TAR_TOO_BIG;

    if (!sink->begin(sink->context, kind, name, size)) return OTA_TAR_SINK;
    ++tar->entries;
    if (kind == OTA_TAR_ENTRY_VERSION) tar->saw_version = true;
    tar->file_left = size;
    tar->padding_left = (OTA_TAR_BLOCK - size % OTA_TAR_BLOCK) % OTA_TAR_BLOCK;
    tar->in_file = true;
    if (size == 0U) {
        tar->in_file = false;
        if (!sink->end(sink->context)) return OTA_TAR_SINK;
    }
    return OTA_TAR_OK;
}

ota_tar_result_t ota_tar_feed(ota_tar_t *tar, const uint8_t *bytes, size_t length,
                              const ota_tar_sink_t *sink)
{
    while (tar->result == OTA_TAR_OK && length > 0U) {
        if (tar->ended) {
            /* The second end block and whatever padding a writer adds up to
             * its record size: all zeros, and nothing to do with any file. */
            return tar->result;
        }
        if (tar->in_file) {
            const size_t take = length < tar->file_left ? length : tar->file_left;
            if (!sink->data(sink->context, bytes, take)) {
                tar->result = OTA_TAR_SINK;
                break;
            }
            bytes += take;
            length -= take;
            tar->file_left -= take;
            if (tar->file_left == 0U) {
                tar->in_file = false;
                if (!sink->end(sink->context)) tar->result = OTA_TAR_SINK;
            }
            continue;
        }
        if (tar->padding_left > 0U) {
            const size_t take = length < tar->padding_left ? length : tar->padding_left;
            bytes += take;
            length -= take;
            tar->padding_left -= take;
            continue;
        }
        const size_t want = OTA_TAR_BLOCK - tar->header_fill;
        const size_t take = length < want ? length : want;
        memcpy(tar->header + tar->header_fill, bytes, take);
        tar->header_fill += take;
        bytes += take;
        length -= take;
        if (tar->header_fill == OTA_TAR_BLOCK) {
            tar->header_fill = 0U;
            tar->result = take_header(tar, sink);
        }
    }
    return tar->result;
}

ota_tar_result_t ota_tar_finish(ota_tar_t *tar)
{
    if (tar->result != OTA_TAR_OK) return tar->result;
    if (!tar->ended || tar->in_file) return OTA_TAR_TRUNCATED;
    if (!tar->saw_version) return OTA_TAR_NO_VERSION;
    return OTA_TAR_OK;
}

const char *ota_tar_result_code(ota_tar_result_t result)
{
    switch (result) {
    case OTA_TAR_OK: return "ok";
    case OTA_TAR_BAD_HEADER: return "not_web";
    case OTA_TAR_BAD_NAME: return "bad_name";
    case OTA_TAR_TOO_BIG: return "too_big";
    case OTA_TAR_TRUNCATED: return "upload";
    case OTA_TAR_NO_VERSION: return "not_web";
    case OTA_TAR_SINK: return "write";
    }
    return "not_web";
}
