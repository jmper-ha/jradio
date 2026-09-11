#include "config_archive.h"

#include <string.h>

#define ZIP_SIGNATURE_LOCAL 0x04034B50UL
#define ZIP_SIGNATURE_CENTRAL 0x02014B50UL
#define ZIP_SIGNATURE_END 0x06054B50UL
#define ZIP_LOCAL_HEADER_LEN 30U
#define ZIP_CENTRAL_HEADER_LEN 46U
#define ZIP_END_RECORD_LEN 22U
#define ZIP_METHOD_STORED 0U
#define ZIP_METHOD_DEFLATE 8U
/* General purpose bit 3: the sizes are not in the local header but in a
 * descriptor after the data. Nothing this device writes sets it, and honouring
 * it would mean scanning for a signature inside the payload. */
#define ZIP_FLAG_DATA_DESCRIPTOR 0x0008U

const char *config_archive_member_file(config_archive_member_t member)
{
    switch (member) {
    case CONFIG_ARCHIVE_MEMBER_WIFI:
        return "wifi.json";
    case CONFIG_ARCHIVE_MEMBER_SETTINGS:
        return "settings.csv";
    case CONFIG_ARCHIVE_MEMBER_YANDEX:
        return "yandex.json";
    case CONFIG_ARCHIVE_MEMBER_WEATHER:
        return "weather.json";
    case CONFIG_ARCHIVE_MEMBER_UNKNOWN:
        break;
    }
    return NULL;
}

static char archive_lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static bool archive_name_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (archive_lower(*left) != archive_lower(*right)) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

config_archive_member_t config_archive_member_from_file(const char *name)
{
    if (name == NULL) return CONFIG_ARCHIVE_MEMBER_UNKNOWN;
    /* Both separators, because the archive may have been repacked on Windows
     * and a backslash there is still a directory, not part of the name. */
    for (const char *cursor = name; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    }
    for (config_archive_member_t member = CONFIG_ARCHIVE_MEMBER_FIRST;
         member <= CONFIG_ARCHIVE_MEMBER_LAST; ++member) {
        if (archive_name_equals(name, config_archive_member_file(member))) return member;
    }
    return CONFIG_ARCHIVE_MEMBER_UNKNOWN;
}

static bool archive_is_space(unsigned char character)
{
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

static bool archive_json_is_plausible(const unsigned char *bytes, size_t size)
{
    size_t start = 0U;
    while (start < size && archive_is_space(bytes[start])) ++start;
    size_t end = size;
    while (end > start && archive_is_space(bytes[end - 1U])) --end;
    if (end - start < 2U) return false;
    if (bytes[start] != '{' || bytes[end - 1U] != '}') return false;
    /* Both files this covers are written with a version first, and both
     * loaders refuse anything else. Asking for it here turns "some other JSON
     * renamed" into a refusal before the write rather than an empty
     * configuration after the next boot. */
    static const char marker[] = "\"version\"";
    const size_t marker_length = sizeof(marker) - 1U;
    if (end - start < marker_length) return false;
    for (size_t index = start; index + marker_length <= end; ++index) {
        if (memcmp(bytes + index, marker, marker_length) == 0) return true;
    }
    return false;
}

static bool archive_csv_is_plausible(const unsigned char *bytes, size_t size)
{
    if (size == 0U) return false;
    bool separator_seen = false;
    for (size_t index = 0U; index < size; ++index) {
        const unsigned char character = bytes[index];
        /* Values are UTF-8 - a station name or a path off the drive - so
         * anything above ASCII passes. Control characters do not: they are how
         * a binary file renamed settings.csv shows up. */
        if (character < 0x20U && character != '\t' && character != '\r' && character != '\n') {
            return false;
        }
        if (character == 0x7FU) return false;
        if (character == ',') separator_seen = true;
    }
    return separator_seen;
}

bool config_archive_member_is_plausible(config_archive_member_t member, const void *data,
                                        size_t size)
{
    if (data == NULL) return false;
    const unsigned char *bytes = data;
    switch (member) {
    case CONFIG_ARCHIVE_MEMBER_WIFI:
    case CONFIG_ARCHIVE_MEMBER_YANDEX:
    case CONFIG_ARCHIVE_MEMBER_WEATHER:
        return archive_json_is_plausible(bytes, size);
    case CONFIG_ARCHIVE_MEMBER_SETTINGS:
        return archive_csv_is_plausible(bytes, size);
    case CONFIG_ARCHIVE_MEMBER_UNKNOWN:
        break;
    }
    return false;
}

uint32_t config_archive_crc32(const void *data, size_t size)
{
    /* Bit by bit, no table: three files of a few kilobytes each are microseconds
     * either way, and a 1 KB table in flash is not worth the byte count. */
    const unsigned char *bytes = data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0U; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

uint16_t config_archive_dos_date(int year, int month, int day)
{
    if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 || day > 31) {
        return CONFIG_ARCHIVE_DOS_DATE_MIN;
    }
    return (uint16_t)(((unsigned)(year - 1980) << 9) | ((unsigned)month << 5) | (unsigned)day);
}

uint16_t config_archive_dos_time(int hour, int minute, int second)
{
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return 0U;
    }
    return (uint16_t)(((unsigned)hour << 11) | ((unsigned)minute << 5) | ((unsigned)second / 2U));
}

