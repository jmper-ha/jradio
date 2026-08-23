#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A structure-aware JSON reader that allocates nothing and never rewinds.
 *
 * It exists because cJSON is the wrong tool for these answers twice over.
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024 sends every allocation under 1 KB
 * to internal RAM, and a cJSON tree over the 12-17 KB documents the rotor
 * returns is thousands of such nodes - the scarcest memory on the board spent
 * on a document read once. And a flat search for the wanted keys cannot work
 * anyway: one batch of five tracks carries 35 "id" fields, 24 "name" and 10
 * "title", nearly all of them belonging to nested albums, artists and labels.
 *
 * So the walk is explicit. Every reader here consumes exactly one grammatical
 * unit and leaves the cursor after it, which is what lets a caller descend
 * into the object it wants and skip the rest by structure rather than by name.
 *
 * A `json_reader_t` is a value: copy it to look ahead without committing, the
 * way parsing an object's second key after reading its first requires. */

typedef struct {
    const char *cursor;
} json_reader_t;

void json_skip_whitespace(json_reader_t *reader);

/* Consumes `expected` if it is the next non-space character. */
bool json_accept(json_reader_t *reader, char expected);

/* Reads a string, decoding escapes and \uXXXX into UTF-8. `output` may be NULL
 * to skip one without copying, which is what every key and every uninteresting
 * value does.
 *
 * A value too long for `capacity` is refused, not truncated: a clipped track
 * id names a track that does not exist, and that failure would surface much
 * later and much less clearly than here. */
bool json_read_string(json_reader_t *reader, char *output, size_t capacity);

/* The same, for text that is only ever displayed: a title too long for the
 * field is clipped instead of costing the whole track. The cut lands on a
 * character boundary, never inside a UTF-8 sequence. */
bool json_read_string_clipped(json_reader_t *reader, char *output, size_t capacity);

/* Reads a non-negative integer. Anything else - a fraction, a sign, a value
 * past UINT32_MAX - is refused rather than rounded or wrapped. */
bool json_read_uint(json_reader_t *reader, uint32_t *value);

/* Reads a literal `true` or `false`. */
bool json_read_bool(json_reader_t *reader, bool *value);

/* Consumes one value of any type. */
bool json_skip_value(json_reader_t *reader);

/* Positions the reader on the value of `key` inside the object that starts
 * here, skipping every other member. False when the object has no such key. */
bool json_enter_object_key(json_reader_t *reader, const char *key);
