#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Bounded JSON writer shared by the WebSocket serializer and the REST
 * handlers. It was private to web_socket.c until the USB listing needed the
 * same UTF-8-aware string escaping: duplicating eighty lines of escaping is
 * how two encoders quietly drift apart, and a name with a quote in it is
 * enough to produce a broken document.
 *
 * Every writer is bounded twice over: by the buffer it was given, and by
 * `limit`. The socket path caps output at the frame size regardless of the
 * buffer handed in, while a REST handler streaming entry by entry wants the
 * whole buffer. Once a write would overflow either bound the writer latches
 * invalid and drops everything after, so a truncated document is never sent
 * as if it were whole - callers check `web_json_valid()`. */
typedef struct {
    char *output;
    size_t capacity;
    size_t limit;
    size_t length;
    bool valid;
} web_json_writer_t;

void web_json_init(web_json_writer_t *writer, char *output, size_t output_size,
                   size_t limit);
bool web_json_valid(const web_json_writer_t *writer);
size_t web_json_length(const web_json_writer_t *writer);

/* Marks the document unusable when the caller - not the writer - discovers the
 * data is wrong. Latching this rather than returning early keeps every failure
 * on one path, so a partial document cannot escape. */
void web_json_invalidate(web_json_writer_t *writer);

/* Empties the buffer so a rejected document cannot be mistaken for a short
 * one. */
void web_json_truncate(web_json_writer_t *writer);

void web_json_bytes(web_json_writer_t *writer, const char *bytes, size_t length);
void web_json_literal(web_json_writer_t *writer, const char *literal);
void web_json_format(web_json_writer_t *writer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/* Writes `text` as a quoted JSON string. Invalid UTF-8 is replaced rather than
 * passed through, because the names come from a FAT volume and nothing
 * guarantees they are well-formed. `content_budget` bounds the encoded content
 * only, so a caller can cap one field without capping the document. */
void web_json_string(web_json_writer_t *writer, const char *text);
void web_json_string_bounded(web_json_writer_t *writer, const char *text,
                             size_t content_budget);
