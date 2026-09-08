#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_timezone.h"

static void test_every_zone_is_usable_as_written(void)
{
    assert(device_timezone_count() > 0U);
    for (size_t index = 0U; index < device_timezone_count(); ++index) {
        const device_timezone_t *const zone = device_timezone_at(index);
        assert(zone != NULL);
        assert(zone->id != NULL && zone->label != NULL && zone->posix != NULL);
        assert(zone->id[0] != '\0' && zone->label[0] != '\0' && zone->posix[0] != '\0');
        assert(strlen(zone->id) < DEVICE_TIMEZONE_ID_MAX);
        assert(strlen(zone->label) < DEVICE_TIMEZONE_LABEL_MAX);
        /* An id is what settings.csv stores and what travels in a URL-shaped
         * JSON field, so it may hold neither the file's separator nor a tab
         * nor a space. The POSIX rule beside it is free to hold commas -
         * that is exactly why it never reaches the file. */
        assert(strchr(zone->id, ',') == NULL);
        assert(strchr(zone->id, '\t') == NULL);
        assert(strchr(zone->id, ' ') == NULL);
        for (const char *cursor = zone->id; *cursor != '\0'; ++cursor) {
            const unsigned char character = (unsigned char)*cursor;
            assert(character > ' ' && character < 0x7FU);
            assert(character != '"' && character != '\\');
        }
        // Two zones sharing an id would make one of them unreachable.
        for (size_t other = 0U; other < index; ++other) {
            assert(strcmp(device_timezone_at(other)->id, zone->id) != 0);
        }
    }
    assert(device_timezone_at(device_timezone_count()) == NULL);
}

static void test_the_default_is_in_the_list(void)
{
    /* The one id the firmware falls back to. Out of the list, every fallback
     * would land on an unset TZ, which is UTC - a device three hours out with
     * nothing on screen to say why. */
    const device_timezone_t *const zone = device_timezone_find(DEVICE_TIMEZONE_DEFAULT_ID);
    assert(zone != NULL);
    assert(strcmp(zone->posix, "MSK-3") == 0);
}

static void test_lookup_by_id(void)
{
    const device_timezone_t *const zone = device_timezone_find("europe/berlin");
    assert(zone != NULL);
    // A rule that shifts twice a year, which is the shape the file cannot hold.
    assert(strchr(zone->posix, ',') != NULL);
    assert(device_timezone_index_of("europe/berlin") < device_timezone_count());
    assert(device_timezone_at(device_timezone_index_of("europe/berlin")) == zone);

    assert(device_timezone_find("mars/olympus") == NULL);
    assert(device_timezone_find("") == NULL);
    assert(device_timezone_find(NULL) == NULL);
    // Past the end, which is what the web view carries for an unknown zone.
    assert(device_timezone_index_of("mars/olympus") == device_timezone_count());
    assert(device_timezone_index_of(NULL) == device_timezone_count());
}

int main(void)
{
    test_every_zone_is_usable_as_written();
    test_the_default_is_in_the_list();
    test_lookup_by_id();
    puts("device_timezone tests passed");
    return 0;
}
