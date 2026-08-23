#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_track.h"

/* Shaped like the real answer of /rotor/station/user:onyourwave/tracks,
 * trimmed from a capture taken 2026-08-23. The nesting is the point: the
 * numeric ids of the artist, the album and the label sit in the same object
 * as the track's own string id, and the album carries a "title" of its own. */
static const char *const BATCH =
    "{\"invocationInfo\":{\"hostname\":\"music-web-mobile.yandex.net\"},"
    "\"result\":{\"id\":\"user:onyourwave\",\"batchId\":\"1787493923927354-179.KybK\","
    "\"sequence\":["
    "{\"type\":\"track\",\"track\":{"
    "\"id\":\"73189865\",\"realId\":\"73189865\",\"title\":\"It's in Her Heels\","
    "\"version\":\"feat. Haylen; Wolfgang Lohr Remix\","
    "\"major\":{\"id\":508,\"name\":\"WAGRAM_MUSIC\"},"
    "\"available\":true,\"durationMs\":180920,"
    "\"artists\":[{\"id\":784066,\"name\":\"Bart & Baker\",\"available\":true,"
    "\"cover\":{\"prefix\":\"4cfb1690.a\"},\"genres\":[]},"
    "{\"id\":900001,\"name\":\"Haylen\",\"available\":true,\"genres\":[]}],"
    "\"albums\":[{\"id\":12676746,\"title\":\"Et voil\\u00e0 !\",\"year\":2019,"
    "\"artists\":[{\"id\":784066,\"name\":\"Bart & Baker\"}],"
    "\"labels\":[{\"id\":1725897,\"name\":\"Pr'formance\"}],\"available\":true,"
    "\"coverUri\":\"avatars.yandex.net/album-cover-that-must-not-win/%%\"}],"
    "\"coverUri\":\"avatars.yandex.net/get-music-content/2433207/16fb.a.12-1/%%\","
    "\"type\":\"music\"},"
    "\"liked\":true,\"trackParameters\":{\"bpm\":130}},"
    "{\"type\":\"track\",\"track\":{"
    "\"id\":\"5072025\",\"title\":\"Green Onions\",\"available\":false,"
    "\"durationMs\":176330,\"artists\":[{\"id\":1,\"name\":\"Booker T.\"}]},"
    "\"liked\":false},"
    "{\"type\":\"track\",\"track\":{"
    "\"id\":\"38207393\",\"title\":\"\\u041f\\u0430\\u0440\\u0438\\u0436\","
    "\"available\":true,\"durationMs\":343910,"
    "\"artists\":[{\"id\":2,\"name\":\"Bellaire\"}]},"
    "\"liked\":false}"
    "],\"pumpkin\":false}}";

static void test_the_real_shape_yields_the_playable_tracks(void)
{
    yandex_track_batch_t batch;
    assert(yandex_tracks_parse_batch(BATCH, &batch));
    /* Three in the answer, one of them unavailable. */
    assert(batch.count == 2U);

    assert(strcmp(batch.tracks[0].id, "73189865") == 0);
    assert(strcmp(batch.tracks[0].title, "It's in Her Heels") == 0);
    assert(strcmp(batch.tracks[0].version, "feat. Haylen; Wolfgang Lohr Remix") == 0);
    assert(strcmp(batch.tracks[0].artist, "Bart & Baker, Haylen") == 0);
    assert(batch.tracks[0].duration_ms == 180920U);
    /* The track's own cover, not the album's - they sit in the same object and
     * the album carries one too. */
    assert(strcmp(batch.tracks[0].cover,
                  "avatars.yandex.net/get-music-content/2433207/16fb.a.12-1/%%") == 0);
    assert(batch.tracks[0].liked);

    /* The second playable track is the third element: the unavailable one in
     * between must not shift anything or leave a hole. */
    assert(strcmp(batch.tracks[1].id, "38207393") == 0);
    assert(strcmp(batch.tracks[1].title, "Париж") == 0);
    assert(strcmp(batch.tracks[1].artist, "Bellaire") == 0);
    assert(batch.tracks[1].version[0] == '\0');
    /* A track with no picture leaves the field empty rather than borrowing the
     * previous one's. */
    assert(batch.tracks[1].cover[0] == '\0');
    assert(!batch.tracks[1].liked);
}

