/* See lhttp.h. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lhttp.h"

#define LHTTP_REPLY_MAX (2u * 1024 * 1024)

int
ps5tm_lhttp(int port, const char *method, const char *path, const char *content_type, const char *body, size_t body_len, int timeout_ms,
            unsigned char **out, size_t *out_len) {
  if(out) *out = NULL;
  if(out_len) *out_len = 0;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_port = htons((unsigned short)port);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); return -1; }
  char head[640];
  int hn = snprintf(head, sizeof head, "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n%s%s%sContent-Length: %zu\r\n\r\n", method, path,
                    content_type ? "Content-Type: " : "", content_type ? content_type : "", content_type ? "\r\n" : "", body_len);
  if(hn <= 0 || (size_t)hn >= sizeof head || send(fd, head, (size_t)hn, 0) != hn || (body_len && send(fd, body, body_len, 0) != (ssize_t)body_len)) {
    close(fd);
    return -1;
  }
  size_t cap = 8192, len = 0;
  unsigned char *buf = malloc(cap);
  if(!buf) { close(fd); return -1; }
  for(;;) {
    if(len + 4096 + 1 > cap) {
      if(cap >= LHTTP_REPLY_MAX) break;
      unsigned char *nb = realloc(buf, cap * 2);
      if(!nb) break;
      buf = nb;
      cap *= 2;
    }
    ssize_t r = recv(fd, buf + len, cap - len - 1, 0);
    if(r <= 0) break;
    len += (size_t)r;
  }
  close(fd);
  buf[len] = 0;
  int status = -1;
  if(len > 12 && !strncmp((char *)buf, "HTTP/1.", 7)) status = atoi((char *)buf + 9);
  if(status <= 0) { free(buf); return -1; }
  if(out) {
    unsigned char *p = NULL;
    for(size_t i = 0; i + 3 < len; i++)
      if(buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') { p = buf + i + 4; break; }
    size_t blen = p ? len - (size_t)(p - buf) : 0;
    unsigned char *copy = malloc(blen + 1);
    if(copy) {
      if(blen) memcpy(copy, p, blen);
      copy[blen] = 0;
      *out = copy;
      if(out_len) *out_len = blen;
    }
  }
  free(buf);
  return status;
}
