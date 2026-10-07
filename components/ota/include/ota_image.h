#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a firmware file has to say about itself before a byte of it goes to
 * flash. Pure, so the host tests can feed it real and broken headers.
 *
 * Everything is read from the start of the file: ESP-IDF's image header (24
 * bytes), the first segment's header (8), the app description (256) - which
 * the linker puts first in that segment - and right after it our own mark,
 * placed by ota_update.c in .rodata_custom_desc. The mark is what knows the
 * display: an image for another panel boots fine and draws a wrong-sized
 * screen, and with no rollback yet only a cable puts that right. */

#define OTA_IMAGE_HEAD_SIZE 512U

/* "JRD1". The number after JRD is the mark's own format. */
#define OTA_IMAGE_MARK_MAGIC 0x3144524AUL

typedef struct {
    uint32_t magic;
    uint16_t display;   /* DISPLAY_* from board_parts.h */
    uint16_t reserved;
} ota_image_mark_t;

typedef enum {
    OTA_IMAGE_OK = 0,
    OTA_IMAGE_SHORT,
    OTA_IMAGE_NOT_FIRMWARE,
    OTA_IMAGE_WRONG_CHIP,
    OTA_IMAGE_NOT_JRADIO,
    /* A jradio from before the mark - older than updates over the network,
     * so not one to go back to this way: from there only a cable comes back. */
    OTA_IMAGE_NO_MARK,
    OTA_IMAGE_WRONG_DISPLAY,
} ota_image_result_t;

typedef struct {
    char version[32];
    unsigned display;
} ota_image_info_t;

ota_image_result_t ota_image_check(const uint8_t *head, size_t length,
                                   unsigned expected_display, ota_image_info_t *info);

/* The word the web page turns into a sentence in its own language. */
const char *ota_image_result_code(ota_image_result_t result);

#ifdef __cplusplus
}
#endif
