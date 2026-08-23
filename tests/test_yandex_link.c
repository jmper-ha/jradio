#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_link.h"
#include "yandex_md5.h"

/* RFC 1321, appendix A.5. If these pass, the signature the server checks is
 * being computed the way the server computes it. */
static void test_md5_matches_the_published_vectors(void)
{
    static const struct {
        const char *input;
        const char *digest;
    } VECTORS[] = {
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    };
    for (size_t index = 0U; index < sizeof(VECTORS) / sizeof(VECTORS[0]); ++index) {
        char hex[YANDEX_MD5_HEX_SIZE];
        yandex_md5_hex_of(hex, VECTORS[index].input, NULL);
        assert(strcmp(hex, VECTORS[index].digest) == 0);
    }
}

/* A path of a thousand characters is the normal case here, so the block
 * boundary the streaming update has to get right is not an edge case. */
static void test_md5_spans_many_blocks(void)
{
    char input[1001];
    memset(input, 'a', sizeof(input) - 1U);
    input[sizeof(input) - 1U] = '\0';

    char hex[YANDEX_MD5_HEX_SIZE];
    yandex_md5_hex_of(hex, input, NULL);
    assert(strcmp(hex, "cabe45dcc9ae5b66ba86600cca6b8ba8") == 0);

    /* Split across arguments, it must digest the same run of bytes. */
    char split[YANDEX_MD5_HEX_SIZE];
    char head[501];
    memset(head, 'a', sizeof(head) - 1U);
    head[sizeof(head) - 1U] = '\0';
    yandex_md5_hex_of(split, head, head, NULL);
    assert(strcmp(split, hex) == 0);
}

/* The real answer, shortened: two MP3 variants at different bitrates. */
static const char *const DOWNLOAD_INFO =
    "{\"invocationInfo\":{\"hostname\":\"music-web-mobile.yandex.net\"},\"result\":["
    "{\"codec\":\"mp3\",\"gain\":false,\"preview\":false,"
    "\"downloadInfoUrl\":\"https://api.music.yandex.net/get-mp3/low\","
    "\"direct\":false,\"bitrateInKbps\":192},"
    "{\"codec\":\"mp3\",\"gain\":false,\"preview\":false,"
    "\"downloadInfoUrl\":\"https://api.music.yandex.net/get-mp3/high\","
    "\"direct\":false,\"bitrateInKbps\":320}]}";

static void test_the_best_playable_mp3_wins(void)
{
    char url[YANDEX_LINK_URL_MAX + 1U];
    uint16_t bitrate = 0U;
    assert(yandex_link_pick_variant(DOWNLOAD_INFO, url, sizeof(url), &bitrate) ==
           YANDEX_LINK_OK);
    assert(strcmp(url, "https://api.music.yandex.net/get-mp3/high") == 0);
    assert(bitrate == 320U);
}

static void test_the_envelope_is_optional(void)
{
    char url[YANDEX_LINK_URL_MAX + 1U];
    assert(yandex_link_pick_variant(
               "[{\"codec\":\"mp3\",\"preview\":false,\"bitrateInKbps\":192,"
               "\"downloadInfoUrl\":\"https://host/x\"}]",
               url, sizeof(url), NULL) == YANDEX_LINK_OK);
    assert(strcmp(url, "https://host/x") == 0);
}

static void test_a_codec_we_do_not_decode_is_not_chosen(void)
{
    char url[YANDEX_LINK_URL_MAX + 1U];
    uint16_t bitrate = 0U;
    assert(yandex_link_pick_variant(
               "[{\"codec\":\"aac\",\"preview\":false,\"bitrateInKbps\":320,"
               "\"downloadInfoUrl\":\"https://host/aac\"},"
               "{\"codec\":\"mp3\",\"preview\":false,\"bitrateInKbps\":192,"
               "\"downloadInfoUrl\":\"https://host/mp3\"}]",
               url, sizeof(url), &bitrate) == YANDEX_LINK_OK);
    assert(strcmp(url, "https://host/mp3") == 0);
    assert(bitrate == 192U);
}

