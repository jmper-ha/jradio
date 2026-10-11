#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "web_icon_fetch.h"

static void test_decode_takes_what_the_page_sends(void)
{
    char out[64];
    // encodeURIComponent() of an address with a query.
    assert(web_icon_fetch_decode("https%3A%2F%2Fa.ru%2Fi.png%3Fv%3D2", out, sizeof(out)));
    assert(strcmp(out, "https://a.ru/i.png?v=2") == 0);
    // Either case of hex, and a form's '+'.
    assert(web_icon_fetch_decode("%2f%2F+x", out, sizeof(out)));
    assert(strcmp(out, "// x") == 0);
    assert(web_icon_fetch_decode("", out, sizeof(out)) && out[0] == '\0');
}

static void test_decode_refuses_what_it_cannot_read(void)
{
    char out[8];
    assert(!web_icon_fetch_decode("%", out, sizeof(out)));
    assert(!web_icon_fetch_decode("%4", out, sizeof(out)));
    assert(!web_icon_fetch_decode("%zz", out, sizeof(out)));
    // Seven bytes and the terminator fit; eight do not.
    assert(web_icon_fetch_decode("1234567", out, sizeof(out)));
    assert(!web_icon_fetch_decode("12345678", out, sizeof(out)));
    assert(!web_icon_fetch_decode(NULL, out, sizeof(out)));
}

static void test_url_must_be_a_web_address(void)
{
    assert(web_icon_fetch_url_ok("https://radiojazzfm.ru/favicons/apple-touch-icon.png"));
    assert(web_icon_fetch_url_ok("http://79.111.119.111:8002/logo.png"));
    assert(!web_icon_fetch_url_ok("ftp://a.ru/x.png"));
    assert(!web_icon_fetch_url_ok("data:image/png;base64,AAAA"));
    assert(!web_icon_fetch_url_ok("file:///etc/passwd"));
    assert(!web_icon_fetch_url_ok("https://"));
    assert(!web_icon_fetch_url_ok("http:///x.png"));
    assert(!web_icon_fetch_url_ok("http://?x"));
    // A space or a line break would be a second request line in the making.
    assert(!web_icon_fetch_url_ok("http://a.ru/x y.png"));
    assert(!web_icon_fetch_url_ok("http://a.ru/x\r\nHost: b"));
    assert(!web_icon_fetch_url_ok("http://a.ru/\xd0\xb8.png"));
    assert(!web_icon_fetch_url_ok(NULL));

    char long_url[WEB_ICON_FETCH_URL_MAX + 1U];
    memcpy(long_url, "http://a.ru/", 12U);
    memset(long_url + 12, 'x', sizeof(long_url) - 13U);
    long_url[sizeof(long_url) - 1U] = '\0';
    assert(!web_icon_fetch_url_ok(long_url));
    long_url[WEB_ICON_FETCH_URL_MAX - 1U] = '\0';
    assert(web_icon_fetch_url_ok(long_url));
}

static void test_sniff_names_the_pictures(void)
{
    const uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0};
    const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xe0};
    const uint8_t gif[] = "GIF89a....";
    const uint8_t webp[] = "RIFF\x10\0\0\0WEBPVP8 ";
    const uint8_t ico[] = {0, 0, 1, 0, 2, 0, 16, 16};
    const char svg[] = "\xef\xbb\xbf<?xml version=\"1.0\"?>\n<!-- logo -->\n<svg xmlns=\"x\"/>";
    const char svg_bare[] = "  <svg viewBox=\"0 0 1 1\"></svg>";
    assert(strcmp(web_icon_fetch_sniff(png, sizeof(png)), "image/png") == 0);
    assert(strcmp(web_icon_fetch_sniff(jpeg, sizeof(jpeg)), "image/jpeg") == 0);
    assert(strcmp(web_icon_fetch_sniff(gif, sizeof(gif) - 1U), "image/gif") == 0);
    assert(strcmp(web_icon_fetch_sniff(webp, sizeof(webp) - 1U), "image/webp") == 0);
    assert(strcmp(web_icon_fetch_sniff(ico, sizeof(ico)), "image/x-icon") == 0);
    assert(strcmp(web_icon_fetch_sniff((const uint8_t *)svg, strlen(svg)), "image/svg+xml") == 0);
    assert(strcmp(web_icon_fetch_sniff((const uint8_t *)svg_bare, strlen(svg_bare)),
                  "image/svg+xml") == 0);
}

static void test_sniff_refuses_the_rest(void)
{
    // The usual wrong answer: the site's page where its icon was expected.
    const char html[] = "<!DOCTYPE html><html><head><title>x</title></head><body>"
                        "<svg></svg></body></html>";
    assert(web_icon_fetch_sniff((const uint8_t *)html, strlen(html)) == NULL);
    const char text[] = "Not Found";
    assert(web_icon_fetch_sniff((const uint8_t *)text, strlen(text)) == NULL);
    // Cut short: a PNG signature needs all eight bytes.
    const uint8_t short_png[] = {0x89, 'P', 'N', 'G'};
    assert(web_icon_fetch_sniff(short_png, sizeof(short_png)) == NULL);
    // An ICO header naming no images is not one.
    const uint8_t empty_ico[] = {0, 0, 1, 0, 0, 0};
    assert(web_icon_fetch_sniff(empty_ico, sizeof(empty_ico)) == NULL);
    assert(web_icon_fetch_sniff(NULL, 0U) == NULL);
    assert(web_icon_fetch_sniff((const uint8_t *)"", 0U) == NULL);
}

int main(void)
{
    test_decode_takes_what_the_page_sends();
    test_decode_refuses_what_it_cannot_read();
    test_url_must_be_a_web_address();
    test_sniff_names_the_pictures();
    test_sniff_refuses_the_rest();
    printf("web icon fetch tests passed\n");
    return 0;
}
