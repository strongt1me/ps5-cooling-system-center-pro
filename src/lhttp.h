/* A small HTTP client for the programs that run beside the app on this console and answer on 127.0.0.1 (Kraken Mount on 8097,
   Dump Forge on 8095): one request, "Connection: close", the whole answer read. Nothing here goes beyond the loopback. */
#ifndef PS5TM_LHTTP_H
#define PS5TM_LHTTP_H

#include <stddef.h>

/* One request to 127.0.0.1:port. Returns the HTTP status, or -1 when nothing answers. *out (when out is not NULL) gets the body,
   malloc'd and always followed by a 0 byte (an image is no string, then); *out_len its length. content_type may be NULL. */
int ps5tm_lhttp(int port, const char *method, const char *path, const char *content_type, const char *body, size_t body_len,
                int timeout_ms, unsigned char **out, size_t *out_len);

#endif