static void test_previews_only_is_reported_as_itself(void)
{
    char url[YANDEX_LINK_URL_MAX + 1U];
    assert(yandex_link_pick_variant(
               "[{\"codec\":\"mp3\",\"preview\":true,\"bitrateInKbps\":192,"
               "\"downloadInfoUrl\":\"https://host/x\"}]",
               url, sizeof(url), NULL) == YANDEX_LINK_ERR_PREVIEW_ONLY);
    assert(url[0] == '\0');
}

static void test_a_broken_answer_is_a_format_error(void)
{
    char url[YANDEX_LINK_URL_MAX + 1U];
    assert(yandex_link_pick_variant("[]", url, sizeof(url), NULL) == YANDEX_LINK_ERR_FORMAT);
    assert(yandex_link_pick_variant("{\"error\":\"not-found\"}", url, sizeof(url), NULL) ==
           YANDEX_LINK_ERR_FORMAT);
    assert(yandex_link_pick_variant("not json", url, sizeof(url), NULL) ==
           YANDEX_LINK_ERR_FORMAT);
    assert(yandex_link_pick_variant(NULL, url, sizeof(url), NULL) == YANDEX_LINK_ERR_FORMAT);
}

static void test_the_signed_link_matches_the_reference_signature(void)
{
    /* md5("XGRlBW9FXlekgbPrRHuSiA" + "abc123" + "deadbeef"), computed
     * independently - the salt and the order are what the server checks. */
    static const char *const XML =
        "<download-info><host>api.music.yandex.net</host><path>/abc123</path>"
        "<ts>1a02f27aee4</ts><region>-1</region><s>deadbeef</s></download-info>";
    char url[YANDEX_LINK_URL_MAX + 1U];
    assert(yandex_link_from_xml(XML, url, sizeof(url)) == YANDEX_LINK_OK);
    assert(strcmp(url,
                  "https://api.music.yandex.net/get-mp3/"
                  "7dab18af3b67678b8317370d9ebee45c/1a02f27aee4/abc123") == 0);
}

static void test_a_missing_element_or_a_pathless_path_is_refused(void)
{
    char url[YANDEX_LINK_URL_MAX + 1U];
    assert(yandex_link_from_xml("<download-info><host>h</host><path>/p</path>"
                                "<ts>1</ts></download-info>",
                                url, sizeof(url)) == YANDEX_LINK_ERR_FORMAT);
    /* Without the leading slash the signature would cover different bytes. */
    assert(yandex_link_from_xml("<download-info><host>h</host><path>p</path>"
                                "<ts>1</ts><s>x</s></download-info>",
                                url, sizeof(url)) == YANDEX_LINK_ERR_FORMAT);
    assert(yandex_link_from_xml("<download-info></download-info>", url, sizeof(url)) ==
           YANDEX_LINK_ERR_FORMAT);
    assert(yandex_link_from_xml(NULL, url, sizeof(url)) == YANDEX_LINK_ERR_FORMAT);
}

static void test_a_link_that_does_not_fit_says_so(void)
{
    char url[64];
    static const char *const XML =
        "<download-info><host>api.music.yandex.net</host>"
        "<path>/0123456789012345678901234567890123456789012345678901234567890123456789</path>"
        "<ts>1a02f27aee4</ts><s>deadbeef</s></download-info>";
    assert(yandex_link_from_xml(XML, url, sizeof(url)) == YANDEX_LINK_ERR_TOO_LONG);
    assert(url[0] == '\0');
}

int main(void)
{
    test_md5_matches_the_published_vectors();
    test_md5_spans_many_blocks();
    test_the_best_playable_mp3_wins();
    test_the_envelope_is_optional();
    test_a_codec_we_do_not_decode_is_not_chosen();
    test_previews_only_is_reported_as_itself();
    test_a_broken_answer_is_a_format_error();
    test_the_signed_link_matches_the_reference_signature();
    test_a_missing_element_or_a_pathless_path_is_refused();
    test_a_link_that_does_not_fit_says_so();
    printf("yandex_link tests passed\n");
    return 0;
}
