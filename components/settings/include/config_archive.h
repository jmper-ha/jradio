#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The three files that make a device this device: the networks it knows, how
 * it is set up, and the Yandex token. A backup carries them as one zip, so a
 * restore does not depend on the browser having kept three separate downloads
 * together - and so one file out of the archive can still be restored on its
 * own.
 *
 * Entries are stored, never deflated. The files are a few kilobytes each, so
 * compression would buy nothing, and an inflater on the device would be a
 * decompression bomb waiting for the one upload nobody checked. Every zip
 * reader opens stored entries; the device only has to read back what it wrote
 * itself.
 *
 * Everything here is pure: no filesystem, no ESP-IDF. The buffer belongs to
 * the caller. */

#define CONFIG_ARCHIVE_MEMBER_MAX 3U
/* Long enough for the names below with a directory prefix; anything longer
 * belongs to some other archive and is skipped rather than truncated into a
 * name that might collide with ours. */
#define CONFIG_ARCHIVE_NAME_MAX 48U

typedef enum {
    CONFIG_ARCHIVE_MEMBER_UNKNOWN = 0,
    CONFIG_ARCHIVE_MEMBER_WIFI,
    CONFIG_ARCHIVE_MEMBER_SETTINGS,
    CONFIG_ARCHIVE_MEMBER_YANDEX,
} config_archive_member_t;

/* "wifi.json", "settings.csv", "yandex.json"; NULL for UNKNOWN. */
const char *config_archive_member_file(config_archive_member_t member);

/* Maps a file name onto a member. A leading directory is ignored, so both
 * "wifi.json" and "config/wifi.json" arrive at the same place - a zip made by
 * a file manager out of the config directory keeps the folder in the path.
 * Case is ignored: Windows hands back the name it feels like. */
config_archive_member_t config_archive_member_from_file(const char *name);

/* Whether the bytes could be that member at all. This is a gate in front of
 * the filesystem, not a parser: it exists so a restore refuses a picture named
 * wifi.json before it overwrites the real one, while the loaders keep doing
 * the real validation at boot, where a damaged file already degrades safely to
 * "no networks" instead of a boot loop. */
bool config_archive_member_is_plausible(config_archive_member_t member, const void *data,
                                        size_t size);

uint32_t config_archive_crc32(const void *data, size_t size);

/* MS-DOS packed date and time, the only stamp a zip entry has. The device has
 * no RTC, so a backup taken before SNTP has answered gets the epoch these
 * fields can express at all - 1980-01-01, which readers accept, unlike the
 * zero that a memset would leave. */
#define CONFIG_ARCHIVE_DOS_DATE_MIN 0x0021U
uint16_t config_archive_dos_date(int year, int month, int day);
uint16_t config_archive_dos_time(int hour, int minute, int second);

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    uint16_t dos_date;
    uint16_t dos_time;
    size_t count;
    bool failed;
    struct {
        char name[CONFIG_ARCHIVE_NAME_MAX];
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    } entries[CONFIG_ARCHIVE_MEMBER_MAX];
} config_archive_writer_t;

/* How much room the archive needs for members of `payload` bytes in total.
 * Derived rather than typed: a cap guessed by hand is the thing that silently
 * truncates the day a file grows. */
#define CONFIG_ARCHIVE_OVERHEAD                                                    \
    (CONFIG_ARCHIVE_MEMBER_MAX * (30U + 46U + 2U * CONFIG_ARCHIVE_NAME_MAX) + 22U)
#define CONFIG_ARCHIVE_CAPACITY(payload) ((size_t)(payload) + CONFIG_ARCHIVE_OVERHEAD)

void config_archive_writer_init(config_archive_writer_t *writer, void *buffer, size_t capacity,
                                uint16_t dos_date, uint16_t dos_time);
bool config_archive_writer_add(config_archive_writer_t *writer, const char *name,
                               const void *data, size_t size);
/* Writes the central directory. False if any add failed - the archive is then
 * incomplete and must not be sent, because a truncated zip reads as a valid
 * shorter backup rather than as an error. */
bool config_archive_writer_finish(config_archive_writer_t *writer, size_t *length);

typedef enum {
    CONFIG_ARCHIVE_READ_OK = 0,
    CONFIG_ARCHIVE_READ_END,
    CONFIG_ARCHIVE_READ_MALFORMED,
    /* Packed with something that is neither stored nor deflate. Distinct from
     * malformed because the answer to the user is different: the archive is
     * fine, this device just cannot unpack it - re-zip it, or upload the one
     * file that is wanted. */
    CONFIG_ARCHIVE_READ_COMPRESSED,
    /* Only ever returned for a stored entry. A deflated one is checked by the
     * caller after inflating, against entry.crc. */
    CONFIG_ARCHIVE_READ_CRC,
} config_archive_read_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
} config_archive_reader_t;

typedef struct {
    /* Empty when the entry's name did not fit CONFIG_ARCHIVE_NAME_MAX: the
     * entry is still walked over, it simply cannot be one of ours. */
    char name[CONFIG_ARCHIVE_NAME_MAX];
    /* The bytes as the archive holds them, and how many. For a stored entry
     * these are the file; for a deflated one they are the raw deflate stream
     * and `original_size` says how much room inflating needs. */
    const uint8_t *data;
    size_t size;
    size_t original_size;
    bool deflated;
    /* Of the file, not of what is in the archive - which is why it is handed
     * out: it is the caller's only check that an inflate produced the file
     * that was packed. */
    uint32_t crc;
} config_archive_entry_t;

bool config_archive_looks_like_zip(const void *data, size_t size);
void config_archive_reader_init(config_archive_reader_t *reader, const void *data, size_t size);
config_archive_read_t config_archive_reader_next(config_archive_reader_t *reader,
                                                 config_archive_entry_t *entry);
