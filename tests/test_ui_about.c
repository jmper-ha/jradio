#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_about.h"

/* What this screen has to get right is not the layout - that is checked in
   test_ui_layout.c - but the three answers it can give about a version: here
   it is, the device does not know, and the two halves disagree. The last one
   is the reason the screen exists, and it is the one that must not fire when
   the web stamp is merely absent. */

static version_info_t both(const char *firmware, const char *web)
{
    version_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.firmware.version, sizeof(info.firmware.version), "%s", firmware);
    snprintf(info.firmware.built, sizeof(info.firmware.built), "Sep  6 2026");
    snprintf(info.idf, sizeof(info.idf), "v5.5.5");
    info.firmware.present = true;
    if (web != NULL) {
        snprintf(info.web.version, sizeof(info.web.version), "%s", web);
        info.web.present = true;
    }
    return info;
}

static void test_it_names_what_the_device_is_running(void)
{
    const version_info_t info = both("v1.2.0", "v1.2.0");
    ui_about_lines_t lines;
    ui_about_build(&info, false, &lines);
    assert(strcmp(lines.firmware, "Прошивка: v1.2.0") == 0);
    assert(strcmp(lines.built, "Собрана: Sep  6 2026") == 0);
    assert(strcmp(lines.web, "Веб-интерфейс: v1.2.0") == 0);
    assert(strcmp(lines.idf, "ESP-IDF: v5.5.5") == 0);
    /* Agreement is the quiet case: nothing to say. */
    assert(lines.notice[0] == '\0');

    ui_about_build(&info, true, &lines);
    assert(strcmp(lines.firmware, "Firmware: v1.2.0") == 0);
    assert(strcmp(lines.web, "Web UI: v1.2.0") == 0);
    /* The framework's label is a proper name and is not translated. */
    assert(strcmp(lines.idf, "ESP-IDF: v5.5.5") == 0);
}

static void test_a_mismatch_says_so(void)
{
    const version_info_t info = both("v1.3.0", "v1.2.0");
    ui_about_lines_t lines;
    ui_about_build(&info, false, &lines);
    assert(lines.notice[0] != '\0');
    ui_about_build(&info, true, &lines);
    assert(lines.notice[0] != '\0');
    /* And each half still names itself, so the reader can see which is which
       rather than only that something is wrong. */
    assert(strcmp(lines.firmware, "Firmware: v1.3.0") == 0);
    assert(strcmp(lines.web, "Web UI: v1.2.0") == 0);
}

static void test_a_missing_web_stamp_is_unknown_and_not_a_mismatch(void)
{
    /* An image flashed before the stamp existed. The web line has to admit
       ignorance, and the notice has to stay quiet: shouting "mismatch" at
       something that is merely old teaches the reader to ignore the line that
       matters. */
    const version_info_t info = both("v1.2.0", NULL);
    ui_about_lines_t lines;
    ui_about_build(&info, false, &lines);
    assert(strcmp(lines.web, "Веб-интерфейс: неизвестно") == 0);
    assert(lines.notice[0] == '\0');

    ui_about_build(&info, true, &lines);
    assert(strcmp(lines.web, "Web UI: unknown") == 0);
    assert(lines.notice[0] == '\0');
}

static void test_it_fills_every_line_whatever_it_is_given(void)
{
    /* A blank row reads as a broken screen, so nothing is ever left empty
       except the notice, which is empty on purpose. */
    ui_about_lines_t lines;
    version_info_t nothing;
    memset(&nothing, 0, sizeof(nothing));
    ui_about_build(&nothing, false, &lines);
    assert(strstr(lines.firmware, "неизвестно") != NULL);
    assert(strstr(lines.built, "неизвестно") != NULL);
    assert(strstr(lines.web, "неизвестно") != NULL);
    assert(strstr(lines.idf, "неизвестно") != NULL);
    assert(lines.notice[0] == '\0');

    /* No information at all is the same answer, not a crash. */
    ui_about_build(NULL, false, &lines);
    assert(strstr(lines.firmware, "неизвестно") != NULL);
    ui_about_build(NULL, true, &lines);
    assert(strstr(lines.firmware, "unknown") != NULL);
    /* And a null destination is simply nothing to do. */
    ui_about_build(&nothing, false, NULL);
}

static void test_the_longest_version_still_fits_its_line(void)
{
    /* `git describe` at full stretch, in the field that holds it. The line
       has to survive it without the label being cut off, or the screen would
       show a version with no name beside it. */
    version_info_t info;
    memset(&info, 0, sizeof(info));
    for (size_t index = 0U; index < VERSION_INFO_STRING_MAX; ++index) {
        info.firmware.version[index] = 'v';
        info.web.version[index] = 'w';
    }
    info.firmware.present = true;
    info.web.present = true;

    ui_about_lines_t lines;
    ui_about_build(&info, false, &lines);
    /* Not truncated to nothing, and the label survived. */
    assert(strncmp(lines.web, "Веб-интерфейс: ", strlen("Веб-интерфейс: ")) == 0);
    assert(strlen(lines.web) > strlen("Веб-интерфейс: "));
    assert(strlen(lines.web) <= UI_ABOUT_LINE_MAX);
    /* Two different versions, so the notice fires. */
    assert(lines.notice[0] != '\0');
}

int main(void)
{
    test_it_names_what_the_device_is_running();
    test_a_mismatch_says_so();
    test_a_missing_web_stamp_is_unknown_and_not_a_mismatch();
    test_it_fills_every_line_whatever_it_is_given();
    test_the_longest_version_still_fits_its_line();
    puts("ui_about tests passed");
    return 0;
}
