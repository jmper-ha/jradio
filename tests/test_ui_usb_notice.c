#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_usb_notice.h"

static void test_a_readable_drive_with_files_opens_without_a_notice(void)
{
    assert(ui_usb_notice(USB_BROWSER_MEDIA_READY, 8U) == NULL);
    assert(ui_usb_can_open(USB_BROWSER_MEDIA_READY, 8U));
    assert(ui_usb_can_open(USB_BROWSER_MEDIA_READY, 1U));
}

static void test_a_missing_drive_asks_for_one(void)
{
    const char *notice = ui_usb_notice(USB_BROWSER_MEDIA_ABSENT, 0U);
    assert(notice != NULL);
    assert(strcmp(notice, "Вставьте USB-флешку") == 0);
    assert(!ui_usb_can_open(USB_BROWSER_MEDIA_ABSENT, 0U));
}

static void test_an_unreadable_drive_is_not_told_to_be_inserted(void)
{
    /* The whole reason the media state is not a bool: the drive is already
     * there, so repeating "insert a drive" sends the user looking in the wrong
     * place. */
    const char *notice = ui_usb_notice(USB_BROWSER_MEDIA_UNREADABLE, 0U);
    assert(notice != NULL);
    assert(strstr(notice, "Вставьте") == NULL);
    assert(strstr(notice, "FAT32") != NULL);
    assert(!ui_usb_can_open(USB_BROWSER_MEDIA_UNREADABLE, 0U));
}

static void test_an_unreadable_drive_stays_unreadable_whatever_the_count(void)
{
    /* A stale count from a previous drive must not make an unreadable one look
     * usable. */
    const char *notice = ui_usb_notice(USB_BROWSER_MEDIA_UNREADABLE, 12U);
    assert(notice != NULL);
    assert(!ui_usb_can_open(USB_BROWSER_MEDIA_UNREADABLE, 12U));
}

static void test_an_empty_drive_says_so_rather_than_showing_a_blank_list(void)
{
    const char *notice = ui_usb_notice(USB_BROWSER_MEDIA_READY, 0U);
    assert(notice != NULL);
    assert(strcmp(notice, "На флешке нет файлов") == 0);
    /* Not an error, but an empty browser looks like one, so it is still not
     * worth opening. */
    assert(!ui_usb_can_open(USB_BROWSER_MEDIA_READY, 0U));
}

static void test_each_state_gets_its_own_wording(void)
{
    const char *absent = ui_usb_notice(USB_BROWSER_MEDIA_ABSENT, 0U);
    const char *unreadable = ui_usb_notice(USB_BROWSER_MEDIA_UNREADABLE, 0U);
    const char *empty = ui_usb_notice(USB_BROWSER_MEDIA_READY, 0U);

    assert(strcmp(absent, unreadable) != 0);
    assert(strcmp(absent, empty) != 0);
    assert(strcmp(unreadable, empty) != 0);
}

static void test_media_presence_is_not_the_same_question_as_openability(void)
{
    /* What the open browser polls. An empty directory is still browsable - the
     * ".." row leads out of it - so it must not be mistaken for a drive that
     * has been pulled, which leaves the rows with nothing to refill them. */
    assert(ui_usb_media_present(USB_BROWSER_MEDIA_READY));
    assert(ui_usb_media_present(USB_BROWSER_MEDIA_READY) &&
           !ui_usb_can_open(USB_BROWSER_MEDIA_READY, 0U));
    assert(!ui_usb_media_present(USB_BROWSER_MEDIA_ABSENT));
    assert(!ui_usb_media_present(USB_BROWSER_MEDIA_UNREADABLE));
}

int main(void)
{
    test_a_readable_drive_with_files_opens_without_a_notice();
    test_a_missing_drive_asks_for_one();
    test_an_unreadable_drive_is_not_told_to_be_inserted();
    test_an_unreadable_drive_stays_unreadable_whatever_the_count();
    test_an_empty_drive_says_so_rather_than_showing_a_blank_list();
    test_each_state_gets_its_own_wording();
    test_media_presence_is_not_the_same_question_as_openability();
    puts("ui_usb_notice tests passed");
    return 0;
}
