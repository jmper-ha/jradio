#include "ota_offer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The manifest as tools/ota_manifest.py writes it, two displays and the web
 * files. */
static const char k_manifest[] =
    "{\"format\":1,\"version\":\"v1.6.0\","
    "\"notes\":{\"ru\":\"Новое:\\n- Обновление по сети.\",\"en\":\"New:\\n- Updates.\"},"
    "\"www\":{\"url\":\"https://github.com/o/jradio/releases/download/v1.6.0/ota-www.tar\","
    "\"size\":604160,\"sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\"},"
    "\"firmware\":{"
    "\"ili9341_320_240\":{\"url\":\"https://github.com/o/jradio/releases/download/v1.6.0/"
    "ota-ili9341_320_240.bin\",\"size\":2780368,"
    "\"sha256\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\"},"
    "\"st7789_320_240\":{\"url\":\"https://github.com/o/jradio/releases/download/v1.6.0/"
    "ota-st7789_320_240.bin\",\"size\":2780000,"
    "\"sha256\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\"}}}";

static void test_the_offer_is_this_displays_firmware_and_the_web_files(void)
{
    ota_offer_t offer;
    assert(ota_offer_parse(k_manifest, strlen(k_manifest), "st7789_320_240", &offer) ==
           OTA_OFFER_OK);
    assert(strcmp(offer.version, "v1.6.0") == 0);
    assert(strstr(offer.app.url, "ota-st7789_320_240.bin") != NULL);
    assert(offer.app.size == 2780000U);
    assert(offer.app.sha256[0] == 0xFF && offer.app.sha256[31] == 0x00);
    assert(strstr(offer.www.url, "ota-www.tar") != NULL);
    assert(offer.www.size == 604160U);
    assert(offer.www.sha256[1] == 0x11);
    assert(strcmp(offer.notes_ru, "Новое:\n- Обновление по сети.") == 0);
    assert(strcmp(offer.notes_en, "New:\n- Updates.") == 0);
    ota_offer_free(&offer);
}

static void test_a_release_without_this_display_is_not_an_offer(void)
{
    ota_offer_t offer;
    assert(ota_offer_parse(k_manifest, strlen(k_manifest), "ili9488_480_320", &offer) ==
           OTA_OFFER_NO_DISPLAY);
    assert(offer.notes_ru == NULL);
}

static void test_broken_and_later_manifests_are_told_apart(void)
{
    ota_offer_t offer;
    char text[sizeof(k_manifest) + 16];
    assert(ota_offer_parse("<html>Not Found</html>", 22, "st7789_320_240", &offer) ==
           OTA_OFFER_MALFORMED);
    snprintf(text, sizeof(text), "%s", k_manifest);
    memcpy(strstr(text, "\"format\":1"), "\"format\":2", 10);
    assert(ota_offer_parse(text, strlen(text), "st7789_320_240", &offer) == OTA_OFFER_FORMAT);
    /* A digest one character short. */
    snprintf(text, sizeof(text), "%s", k_manifest);
    char *digest = strstr(text, "ffeeddcc");
    digest[0] = '"';
    assert(ota_offer_parse(text, strlen(text), "ili9341_320_240", &offer) == OTA_OFFER_MALFORMED);
    /* A download that is not one. */
    snprintf(text, sizeof(text), "%s", k_manifest);
    memcpy(strstr(text, "https://github.com/o/jradio/releases/download/v1.6.0/ota-www"), "file:///", 8);
    assert(ota_offer_parse(text, strlen(text), "st7789_320_240", &offer) == OTA_OFFER_MALFORMED);
}

static void test_versions_order_as_git_describe_writes_them(void)
{
    bool known = false;
    assert(ota_version_compare("v1.6.0", "v1.5.5", &known) > 0 && known);
    assert(ota_version_compare("v1.10.0", "v1.9.9", &known) > 0);
    assert(ota_version_compare("v1.5.5", "v1.5.5-dirty", &known) == 0 && known);
    /* A build past a tag is after it and before the next one. */
    assert(ota_version_compare("v1.5.5-4-ge5fcf3d", "v1.5.5", &known) > 0);
    assert(ota_version_compare("v1.5.5-4-ge5fcf3d-dirty", "v1.5.6", &known) < 0);
    assert(ota_version_compare("v1.5.5-12-gabc", "v1.5.5-4-gdef", &known) > 0);
    /* No tags fetched: the version is a bare hash. */
    assert(ota_version_compare("e5fcf3d", "v1.5.5", &known) == 0 && !known);
}

static void test_what_is_offered(void)
{
    assert(ota_offer_is_wanted("v1.5.5", "v1.6.0", ""));
    assert(!ota_offer_is_wanted("v1.6.0", "v1.6.0", ""));
    assert(!ota_offer_is_wanted("v1.6.1", "v1.6.0", ""));
    /* Skipped is that version only: the next one is offered again. */
    assert(!ota_offer_is_wanted("v1.5.5", "v1.6.0", "v1.6.0"));
    assert(ota_offer_is_wanted("v1.5.5", "v1.6.1", "v1.6.0"));
    /* A bench build past the release is not offered the release. */
    assert(!ota_offer_is_wanted("v1.6.0-3-gabc1234-dirty", "v1.6.0", ""));
    /* A firmware from no release at all is offered one. */
    assert(ota_offer_is_wanted("e5fcf3d", "v1.6.0", NULL));
    assert(!ota_offer_is_wanted("v1.5.5", "", ""));
    assert(!ota_offer_is_wanted("v1.5.5", "nightly", ""));
}

int main(void)
{
    test_the_offer_is_this_displays_firmware_and_the_web_files();
    test_a_release_without_this_display_is_not_an_offer();
    test_broken_and_later_manifests_are_told_apart();
    test_versions_order_as_git_describe_writes_them();
    test_what_is_offered();
    puts("ota offer tests passed");
    return 0;
}
