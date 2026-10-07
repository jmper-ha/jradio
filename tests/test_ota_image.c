#include "ota_image.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A head laid out the way ESP-IDF writes one, with only the bytes the check
 * reads filled in. */
static void put_u32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static void make_head(uint8_t head[OTA_IMAGE_HEAD_SIZE], const char *project,
                      const char *version, unsigned display, int marked)
{
    memset(head, 0, OTA_IMAGE_HEAD_SIZE);
    head[0] = 0xE9;
    head[12] = 0x09;  /* ESP32-S3 */
    put_u32(head + 32, 0xABCD5432UL);
    strncpy((char *)head + 32 + 16, version, 31);
    strncpy((char *)head + 32 + 48, project, 31);
    if (marked) {
        put_u32(head + 288, OTA_IMAGE_MARK_MAGIC);
        head[292] = (uint8_t)display;
        head[293] = (uint8_t)(display >> 8);
    }
}

static void test_our_own_image_for_this_panel_passes(void)
{
    uint8_t head[OTA_IMAGE_HEAD_SIZE];
    make_head(head, "jradio", "v1.6.0", 5, 1);
    ota_image_info_t info;
    assert(ota_image_check(head, sizeof(head), 5, &info) == OTA_IMAGE_OK);
    assert(strcmp(info.version, "v1.6.0") == 0);
    assert(info.display == 5);
}

static void test_another_panel_is_named_not_installed(void)
{
    uint8_t head[OTA_IMAGE_HEAD_SIZE];
    make_head(head, "jradio", "v1.6.0", 3, 1);
    ota_image_info_t info;
    assert(ota_image_check(head, sizeof(head), 5, &info) == OTA_IMAGE_WRONG_DISPLAY);
    assert(info.display == 3);
    assert(strcmp(ota_image_result_code(OTA_IMAGE_WRONG_DISPLAY), "wrong_display") == 0);
}

static void test_a_build_from_before_the_mark_is_refused(void)
{
    uint8_t head[OTA_IMAGE_HEAD_SIZE];
    make_head(head, "jradio", "v1.5.5", 5, 0);
    ota_image_info_t info;
    assert(ota_image_check(head, sizeof(head), 5, &info) == OTA_IMAGE_NO_MARK);
    assert(strcmp(info.version, "v1.5.5") == 0);
}

static void test_files_that_are_not_ours_are_told_apart(void)
{
    uint8_t head[OTA_IMAGE_HEAD_SIZE];
    make_head(head, "jradio", "v1.6.0", 5, 1);
    head[0] = 'P';  /* a zip, a picture, anything */
    assert(ota_image_check(head, sizeof(head), 5, NULL) == OTA_IMAGE_NOT_FIRMWARE);

    /* The bootloader starts with the same magic and has no app description. */
    make_head(head, "jradio", "v1.6.0", 5, 1);
    put_u32(head + 32, 0);
    assert(ota_image_check(head, sizeof(head), 5, NULL) == OTA_IMAGE_NOT_FIRMWARE);

    make_head(head, "jradio", "v1.6.0", 5, 1);
    head[12] = 0x00;  /* the plain ESP32: the jradio-bt module's firmware */
    assert(ota_image_check(head, sizeof(head), 5, NULL) == OTA_IMAGE_WRONG_CHIP);

    make_head(head, "esphome", "2026.9.0", 5, 1);
    assert(ota_image_check(head, sizeof(head), 5, NULL) == OTA_IMAGE_NOT_JRADIO);
}

static void test_a_cut_head_is_short_not_garbage(void)
{
    uint8_t head[OTA_IMAGE_HEAD_SIZE];
    make_head(head, "jradio", "v1.6.0", 5, 1);
    assert(ota_image_check(head, 200, 5, NULL) == OTA_IMAGE_SHORT);
    assert(ota_image_check(head, 0, 5, NULL) == OTA_IMAGE_SHORT);
    assert(ota_image_check(NULL, 512, 5, NULL) == OTA_IMAGE_SHORT);
}

static void test_an_unterminated_version_stays_inside_its_field(void)
{
    uint8_t head[OTA_IMAGE_HEAD_SIZE];
    make_head(head, "jradio", "", 5, 1);
    memset(head + 32 + 16, 'x', 32);
    ota_image_info_t info;
    assert(ota_image_check(head, sizeof(head), 5, &info) == OTA_IMAGE_OK);
    assert(strlen(info.version) == 31);
}

int main(void)
{
    test_our_own_image_for_this_panel_passes();
    test_another_panel_is_named_not_installed();
    test_a_build_from_before_the_mark_is_refused();
    test_files_that_are_not_ours_are_told_apart();
    test_a_cut_head_is_short_not_garbage();
    test_an_unterminated_version_stays_inside_its_field();
    puts("ota image tests passed");
    return 0;
}
