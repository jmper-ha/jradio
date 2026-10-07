#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "ota_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The web half of an update: www.tar unpacked beside the live pages and
 * swapped in when the update is confirmed.
 *
 * Plain POSIX file calls under a root that is /littlefs on the device and a
 * temporary directory in the host tests. The live directory is never written
 * into: the archive goes to <root>/www.new, and the swap is two renames, so a
 * power cut at any point leaves either the old pages or the new ones -
 * ota_www_boot_cleanup() finishes or undoes a swap that was cut short. The
 * stamp travels inside the new directory and is moved to config/ last. */

#define OTA_WWW_PATH_MAX 96U

typedef struct {
    char root[40];
    ota_tar_t tar;
    FILE *file;
    char path[OTA_WWW_PATH_MAX];
    char version[32];
    bool failed;
} ota_www_t;

/* Clears whatever an earlier upload left in www.new and starts afresh. */
bool ota_www_begin(ota_www_t *www, const char *root);
ota_tar_result_t ota_www_write(ota_www_t *www, const void *bytes, size_t length);
/* The archive is whole, holds the pages a device cannot do without, and
 * carries a version; www->version is filled. Anything else is discarded. */
ota_tar_result_t ota_www_finish(ota_www_t *www);
/* An upload that broke off, or an update declined: www.new goes. */
void ota_www_abort(ota_www_t *www);
void ota_www_discard(const char *root);

bool ota_www_apply(const char *root);
void ota_www_boot_cleanup(const char *root);

/* The "version" of a stamp as stamp_version.py writes it. Pure. */
bool ota_www_stamp_version(const char *stamp, size_t length, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
