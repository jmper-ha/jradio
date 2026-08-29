#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "audio_tags.h"
#include "player_control_types.h"

/* The three lines the now-playing block reads, wherever it is drawn.
 *
 * The panel and the web page used to work them out separately and drifted
 * apart: the page showed a file's name where the panel showed the title and
 * performer out of its tags, and named a station by whatever the stream called
 * itself while the panel obeyed the playlist's flag. Neither was a bug in one
 * of them so much as two answers to one question, so the question is asked
 * here once and each face only decides how to draw the answer.
 *
 * Pure on purpose: no LVGL, no player_control lookups, nothing but the strings
 * handed in. The callers differ in what they can see - the panel knows which
 * station the user has just picked while the switch is still pending, the web
 * knows only what the snapshot says - and that stays their business.
 *
 * Sized for what the sources can hand over rather than for what a label is
 * likely to fit: a tag field is AUDIO_TAGS_TEXT_MAX_LEN and an ICY line is
 * PLAYER_TITLE_MAX_LEN, and cutting them here would cut both faces at once. */
typedef struct {
    /* What is being listened to, rather than what is playing right now: the
     * station for a stream, the album for a file. */
    char heading[AUDIO_TAGS_TEXT_MAX_LEN];
    char artist[PLAYER_TITLE_MAX_LEN];
    char title[PLAYER_TITLE_MAX_LEN];
} ui_now_playing_t;

/* Which name the station line carries.
 *
 * The playlist's third column is exactly this choice, per station: true means
 * the name kept in the list is the one to show, false means the stream's own
 * name - its icy-name header - is. Neither answer fits every entry: some
 * stations name themselves better than a hand-kept list does, and some
 * announce a whole network where the list names the programme.
 *
 * Only the name is chosen here. The performer and the track always come from
 * the stream; the list knows nothing about what is playing right now, and the
 * column used to decide that too - clearing the flag replaced the track line
 * with the station's name and lost the metadata entirely.
 *
 * An empty `stream_name` falls back to the list, so a station that sends no
 * icy-name leaves the line filled rather than blank. Never returns NULL. */
const char *ui_now_playing_station_name(bool name_from_list, const char *list_name,
                                        const char *stream_name);

/* Splits an ICY title into performer and track.
 *
 * Stations send one string, and the near-universal convention is
 * "Artist - Title". Showing it whole wastes the one wide line on the screen
 * and buries the track name in the middle of it. There is no way to be
 * certain, so the rule is conservative: split on the first separator with
 * spaces on both sides - an ASCII hyphen or an en dash, whichever comes first,
 * because European stations use the dash and a hyphenated word has no spaces
 * around it. Anything else goes to `title` untouched and leaves `artist`
 * empty: a wrong split reads worse than no split.
 *
 * `heading` is what the line above already says, and a title that only repeats
 * it is not a performer and a track - a station announcing itself as
 * "Jazz - Lounge" would otherwise be filed under a performer called Jazz.
 * Pass NULL when there is nothing above to compare with.
 *
 * The output is sanitised UTF-8: an invalid sequence becomes U+FFFD and the
 * copy stops on a whole character rather than halfway into one. ICY bytes come
 * off the wire in whatever the station felt like sending, and both faces have
 * to survive it - the page as valid JSON, the panel as a renderable string.
 *
 * Returns true when a split happened. */
bool ui_now_playing_split_title(const char *icy, const char *heading, char *artist,
                                size_t artist_size, char *title, size_t title_size);

/* A file, from its own tags with the browser's names behind them.
 *
 * `tags` is NULL for a file that carries none. Each line falls back on its
 * own: a file can name its album and not its performer, and half a set of tags
 * is still better than none. The album takes the place a station's name has,
 * and the directory stands in where the file named no album - a folder is what
 * the listener was looking at when they chose it. */
void ui_now_playing_for_file(const char *directory, const char *file_name,
                             const audio_tags_t *tags, ui_now_playing_t *out);

/* A station, from the list and from the stream. */
void ui_now_playing_for_station(bool name_from_list, const char *list_name,
                                const char *stream_name, const char *icy_title,
                                ui_now_playing_t *out);
