#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_link.h"

static void test_the_size_marker_is_replaced(void)
{
    char url[YANDEX_COVER_URL_MAX + 1U];
    assert(yandex_cover_url("avatars.yandex.net/get-music-content/14247687/f8a8.a.336-1/%%",
                            url, sizeof(url)));
    /* Plain HTTP, and the size the 96x96 tile wants rather than the largest
     * the service will serve. */
    assert(strcmp(url,
                  "http://avatars.yandex.net/get-music-content/14247687/f8a8.a.336-1/100x100") ==
           0);
}

static void test_a_marker_in_the_middle_still_works(void)
{
    char url[YANDEX_COVER_URL_MAX + 1U];
    assert(yandex_cover_url("host/a/%%/b", url, sizeof(url)));
    assert(strcmp(url, "http://host/a/100x100/b") == 0);
}

static void test_a_cover_without_a_marker_is_refused(void)
{
    char url[YANDEX_COVER_URL_MAX + 1U];
    /* Fetching it verbatim would ask for a path the service does not serve,
     * and a wrong address is worse than no picture. */
    assert(!yandex_cover_url("avatars.yandex.net/get-music-content/plain", url, sizeof(url)));
    assert(url[0] == '\0');
    assert(!yandex_cover_url("", url, sizeof(url)));
    assert(!yandex_cover_url(NULL, url, sizeof(url)));
}

static void test_a_cover_that_does_not_fit_is_refused(void)
{
    char url[32];
    assert(!yandex_cover_url("avatars.yandex.net/get-music-content/14247687/f8a8.a.336-1/%%",
                             url, sizeof(url)));
    assert(url[0] == '\0');
}

int main(void)
{
    test_the_size_marker_is_replaced();
    test_a_marker_in_the_middle_still_works();
    test_a_cover_without_a_marker_is_refused();
    test_a_cover_that_does_not_fit_is_refused();
    printf("yandex_cover tests passed\n");
    return 0;
}
