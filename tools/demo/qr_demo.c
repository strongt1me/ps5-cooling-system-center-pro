/* Writes the answer of GET /api/v1/qr for the invented address of the demo mode (tools/demo/fixtures/qr.json), with the QR encoder of the app.
 *   gcc -Isrc -o /tmp/qr_demo tools/demo/qr_demo.c src/qr.c && /tmp/qr_demo > tools/demo/fixtures/qr.json */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ps5tm_qr_svg(const char *text, char *out, size_t out_size);

int
main(void) {
  const char *url = "http://192.168.1.50:8086/";
  char svg[8192];
  if(ps5tm_qr_svg(url, svg, sizeof svg) != 0) return 1;
  printf("{\"ok\":true,\"url\":\"%s\",\"svg\":\"", url);
  for(const char *p = svg; *p; p++) {
    if(*p == '"') fputs("\\\"", stdout);
    else if(*p == '\\') fputs("\\\\", stdout);
    else putchar(*p);
  }
  printf("\"}\n");
  return 0;
}