void config_archive_writer_init(config_archive_writer_t *writer, void *buffer, size_t capacity,
                                uint16_t dos_date, uint16_t dos_time)
{
    if (writer == NULL) return;
    *writer = (config_archive_writer_t){0};
    writer->buffer = buffer;
    writer->capacity = buffer == NULL ? 0U : capacity;
    writer->dos_date = dos_date;
    writer->dos_time = dos_time;
    writer->failed = buffer == NULL;
}

static bool archive_room(config_archive_writer_t *writer, size_t bytes)
{
    if (writer->failed || writer->capacity - writer->length < bytes) {
        writer->failed = true;
        return false;
    }
    return true;
}

static void archive_put16(config_archive_writer_t *writer, uint16_t value)
{
    writer->buffer[writer->length++] = (uint8_t)(value & 0xFFU);
    writer->buffer[writer->length++] = (uint8_t)((value >> 8) & 0xFFU);
}

static void archive_put32(config_archive_writer_t *writer, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        writer->buffer[writer->length++] = (uint8_t)((value >> shift) & 0xFFU);
    }
}

bool config_archive_writer_add(config_archive_writer_t *writer, const char *name,
                               const void *data, size_t size)
{
    if (writer == NULL) return false;
    if (name == NULL || data == NULL || writer->count >= CONFIG_ARCHIVE_MEMBER_MAX) {
        writer->failed = true;
        return false;
    }
    const size_t name_length = strlen(name);
    if (name_length == 0U || name_length >= CONFIG_ARCHIVE_NAME_MAX) {
        writer->failed = true;
        return false;
    }
    if (!archive_room(writer, ZIP_LOCAL_HEADER_LEN + name_length + size)) return false;

    const uint32_t offset = (uint32_t)writer->length;
    const uint32_t crc = config_archive_crc32(data, size);
    archive_put32(writer, ZIP_SIGNATURE_LOCAL);
    archive_put16(writer, 20U);
    archive_put16(writer, 0U);
    archive_put16(writer, ZIP_METHOD_STORED);
    archive_put16(writer, writer->dos_time);
    archive_put16(writer, writer->dos_date);
    archive_put32(writer, crc);
    archive_put32(writer, (uint32_t)size);
    archive_put32(writer, (uint32_t)size);
    archive_put16(writer, (uint16_t)name_length);
    archive_put16(writer, 0U);
    memcpy(writer->buffer + writer->length, name, name_length);
    writer->length += name_length;
    if (size > 0U) {
        memcpy(writer->buffer + writer->length, data, size);
        writer->length += size;
    }

    memcpy(writer->entries[writer->count].name, name, name_length + 1U);
    writer->entries[writer->count].crc = crc;
    writer->entries[writer->count].size = (uint32_t)size;
    writer->entries[writer->count].offset = offset;
    ++writer->count;
    return true;
}

bool config_archive_writer_finish(config_archive_writer_t *writer, size_t *length)
{
    if (writer == NULL || writer->failed) return false;
    const size_t directory_offset = writer->length;
    for (size_t index = 0U; index < writer->count; ++index) {
        const size_t name_length = strlen(writer->entries[index].name);
        if (!archive_room(writer, ZIP_CENTRAL_HEADER_LEN + name_length)) return false;
        archive_put32(writer, ZIP_SIGNATURE_CENTRAL);
        archive_put16(writer, 20U);
        archive_put16(writer, 20U);
        archive_put16(writer, 0U);
        archive_put16(writer, ZIP_METHOD_STORED);
        archive_put16(writer, writer->dos_time);
        archive_put16(writer, writer->dos_date);
        archive_put32(writer, writer->entries[index].crc);
        archive_put32(writer, writer->entries[index].size);
        archive_put32(writer, writer->entries[index].size);
        archive_put16(writer, (uint16_t)name_length);
        archive_put16(writer, 0U);
        archive_put16(writer, 0U);
        archive_put16(writer, 0U);
        archive_put16(writer, 0U);
        archive_put32(writer, 0U);
        archive_put32(writer, writer->entries[index].offset);
        memcpy(writer->buffer + writer->length, writer->entries[index].name, name_length);
        writer->length += name_length;
    }
    /* Taken before the end record is written: writer->length moves while the
     * record is being laid down, and the size the record has to carry is the
     * directory's, not the directory's plus however much of the record has
     * already gone out. */
    const size_t directory_size = writer->length - directory_offset;
    if (!archive_room(writer, ZIP_END_RECORD_LEN)) return false;
    archive_put32(writer, ZIP_SIGNATURE_END);
    archive_put16(writer, 0U);
    archive_put16(writer, 0U);
    archive_put16(writer, (uint16_t)writer->count);
    archive_put16(writer, (uint16_t)writer->count);
    archive_put32(writer, (uint32_t)directory_size);
    archive_put32(writer, (uint32_t)directory_offset);
    archive_put16(writer, 0U);
    if (length != NULL) *length = writer->length;
    return true;
}

