#include "yandex_session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The requests of a rotor session, against the shapes the server answered
 * HTTP 200 to on 2026-10-13. */

static yandex_track_batch_t two_tracks(void)
{
    yandex_track_batch_t batch = {0};
    batch.count = 2U;
    snprintf(batch.tracks[0].id, sizeof(batch.tracks[0].id), "986");
    snprintf(batch.tracks[1].id, sizeof(batch.tracks[1].id), "28673050");
    return batch;
}

static void test_a_station_opens_a_session_as_its_only_seed(void)
{
    char body[128];
    assert(yandex_session_new_body("user:onyourwave", body, sizeof(body)) == strlen(body));
    assert(strcmp(body, "{\"seeds\":[\"user:onyourwave\"],\"includeTracksInResponse\":true}") ==
           0);
    assert(yandex_session_new_body("genre:rock", body, sizeof(body)) > 0U);
    assert(strstr(body, "[\"genre:rock\"]") != NULL);
}

static void test_the_next_batch_names_the_tracks_handed_out(void)
{
    char path[96];
    assert(yandex_session_tracks_path("QCUj-2ju8hq5hIlJ9yQ-rDu4", path, sizeof(path)) ==
           strlen(path));
    assert(strcmp(path, "/rotor/session/QCUj-2ju8hq5hIlJ9yQ-rDu4/tracks") == 0);

    char body[128];
    const yandex_track_batch_t batch = two_tracks();
    assert(yandex_session_tracks_body(&batch, body, sizeof(body)) == strlen(body));
    assert(strcmp(body, "{\"queue\":[\"986\",\"28673050\"]}") == 0);

    /* An empty queue is what the library sends when it has nothing to name. */
    const yandex_track_batch_t none = {0};
    (void)yandex_session_tracks_body(&none, body, sizeof(body));
    assert(strcmp(body, "{\"queue\":[]}") == 0);
}

static void test_what_is_not_an_identifier_goes_nowhere(void)
{
    char out[128];
    assert(yandex_session_new_body("user:on wave", out, sizeof(out)) == 0U);
    assert(out[0] == '\0');
    assert(yandex_session_new_body("", out, sizeof(out)) == 0U);
    assert(yandex_session_tracks_path("ab/../cd", out, sizeof(out)) == 0U);
    assert(yandex_session_tracks_path(NULL, out, sizeof(out)) == 0U);

    /* One bad track id costs that id, not the request. */
    yandex_track_batch_t batch = two_tracks();
    snprintf(batch.tracks[0].id, sizeof(batch.tracks[0].id), "9\",\"x");
    (void)yandex_session_tracks_body(&batch, out, sizeof(out));
    assert(strcmp(out, "{\"queue\":[\"28673050\"]}") == 0);
}

static void test_a_buffer_that_will_not_hold_it_yields_nothing(void)
{
    /* Never half a path: a session id cut short is somebody else's session. */
    char small[20];
    assert(yandex_session_tracks_path("QCUj-2ju8hq5hIlJ9yQ-rDu4", small, sizeof(small)) == 0U);
    assert(small[0] == '\0');
    assert(yandex_session_new_body("user:onyourwave", small, sizeof(small)) == 0U);
    assert(small[0] == '\0');
    const yandex_track_batch_t batch = two_tracks();
    assert(yandex_session_tracks_body(&batch, small, sizeof(small)) == 0U);
    assert(small[0] == '\0');
}

int main(void)
{
    test_a_station_opens_a_session_as_its_only_seed();
    test_the_next_batch_names_the_tracks_handed_out();
    test_what_is_not_an_identifier_goes_nowhere();
    test_a_buffer_that_will_not_hold_it_yields_nothing();
    printf("yandex_session tests passed\n");
    return 0;
}
