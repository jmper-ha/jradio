#include "version_info.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"

/* Where tools/stamp_version.py lands once the image is flashed. Beside
 * settings.csv rather than under www/: it is read by the device and is not a
 * page, and a file in www/ is one the web server would serve by accident. */
#define VERSION_INFO_WEB_STAMP_PATH "/littlefs/config/version.json"

/* The stamp is two short strings and its own punctuation - about sixty bytes.
 * This is room for that and then some, on the stack of whoever asked, for the
 * moment it takes to parse. */
#define VERSION_INFO_STAMP_MAX 256U

static void copy_field(char *out, size_t out_size, const char *value)
{
    snprintf(out, out_size, "%s", value == NULL ? "" : value);
}

static void read_web_stamp(version_info_part_t *part)
{
    memset(part, 0, sizeof(*part));
    FILE *file = fopen(VERSION_INFO_WEB_STAMP_PATH, "rb");
    /* Absent is a real answer, not a failure to report: an image flashed
     * before the stamp existed has no such file, and that is precisely the
     * case worth telling the reader about. */
    if (file == NULL) return;
    char buffer[VERSION_INFO_STAMP_MAX];
    const size_t read = fread(buffer, 1U, sizeof(buffer), file);
    fclose(file);
    (void)version_info_parse(buffer, read, part);
}

void version_info_read(version_info_t *info)
{
    if (info == NULL) return;
    memset(info, 0, sizeof(*info));

    /* Nothing is generated for this half: ESP-IDF runs `git describe` at
     * configure time and writes the result into the app header, which is what
     * the boot log prints as "App version". */
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc != NULL) {
        copy_field(info->firmware.version, sizeof(info->firmware.version), desc->version);
        copy_field(info->firmware.built, sizeof(info->firmware.built), desc->date);
        copy_field(info->idf, sizeof(info->idf), desc->idf_ver);
        info->firmware.present = info->firmware.version[0] != '\0';
    }

    read_web_stamp(&info->web);
}
