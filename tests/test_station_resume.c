#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "station_resume.h"

static void test_save_and_load_last_station_url(void)
{
    const char *path = "/tmp/jradio_last_station_test.txt";
    const char *expected_url = "https://jazzlounge.ice.infomaniak.ch/jazzlounge-high.mp3";
    char url[128] = {0};

    assert(station_resume_save_last_url(path, expected_url));
    assert(station_resume_load_last_url(path, url, sizeof(url)));
    assert(strcmp(url, expected_url) == 0);
    assert(remove(path) == 0);
}

static void test_preserve_other_settings(void)
{
    const char *path = "/tmp/jradio_settings_test.csv";
    const char *expected_url = "http://example.test/new";
    char url[128] = {0};
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("volume,80\nlast_station_url,http://example.test/old\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(station_resume_save_last_url(path, expected_url));
    assert(station_resume_load_last_url(path, url, sizeof(url)));
    assert(strcmp(url, expected_url) == 0);
    file = fopen(path, "r");
    assert(file != NULL);
    char content[256] = {0};
    assert(fread(content, 1, sizeof(content) - 1, file) > 0);
    assert(fclose(file) == 0);
    assert(strstr(content, "volume,80\n") != NULL);
    assert(strstr(content, "last_station_url,http://example.test/new\n") != NULL);
    assert(remove(path) == 0);
}

static void test_reject_url_with_newline(void)
{
    const char *path = "/tmp/jradio_last_station_test_invalid.txt";

    assert(!station_resume_save_last_url(path, "http://example.invalid/stream\nextra"));
    assert(remove(path) != 0);
}

int main(void)
{
    test_save_and_load_last_station_url();
    test_preserve_other_settings();
    test_reject_url_with_newline();
    puts("station_resume tests passed");
    return 0;
}