static uint16_t archive_get16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t archive_get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

bool config_archive_looks_like_zip(const void *data, size_t size)
{
    if (data == NULL || size < 4U) return false;
    const uint32_t signature = archive_get32(data);
    /* An empty archive starts with the end record, and a zip that has been
     * repacked can start with a central directory entry. Neither carries a
     * member, but both are a zip - and telling the user "this is not an
     * archive" about a file their file manager calls one is worse than
     * telling them it holds nothing of ours. */
    return signature == ZIP_SIGNATURE_LOCAL || signature == ZIP_SIGNATURE_CENTRAL ||
           signature == ZIP_SIGNATURE_END;
}

void config_archive_reader_init(config_archive_reader_t *reader, const void *data, size_t size)
{
    if (reader == NULL) return;
    reader->data = data;
    reader->size = data == NULL ? 0U : size;
    reader->offset = 0U;
}

config_archive_read_t config_archive_reader_next(config_archive_reader_t *reader,
                                                 config_archive_entry_t *entry)
{
    if (reader == NULL || entry == NULL || reader->data == NULL) {
        return CONFIG_ARCHIVE_READ_MALFORMED;
    }
    *entry = (config_archive_entry_t){0};
    if (reader->size - reader->offset < 4U) {
        /* Anything shorter than a signature at the end is padding, not a
         * truncated member: the last member was already handed over. */
        return reader->offset >= reader->size ? CONFIG_ARCHIVE_READ_END
                                              : CONFIG_ARCHIVE_READ_MALFORMED;
    }
    const uint8_t *header = reader->data + reader->offset;
    const uint32_t signature = archive_get32(header);
    if (signature == ZIP_SIGNATURE_CENTRAL || signature == ZIP_SIGNATURE_END) {
        /* The central directory begins: every member has been walked. */
        reader->offset = reader->size;
        return CONFIG_ARCHIVE_READ_END;
    }
    if (signature != ZIP_SIGNATURE_LOCAL) return CONFIG_ARCHIVE_READ_MALFORMED;
    if (reader->size - reader->offset < ZIP_LOCAL_HEADER_LEN) {
        return CONFIG_ARCHIVE_READ_MALFORMED;
    }

    const uint16_t flags = archive_get16(header + 6);
    const uint16_t method = archive_get16(header + 8);
    const uint32_t stored_crc = archive_get32(header + 14);
    const uint32_t compressed = archive_get32(header + 18);
    const uint32_t uncompressed = archive_get32(header + 22);
    const uint16_t name_length = archive_get16(header + 26);
    const uint16_t extra_length = archive_get16(header + 28);

    const size_t body = ZIP_LOCAL_HEADER_LEN + (size_t)name_length + (size_t)extra_length;
    if (reader->size - reader->offset < body) return CONFIG_ARCHIVE_READ_MALFORMED;
    if (reader->size - reader->offset - body < compressed) return CONFIG_ARCHIVE_READ_MALFORMED;
    if ((flags & ZIP_FLAG_DATA_DESCRIPTOR) != 0U) return CONFIG_ARCHIVE_READ_MALFORMED;
    if (method != ZIP_METHOD_STORED && method != ZIP_METHOD_DEFLATE) {
        return CONFIG_ARCHIVE_READ_COMPRESSED;
    }
    if (method == ZIP_METHOD_STORED && compressed != uncompressed) {
        return CONFIG_ARCHIVE_READ_MALFORMED;
    }

    const uint8_t *payload = header + body;
    if (method == ZIP_METHOD_STORED && config_archive_crc32(payload, uncompressed) != stored_crc) {
        return CONFIG_ARCHIVE_READ_CRC;
    }
    if (name_length < CONFIG_ARCHIVE_NAME_MAX) {
        memcpy(entry->name, header + ZIP_LOCAL_HEADER_LEN, name_length);
        entry->name[name_length] = '\0';
        /* A NUL inside the name would make strcmp compare a prefix, so the
         * name a reader sees and the name we match on could differ. */
        if (strlen(entry->name) != name_length) entry->name[0] = '\0';
    }
    entry->data = payload;
    entry->size = compressed;
    entry->original_size = uncompressed;
    entry->deflated = method == ZIP_METHOD_DEFLATE;
    entry->crc = stored_crc;
    reader->offset += body + compressed;
    return CONFIG_ARCHIVE_READ_OK;
}
