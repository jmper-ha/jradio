#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The web files that travel with a firmware: a plain ustar archive, read as it
 * arrives so neither the archive nor one of its files is ever whole in RAM.
 * Pure, so the host tests can feed it broken and hostile archives.
 *
 * What may be in it is narrow on purpose - it is written into the file system
 * by a request from a network with no password on it:
 *   version.json      the web stamp, first, moved to /littlefs/config on install
 *   www/<name>        one flat directory, names of letters, digits, '.', '_', '-'
 * Anything else - a path that climbs out, a link, a device, a second directory
 * level - refuses the whole archive. tools/pack_www.py writes exactly this. */

#define OTA_TAR_BLOCK 512U
#define OTA_TAR_NAME_MAX 48U
/* The largest web file is settings.js at about 90 KB; four times that is room
 * to grow, and a bound on what one entry can fill the partition with. */
#define OTA_TAR_FILE_MAX (384U * 1024U)

typedef enum {
    OTA_TAR_ENTRY_INVALID = 0,
    OTA_TAR_ENTRY_VERSION,
    OTA_TAR_ENTRY_WEB,
} ota_tar_entry_t;

typedef enum {
    OTA_TAR_OK = 0,
    OTA_TAR_BAD_HEADER,
    OTA_TAR_BAD_NAME,
    OTA_TAR_TOO_BIG,
    OTA_TAR_TRUNCATED,
    OTA_TAR_NO_VERSION,
    OTA_TAR_SINK,
} ota_tar_result_t;

/* Where the files go. `name` is the bare file name for a web file and
 * "version.json" for the stamp. Returning false stops the archive with
 * OTA_TAR_SINK. */
typedef struct {
    bool (*begin)(void *context, ota_tar_entry_t kind, const char *name, size_t size);
    bool (*data)(void *context, const uint8_t *bytes, size_t length);
    bool (*end)(void *context);
    void *context;
} ota_tar_sink_t;

typedef struct {
    uint8_t header[OTA_TAR_BLOCK];
    size_t header_fill;
    size_t file_left;
    size_t padding_left;
    size_t entries;
    bool in_file;
    bool ended;
    bool saw_version;
    ota_tar_result_t result;
} ota_tar_t;

void ota_tar_init(ota_tar_t *tar);
ota_tar_result_t ota_tar_feed(ota_tar_t *tar, const uint8_t *bytes, size_t length,
                              const ota_tar_sink_t *sink);
/* After the last byte: an archive that stopped mid-file, or before its end
 * blocks, is not one to install. */
ota_tar_result_t ota_tar_finish(ota_tar_t *tar);

/* What a name in the archive stands for, and the file name it is written as. */
ota_tar_entry_t ota_tar_entry(const char *path, const char **file_name);

const char *ota_tar_result_code(ota_tar_result_t result);

#ifdef __cplusplus
}
#endif
