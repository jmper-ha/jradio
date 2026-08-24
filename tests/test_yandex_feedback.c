#include "yandex_feedback.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The four bodies and the two paths, against the contract measured on
 * 2026-08-24 against api.music.yandex.net. Every assertion here is a shape the
 * server accepted or refused for real, not a shape that looked reasonable. */

static yandex_feedback_event_t make(yandex_feedback_kind_t kind)
{
    yandex_feedback_event_t event = {0};
    event.kind = kind;
    snprintf(event.station, sizeof(event.station), "user:onyourwave");
    snprintf(event.from, sizeof(event.from), "user-onyourwave");
    snprintf(event.batch_id, sizeof(event.batch_id),
             "1787591264830687-16297747482082378090.p6O1");
    snprintf(event.track_id, sizeof(event.track_id), "6386262");
    event.timestamp = 1787591265U;
    return event;
}

static void test_each_event_takes_the_shape_the_api_accepts(void)
{
    char body[192];
    yandex_feedback_event_t event = make(YANDEX_FEEDBACK_RADIO_STARTED);
    assert(yandex_feedback_body(&event, body, sizeof(body)) == strlen(body));
    assert(strcmp(body, "{\"type\":\"radioStarted\",\"timestamp\":1787591265,"
                        "\"from\":\"user-onyourwave\"}") == 0);

    event = make(YANDEX_FEEDBACK_TRACK_STARTED);
    (void)yandex_feedback_body(&event, body, sizeof(body));
    assert(strcmp(body, "{\"type\":\"trackStarted\",\"timestamp\":1787591265,"
                        "\"trackId\":\"6386262\"}") == 0);

    /* totalPlayedSeconds is required for both of these - the API answers 400
     * without it, naming the double field it could not fill. */
    event = make(YANDEX_FEEDBACK_TRACK_FINISHED);
    event.played_ms = 352720U;
    (void)yandex_feedback_body(&event, body, sizeof(body));
    assert(strcmp(body, "{\"type\":\"trackFinished\",\"timestamp\":1787591265,"
                        "\"trackId\":\"6386262\",\"totalPlayedSeconds\":352}") == 0);

    event = make(YANDEX_FEEDBACK_SKIP);
    event.played_ms = 12400U;
    (void)yandex_feedback_body(&event, body, sizeof(body));
    assert(strcmp(body, "{\"type\":\"skip\",\"timestamp\":1787591265,"
                        "\"trackId\":\"6386262\",\"totalPlayedSeconds\":12}") == 0);
}

static void test_an_unset_clock_still_produces_a_body(void)
{
    /* The device has no RTC, so a station opened before SNTP answers has no
     * wall clock at all. Measured: the API accepts timestamp 0, so this is
     * worth sending rather than worth holding back. */
    char body[192];
    yandex_feedback_event_t event = make(YANDEX_FEEDBACK_TRACK_STARTED);
    event.timestamp = 0U;
    assert(yandex_feedback_body(&event, body, sizeof(body)) > 0U);
    assert(strstr(body, "\"timestamp\":0") != NULL);
}

static void test_the_optional_fields_are_dropped_not_faked(void)
{
    char body[192];
    /* radioStarted without `from` is accepted; an empty string in its place
     * would be a claim about where the listening started. */
    yandex_feedback_event_t event = make(YANDEX_FEEDBACK_RADIO_STARTED);
    event.from[0] = '\0';
    (void)yandex_feedback_body(&event, body, sizeof(body));
    assert(strcmp(body, "{\"type\":\"radioStarted\",\"timestamp\":1787591265}") == 0);

    char path[128];
    /* Same for the batch id, which the endpoint takes as a query parameter and
     * does not require. */
    event.batch_id[0] = '\0';
    assert(yandex_feedback_path(&event, path, sizeof(path)) == strlen(path));
    assert(strcmp(path, "/rotor/station/user:onyourwave/feedback") == 0);

    event = make(YANDEX_FEEDBACK_TRACK_STARTED);
    (void)yandex_feedback_path(&event, path, sizeof(path));
    assert(strcmp(path, "/rotor/station/user:onyourwave/feedback"
                        "?batch-id=1787591264830687-16297747482082378090.p6O1") == 0);
}

static void test_an_event_with_no_track_is_refused_here(void)
{
    /* Rather than a round trip: the API answers 400 for all three of these,
     * and a refusal costs a TLS handshake to learn. */
    char body[192];
    for (int kind = YANDEX_FEEDBACK_TRACK_STARTED; kind <= YANDEX_FEEDBACK_SKIP; ++kind) {
        yandex_feedback_event_t event = make((yandex_feedback_kind_t)kind);
        event.track_id[0] = '\0';
        assert(yandex_feedback_body(&event, body, sizeof(body)) == 0U);
        assert(body[0] == '\0');
    }
}

static void test_identifiers_that_are_not_identifiers_are_dropped(void)
{
    /* Everything here comes out of the rotor's own JSON, so anything with a
     * quote, a brace or a space in it means the answer was not what we think.
     * Filing nothing beats escaping our way into a request the server reads
     * differently from how it was meant. */
    char body[192];
    char path[128];

    yandex_feedback_event_t event = make(YANDEX_FEEDBACK_TRACK_STARTED);
    snprintf(event.track_id, sizeof(event.track_id), "63\",\"x\":\"1");
    assert(yandex_feedback_body(&event, body, sizeof(body)) == 0U);

    event = make(YANDEX_FEEDBACK_TRACK_STARTED);
    snprintf(event.station, sizeof(event.station), "user:on wave");
    assert(yandex_feedback_path(&event, path, sizeof(path)) == 0U);
    assert(path[0] == '\0');

    /* A station is the one part with nothing to fall back on, unlike the two
     * optional fields, which are simply left out. */
    event = make(YANDEX_FEEDBACK_TRACK_STARTED);
    snprintf(event.batch_id, sizeof(event.batch_id), "17875%%9126/../evil");
    (void)yandex_feedback_path(&event, path, sizeof(path));
    assert(strcmp(path, "/rotor/station/user:onyourwave/feedback") == 0);

    event = make(YANDEX_FEEDBACK_RADIO_STARTED);
    snprintf(event.from, sizeof(event.from), "user\\\\onyourwave");
    (void)yandex_feedback_body(&event, body, sizeof(body));
    assert(strcmp(body, "{\"type\":\"radioStarted\",\"timestamp\":1787591265}") == 0);
}

static void test_a_buffer_that_will_not_hold_it_yields_nothing(void)
{
    /* Never a half-written body: a truncated JSON document is refused by the
     * server, but a truncated path names a different station. */
    char small[24];
    yandex_feedback_event_t event = make(YANDEX_FEEDBACK_TRACK_FINISHED);
    event.played_ms = 1000U;
    assert(yandex_feedback_body(&event, small, sizeof(small)) == 0U);
    assert(small[0] == '\0');
    assert(yandex_feedback_path(&event, small, sizeof(small)) == 0U);
    assert(small[0] == '\0');

    assert(yandex_feedback_body(&event, NULL, sizeof(small)) == 0U);
    assert(yandex_feedback_path(NULL, small, sizeof(small)) == 0U);
}

int main(void)
{
    test_each_event_takes_the_shape_the_api_accepts();
    test_an_unset_clock_still_produces_a_body();
    test_the_optional_fields_are_dropped_not_faked();
    test_an_event_with_no_track_is_refused_here();
    test_identifiers_that_are_not_identifiers_are_dropped();
    test_a_buffer_that_will_not_hold_it_yields_nothing();
    puts("yandex_feedback tests passed");
    return 0;
}