static void test_the_envelope_is_optional(void)
{
    yandex_track_batch_t batch;
    assert(yandex_tracks_parse_batch(
        "{\"sequence\":[{\"track\":{\"id\":\"1\",\"title\":\"A\",\"available\":true,"
        "\"durationMs\":1000,\"artists\":[{\"name\":\"B\"}]}}]}",
        &batch));
    assert(batch.count == 1U);
    assert(strcmp(batch.tracks[0].artist, "B") == 0);
}

static void test_an_unreadable_track_does_not_cost_the_others(void)
{
    yandex_track_batch_t batch;
    /* The first track's id is longer than the field: refusing it is right, and
     * it must not take the readable one with it. */
    assert(yandex_tracks_parse_batch(
        "{\"sequence\":["
        "{\"track\":{\"id\":\"012345678901234567890123456789\",\"title\":\"X\","
        "\"available\":true,\"durationMs\":1,\"artists\":[]}},"
        "{\"track\":{\"id\":\"7\",\"title\":\"Y\",\"available\":true,"
        "\"durationMs\":2,\"artists\":[]}}]}",
        &batch));
    assert(batch.count == 1U);
    assert(strcmp(batch.tracks[0].id, "7") == 0);
}

static void test_a_long_title_is_clipped_on_a_character_boundary(void)
{
    char json[1024];
    char title[512];
    size_t length = 0U;
    /* "я" is two bytes, so a field of odd capacity can only be filled to an
     * even count - the proof that the cut is not made at the byte that fits. */
    for (size_t index = 0U; index < 100U; ++index) {
        title[length++] = (char)0xD1;
        title[length++] = (char)0x8F;
    }
    title[length] = '\0';
    snprintf(json, sizeof(json),
             "{\"sequence\":[{\"track\":{\"id\":\"9\",\"title\":\"%s\","
             "\"available\":true,\"durationMs\":1,\"artists\":[]}}]}",
             title);

    yandex_track_batch_t batch;
    assert(yandex_tracks_parse_batch(json, &batch));
    assert(batch.count == 1U);
    const size_t kept = strlen(batch.tracks[0].title);
    assert(kept > 0U && kept < YANDEX_TRACK_TITLE_MAX + 1U);
    assert(kept % 2U == 0U);
    for (size_t index = 0U; index < kept; index += 2U) {
        assert((unsigned char)batch.tracks[0].title[index] == 0xD1U);
        assert((unsigned char)batch.tracks[0].title[index + 1U] == 0x8FU);
    }
}

static void test_a_batch_with_nothing_playable_fails(void)
{
    yandex_track_batch_t batch;
    assert(!yandex_tracks_parse_batch(
        "{\"sequence\":[{\"track\":{\"id\":\"1\",\"title\":\"A\",\"available\":false,"
        "\"durationMs\":1,\"artists\":[]}}]}",
        &batch));
    assert(batch.count == 0U);
    assert(!yandex_tracks_parse_batch("{\"sequence\":[]}", &batch));
    assert(!yandex_tracks_parse_batch("{\"result\":{}}", &batch));
    assert(!yandex_tracks_parse_batch("not json", &batch));
    assert(!yandex_tracks_parse_batch(NULL, &batch));
}

static void test_more_tracks_than_fit_are_carried_not_refused(void)
{
    char json[4096];
    size_t length = (size_t)snprintf(json, sizeof(json), "{\"sequence\":[");
    for (unsigned int index = 0U; index < YANDEX_TRACK_BATCH_MAX + 3U; ++index) {
        length += (size_t)snprintf(json + length, sizeof(json) - length,
                                   "%s{\"track\":{\"id\":\"%u\",\"title\":\"T\","
                                   "\"available\":true,\"durationMs\":1,\"artists\":[]}}",
                                   index > 0U ? "," : "", index);
    }
    snprintf(json + length, sizeof(json) - length, "]}");

    yandex_track_batch_t batch;
    assert(yandex_tracks_parse_batch(json, &batch));
    assert(batch.count == YANDEX_TRACK_BATCH_MAX);
    assert(strcmp(batch.tracks[0].id, "0") == 0);
}

int main(void)
{
    test_the_real_shape_yields_the_playable_tracks();
    test_the_envelope_is_optional();
    test_an_unreadable_track_does_not_cost_the_others();
    test_a_long_title_is_clipped_on_a_character_boundary();
    test_a_batch_with_nothing_playable_fails();
    test_more_tracks_than_fit_are_carried_not_refused();
    printf("yandex_tracks tests passed\n");
    return 0;
}
