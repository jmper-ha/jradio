#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_likes.h"

static void test_the_two_endpoints_spell_the_parameter_differently(void)
{
    char path[96];
    /* Measured against the API on 2026-08-24: add takes track-id, remove takes
     * track-ids, and each refuses the other's spelling with HTTP 400. The
     * asymmetry is the contract, so a "tidied" builder would break one of the
     * two directions and only one. */
    assert(yandex_likes_path("531515355", "38903142", YANDEX_MARK_LIKE, true, path,
                             sizeof(path)) > 0U);
    assert(strcmp(path, "/users/531515355/likes/tracks/add?track-id=38903142") == 0);
    assert(yandex_likes_path("531515355", "38903142", YANDEX_MARK_LIKE, false, path,
                             sizeof(path)) > 0U);
    assert(strcmp(path, "/users/531515355/likes/tracks/remove?track-ids=38903142") == 0);
}

static void test_the_dislike_is_the_same_shape_of_url(void)
{
    char path[96];
    /* Measured 2026-08-26: the rejection lives in a second collection under
     * the same endpoints, and it repeats the singular/plural asymmetry exactly
     * - track-ids on add answers 400 there too. */
    assert(yandex_likes_path("531515355", "38903142", YANDEX_MARK_DISLIKE, true, path,
                             sizeof(path)) > 0U);
    assert(strcmp(path, "/users/531515355/dislikes/tracks/add?track-id=38903142") == 0);
    assert(yandex_likes_path("531515355", "38903142", YANDEX_MARK_DISLIKE, false, path,
                             sizeof(path)) > 0U);
    assert(strcmp(path, "/users/531515355/dislikes/tracks/remove?track-ids=38903142") == 0);

    /* A mark that is neither builds nothing: the collection name would be a
     * guess, and the request would ask the server to interpret it. */
    assert(yandex_likes_path("531515355", "38903142", (yandex_mark_t)7, true, path,
                             sizeof(path)) == 0U);
    assert(path[0] == '\0');
}

static void test_only_digits_are_ever_put_in_the_path(void)
{
    char path[96];
    /* Both ids come out of the API's own answers. Anything else means the
     * answer was not the one we take it for, and the request is not sent at
     * all rather than sent with an escaped guess in it. */
    assert(yandex_likes_path("531515355", "38903142; drop", YANDEX_MARK_LIKE, true, path,
                             sizeof(path)) == 0U);
    assert(path[0] == '\0');
    assert(yandex_likes_path("../admin", "38903142", YANDEX_MARK_LIKE, true, path, sizeof(path)) == 0U);
    assert(yandex_likes_path("531515355", "", YANDEX_MARK_LIKE, true, path, sizeof(path)) == 0U);
    assert(yandex_likes_path(NULL, "38903142", YANDEX_MARK_LIKE, true, path, sizeof(path)) == 0U);
    /* And a path that will not fit is refused, not clipped: a truncated id
     * names a different track. */
    char tiny[24];
    assert(yandex_likes_path("531515355", "38903142", YANDEX_MARK_LIKE, true, tiny,
                             sizeof(tiny)) == 0U);
    assert(tiny[0] == '\0');
}

static void test_the_account_id_is_read_out_of_the_status_answer(void)
{
    static const char answer[] =
        "{\"invocationInfo\":{\"req-id\":\"17875-92287\"},\"result\":{\"account\":"
        "{\"now\":\"2026-08-24T18:00:00+00:00\",\"uid\":531515355,\"login\":\"someone\"},"
        "\"subscription\":{\"hadAnySubscription\":true}}}";
    char uid[YANDEX_UID_MAX + 1U];
    assert(yandex_account_parse_uid(answer, uid, sizeof(uid)));
    assert(strcmp(uid, "531515355") == 0);

    /* Kept as text: accounts registered by phone carry a sixteen-digit id,
     * which does not fit the uint32 the rest of this reader deals in. */
    static const char long_id[] = "{\"account\":{\"uid\":1130000012345678}}";
    assert(yandex_account_parse_uid(long_id, uid, sizeof(uid)));
    assert(strcmp(uid, "1130000012345678") == 0);
}

static void test_an_answer_without_an_account_is_refused(void)
{
    char uid[YANDEX_UID_MAX + 1U];
    assert(!yandex_account_parse_uid("{\"result\":{\"subscription\":{}}}", uid, sizeof(uid)));
    assert(uid[0] == '\0');
    assert(!yandex_account_parse_uid("{\"account\":{\"uid\":\"531515355\"}}", uid, sizeof(uid)));
    assert(!yandex_account_parse_uid("not json", uid, sizeof(uid)));
    assert(!yandex_account_parse_uid(NULL, uid, sizeof(uid)));

    /* An id longer than the field is refused rather than clipped - half a uid
     * is another account. */
    char small[4];
    assert(!yandex_account_parse_uid("{\"account\":{\"uid\":531515355}}", small, sizeof(small)));
}

int main(void)
{
    test_the_two_endpoints_spell_the_parameter_differently();
    test_the_dislike_is_the_same_shape_of_url();
    test_only_digits_are_ever_put_in_the_path();
    test_the_account_id_is_read_out_of_the_status_answer();
    test_an_answer_without_an_account_is_refused();
    puts("yandex_likes tests passed");
    return 0;
}
