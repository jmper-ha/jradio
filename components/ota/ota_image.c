#include "ota_image.h"

#include <string.h>

/* Offsets into the file, from esp_app_format.h and esp_app_desc.h. Spelled
 * out rather than taken from the structs so this builds on the host, where
 * those headers are not. */
#define IMAGE_MAGIC 0xE9U
#define IMAGE_CHIP_ID_AT 12U
#define CHIP_ID_ESP32S3 0x0009U
#define DESC_AT (24U + 8U)
#define DESC_MAGIC 0xABCD5432UL
#define DESC_VERSION_AT 16U
#define DESC_PROJECT_AT 48U
#define DESC_SIZE 256U
#define MARK_AT (DESC_AT + DESC_SIZE)

static uint32_t read_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 |
           (uint32_t)at[3] << 24;
}

static uint16_t read_u16(const uint8_t *at)
{
    return (uint16_t)(at[0] | at[1] << 8);
}

/* A 32-byte field the build fills with a string; the build NUL-terminates
 * it, but a file off the network is not the build. */
static void copy_field(char out[32], const uint8_t *at)
{
    memcpy(out, at, 31U);
    out[31] = '\0';
}

ota_image_result_t ota_image_check(const uint8_t *head, size_t length,
                                   unsigned expected_display, ota_image_info_t *info)
{
    if (info != NULL) memset(info, 0, sizeof(*info));
    if (head == NULL || length < 1U) return OTA_IMAGE_SHORT;
    if (head[0] != IMAGE_MAGIC) return OTA_IMAGE_NOT_FIRMWARE;
    if (length < MARK_AT + sizeof(ota_image_mark_t)) return OTA_IMAGE_SHORT;
    if (read_u32(head + DESC_AT) != DESC_MAGIC) return OTA_IMAGE_NOT_FIRMWARE;
    if (read_u16(head + IMAGE_CHIP_ID_AT) != CHIP_ID_ESP32S3) return OTA_IMAGE_WRONG_CHIP;

    char project[32];
    copy_field(project, head + DESC_AT + DESC_PROJECT_AT);
    if (strcmp(project, "jradio") != 0) return OTA_IMAGE_NOT_JRADIO;

    ota_image_info_t found;
    memset(&found, 0, sizeof(found));
    copy_field(found.version, head + DESC_AT + DESC_VERSION_AT);
    if (read_u32(head + MARK_AT) != OTA_IMAGE_MARK_MAGIC) {
        if (info != NULL) *info = found;
        return OTA_IMAGE_NO_MARK;
    }
    found.display = read_u16(head + MARK_AT + 4U);
    if (info != NULL) *info = found;
    return found.display == expected_display ? OTA_IMAGE_OK : OTA_IMAGE_WRONG_DISPLAY;
}

const char *ota_image_result_code(ota_image_result_t result)
{
    switch (result) {
    case OTA_IMAGE_OK: return "ok";
    case OTA_IMAGE_SHORT: return "short";
    case OTA_IMAGE_NOT_FIRMWARE: return "not_firmware";
    case OTA_IMAGE_WRONG_CHIP: return "wrong_chip";
    case OTA_IMAGE_NOT_JRADIO: return "not_jradio";
    case OTA_IMAGE_NO_MARK: return "no_mark";
    case OTA_IMAGE_WRONG_DISPLAY: return "wrong_display";
    }
    return "not_firmware";
}
