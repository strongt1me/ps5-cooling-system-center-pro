/* See lhttp.h. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
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
  /* connect with a time limit (a program that no longer accepts, full backlog, must not hold the caller for the TCP timeout): at most 5 s,
     or less when the caller's time is shorter. */
  {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if(rc != 0) {
      if(errno != EINPROGRESS) { close(fd); return -1; }
      struct pollfd pf = { fd, POLLOUT, 0 };
      int cms = timeout_ms < 5000 ? timeout_ms : 5000;
      int soerr = 0;
      socklen_t sl = sizeof soerr;
      if(poll(&pf, 1, cms > 0 ? cms : 1) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) { close(fd); return -1; }
    }
    fcntl(fd, F_SETFL, fl);
  }
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
  /* timeout_ms is the whole time of the answer, not the wait for each piece: a program that sends a byte now and then must not hold
     the caller for ever. */
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  int timed_out = 0, truncated = 0;
  for(;;) {
    if(len + 4096 + 1 > cap) {
      if(cap >= LHTTP_REPLY_MAX) { truncated = 1; break; }
      unsigned char *nb = realloc(buf, cap * 2);
      if(!nb) break;
      buf = nb;
      cap *= 2;
    }
    ssize_t r = recv(fd, buf + len, cap - len - 1, 0);
    if(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { timed_out = 1; break; }
    if(r <= 0) break;
    len += (size_t)r;
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 > (long)timeout_ms + 1000) { timed_out = 1; break; }
  }
  close(fd);
  buf[len] = 0;
  int status = -1;
  if(len > 12 && !strncmp((char *)buf, "HTTP/1.", 7)) status = atoi((char *)buf + 9);
  /* An answer cut off at the limit is no answer (a half JSON reads as nothing and must not pass for "the program answered"). Connected,
     but not a word in time: LHTTP_NO_ANSWER. -1 stays for "nothing listens". A valid status line is an answer, as before, even when the
     program kept the connection open after it (then the wait ran into the time). */
  if(truncated) { free(buf); return LHTTP_TOO_BIG; }
  if(status <= 0) { free(buf); return (timed_out || len == 0) ? LHTTP_NO_ANSWER : -1; }
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
