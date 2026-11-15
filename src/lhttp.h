/* A small HTTP client for the programs that run beside the app on this console and answer on 127.0.0.1 (Kraken Mount on 8097,
   Dump Forge on 8095): one request, "Connection: close", the whole answer read. Nothing here goes beyond the loopback. */
#ifndef PS5TM_LHTTP_H
#define PS5TM_LHTTP_H

#include <stddef.h>

/* The program is there (the connection was made) but did not answer within the time: it is probably busy, not gone. */
#define LHTTP_NO_ANSWER (-2)
/* The answer is longer than the limit (2 MiB) and was cut off: not used (a half JSON would only be read as nothing). */
#define LHTTP_TOO_BIG   (-3)

/* One request to 127.0.0.1:port. Returns the HTTP status; -1 when nothing listens or the request could not be sent; LHTTP_NO_ANSWER
   when it listens but did not answer in time (timeout_ms is the whole time of the answer); LHTTP_TOO_BIG for a cut-off answer. Every
   negative value means "no usable answer". *out (when out is not NULL) gets the body, malloc'd and always followed by a 0 byte (an
   image is no string, then); *out_len its length. content_type may be NULL. */
int ps5tm_lhttp(int port, const char *method, const char *path, const char *content_type, const char *body, size_t body_len,
                int timeout_ms, unsigned char **out, size_t *out_len);

#endif
