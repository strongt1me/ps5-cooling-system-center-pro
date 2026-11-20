/* Host test of src/paystore.c, the lists of the payload store (21.11.2026: itsPLK's list and strongt1me's, one page). The network is a stub that serves two
   lists and can be made to fail; the files go to a folder of the test. What is tried: both lists in one, who keeps an id or a file both name, what a list that
   cannot be reached leaves (its saved copy), the reason in the error, the saved lists after a restart, and when the page asks again by itself. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "paystore.c"                                   /* the unit itself: its statics are the state the test resets */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if(!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

/* ------------------------------------------------------------ stubs: the network, the clock, the log, the start of a payload */

#define H "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define GH "https://github.com/x/y/releases/download/v1/"

static const char LIST_A[] =
  "{\"name\":\"mirror\",\"icons_url\":\"x\",\"payloads\":["
  "{\"name\":\"elfldr\",\"filename\":\"elfldr_v0.28.elf\",\"url\":\"" GH "elfldr_v0.28.elf\",\"version\":\"v0.28\",\"category\":\"Loaders\","
    "\"description\":\"loader\",\"last_update\":\"2026-11-19\",\"source\":\"https://github.com/ps5-payload-dev/elfldr\",\"checksum\":\"" H "\"},"
  "{\"name\":\"Shared Tool\",\"filename\":\"shared.elf\",\"url\":\"" GH "shared.elf\",\"version\":\"1.0\",\"category\":\"Utilities & Tools\","
    "\"description\":\"tool\",\"last_update\":\"2026-11-18\",\"source\":\"https://github.com/someone/shared\",\"checksum\":\"" H "\"}"
  "]}";

static const char LIST_B[] =
  "{\"name\":\"strongt1me PS5 Payloads\",\"payloads\":["
  "{\"id\":\"ps5medic\",\"name\":\"PS5 Medic\",\"filename\":\"ps5medicui.elf\",\"url\":\"https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/payload-manager/ps5medicui.elf\","
    "\"version\":\"0.4.5\",\"last_update\":\"2026-11-20\",\"category\":\"Utilities & Tools\",\"checksum\":\"" H "\",\"author\":\"strongt1me\",\"description\":\"no source field\"},"
  "{\"id\":\"usb-formatierer\",\"name\":\"USB Formatter\",\"filename\":\"usb_format.elf\",\"url\":\"https://github.com/strongt1me/ps5-usb-formatierer/releases/download/v2.6.72/usb_format.elf\","
    "\"version\":\"v2.6.72\",\"last_update\":\"2026-11-20\",\"category\":\"Utilities & Tools\",\"checksum\":\"" H "\",\"author\":\"strongt1me\","
    "\"source\":\"https://github.com/strongt1me/ps5-usb-formatierer/releases\",\"details_url\":\"https://raw.githubusercontent.com/x/README.md\"},"
  "{\"id\":\"elfldr\",\"name\":\"Own elfldr\",\"filename\":\"own_elfldr.elf\",\"url\":\"" GH "own_elfldr.elf\",\"version\":\"9\",\"checksum\":\"" H "\"},"
  "{\"id\":\"other-id\",\"name\":\"Same file as the first list\",\"filename\":\"SHARED.ELF\",\"url\":\"" GH "SHARED.ELF\",\"version\":\"9\",\"checksum\":\"" H "\"},"
  "{\"id\":\"bad-sum\",\"name\":\"Bad sum\",\"filename\":\"bad.elf\",\"url\":\"" GH "bad.elf\",\"version\":\"1\",\"checksum\":\"12345\"},"
  "{\"id\":\"http\",\"name\":\"Plain http\",\"filename\":\"http.elf\",\"url\":\"http://github.com/x/http.elf\",\"version\":\"1\",\"checksum\":\"" H "\"},"
  "{\"id\":\"evil\",\"name\":\"Other host\",\"filename\":\"evil.elf\",\"url\":\"https://example.com/evil.elf\",\"version\":\"1\",\"checksum\":\"" H "\"}"
  "]}";

static int net_fail[2], net_calls, bad_json_b;
static uint64_t now_ms;

int
upd_http_fetch(const upd_req_t *req, size_t max, char **out, size_t *len, upd_res_t *res) {
  (void)max;
  memset(res, 0, sizeof *res);
  *out = NULL;
  *len = 0;
  int which = strstr(req->url, "itsplk") ? 0 : strstr(req->url, "strongt1me") ? 1 : -1;
  if(which < 0) { snprintf(res->step, sizeof res->step, "network"); return -1; }       /* a picture of a developer: not in this test */
  net_calls++;
  if(net_fail[which] == 1) { snprintf(res->step, sizeof res->step, "network"); return -1; }
  if(net_fail[which] == 404) { res->status = 404; snprintf(res->step, sizeof res->step, "http_status"); return -1; }
  const char *body = which == 0 ? LIST_A : bad_json_b ? "<html>not json</html>" : LIST_B;
  *out = strdup(body);
  *len = strlen(body);
  res->status = 200;
  return 0;
}

int
upd_http(const upd_req_t *req, upd_res_t *res) {
  (void)req;
  memset(res, 0, sizeof *res);
  return -1;
}

int
upd_url_split(const char *url, char *host, size_t hn, int *https, int *port, char *path, size_t pn) {
  *https = !strncmp(url, "https://", 8);
  if(!*https && strncmp(url, "http://", 7)) return -1;
  const char *h = url + (*https ? 8 : 7), *e = h;
  while(*e && *e != '/' && *e != ':') e++;
  if((size_t)(e - h) >= hn || e == h) return -1;
  memcpy(host, h, (size_t)(e - h));
  host[e - h] = 0;
  *port = *https ? 443 : 80;
  snprintf(path, pn, "%s", *e == '/' ? e : "/");
  return 0;
}

uint64_t ps5tm_now_ms(void) { return now_ms; }

/* pthread_create is routed through this in the app (fanguard.c); the test starts no thread (the install thread is not tried here) */
int
ps5tm_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
  (void)thread; (void)attr; (void)start; (void)arg;
  return -1;
}

static char last_warn[300];
void
ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  (void)code;
  if(strcmp(level, "WARN")) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(last_warn, sizeof last_warn, fmt, ap);
  va_end(ap);
}

int
ps5tm_payload_start(const char *source, const char *mount, const char *dir, const char *name, ps5tm_payload_result_t *r) {
  (void)source; (void)mount; (void)dir; (void)name;
  memset(r, 0, sizeof *r);
  return 200;
}

/* ------------------------------------------------------------ helpers */

/* As after a restart of the app: nothing in memory, the saved files still there. */
static void
restart(void) {
  for(int s = 0; s < PS_NSRC; s++) { free(g_sv[s].items); memset(&g_sv[s], 0, sizeof g_sv[s]); }
  free(g_items);
  g_items = NULL;
  g_n = -1;
  g_cat_at = 0;
  g_cat_err[0] = 0;
  g_backoff_s = g_next_try_s = 0;
  g_av_running = 1;                                     /* no thread that looks for pictures: it would read what the test resets */
}

static const cJSON *
item(const cJSON *o, const char *id) {
  const cJSON *x;
  cJSON_ArrayForEach(x, cJSON_GetObjectItem(o, "items")) {
    const cJSON *i = cJSON_GetObjectItem(x, "id");
    if(cJSON_IsString(i) && !strcmp(i->valuestring, id)) return x;
  }
  return NULL;
}

static const char *
str(const cJSON *o, const char *k) {
  const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
  return cJSON_IsString(v) ? v->valuestring : "";
}

static int
count(const cJSON *o) {
  return cJSON_GetArraySize(cJSON_GetObjectItem(o, "items"));
}

static int
refresh_action(char *msg, size_t n) {
  cJSON *b = cJSON_Parse("{\"action\":\"refresh\"}");
  int st = ps5tm_paystore_action(b, msg, n);
  cJSON_Delete(b);
  return st;
}

int
main(int argc, char **argv) {
  static char dir[300];
  snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-paystore");
  char cmd[400];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
  if(system(cmd) != 0) return 2;
  mkdir(dir, 0777);
  now_ms = (uint64_t)time(NULL) * 1000;
  restart();
  char msg[300];

  /* ------------------------------------------------------------ both lists in one */
  cJSON *o = ps5tm_paystore_json(2);
  CHECK(net_calls == 2, "the button asks both lists (%d calls)", net_calls);
  CHECK(count(o) == 4, "elfldr, Shared Tool, PS5 Medic, USB Formatter: 4 items, not %d", count(o));
  CHECK(cJSON_IsTrue(cJSON_GetObjectItem(o, "loaded")), "loaded");
  CHECK(!cJSON_GetObjectItem(o, "error"), "no error: %s", str(o, "error"));
  CHECK(strstr(str(o, "source"), "itsPLK") && strstr(str(o, "source"), "strongt1me"), "both names in source: %s", str(o, "source"));
  const cJSON *srcs = cJSON_GetObjectItem(o, "sources");
  CHECK(cJSON_GetArraySize(srcs) == 2, "two sources");
  CHECK((int)cJSON_GetObjectItem(cJSON_GetArrayItem(srcs, 0), "count")->valuedouble == 2, "the first list has 2");
  CHECK((int)cJSON_GetObjectItem(cJSON_GetArrayItem(srcs, 1), "count")->valuedouble == 4, "the second list has 4 usable entries of its own (before the two are put together)");
  CHECK(strstr(str(item(o, "elfldr"), "list"), "itsPLK") != NULL, "elfldr is itsPLK's");
  CHECK(!strcmp(str(item(o, "elfldr"), "name"), "elfldr"), "the first list keeps the id elfldr, not 'Own elfldr'");
  CHECK(item(o, "other-id") == NULL, "a file the first list names too (SHARED.ELF against shared.elf) stays with the first list");
  CHECK(item(o, "bad-sum") == NULL && item(o, "http") == NULL && item(o, "evil") == NULL, "entries with a bad sum, plain http or another host are left out");
  const cJSON *med = item(o, "ps5medic");
  CHECK(med != NULL && strstr(str(med, "list"), "strongt1me") != NULL, "PS5 Medic comes from strongt1me's list");
  CHECK(!strcmp(str(med, "source"), "https://github.com/strongt1me") && !strcmp(str(med, "owner"), "strongt1me"),
        "no source field: the author stands for it (%s / %s)", str(med, "source"), str(med, "owner"));
  CHECK(!strcmp(str(item(o, "usb-formatierer"), "source"), "https://github.com/strongt1me/ps5-usb-formatierer/releases"), "a given source stays");
  CHECK(!strcmp(str(item(o, "ps5medic"), "filename"), "ps5medicui.elf"), "file name of a raw.githubusercontent.com payload");
  cJSON_Delete(o);

  /* ------------------------------------------------------------ the lists are saved, each in its file */
  struct stat st;
  CHECK(stat(g_srcs[0].file, &st) == 0 && stat(g_srcs[1].file, &st) == 0, "a file for each list");
  CHECK(strcmp(g_srcs[0].file, g_srcs[1].file) != 0, "not the same file");
  restart();
  net_fail[0] = net_fail[1] = 1;
  net_calls = 0;
  o = ps5tm_paystore_json(0);
  CHECK(net_calls == 0, "opening without a request goes nowhere");
  CHECK(count(o) == 4 && cJSON_IsTrue(cJSON_GetObjectItem(o, "loaded")), "after a restart the saved lists are there: %d", count(o));
  CHECK(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(o, "sources"), 1), "fetched_at")->valuedouble > 0, "the second list has its time from its file");
  cJSON_Delete(o);

  /* ------------------------------------------------------------ one list does not come: it keeps its copy, the reason names it */
  restart();
  net_fail[0] = 0; net_fail[1] = 404;
  int sc = refresh_action(msg, sizeof msg);
  CHECK(sc == 200, "some lists came: 200 (got %d: %s)", sc, msg);
  o = ps5tm_paystore_json(0);
  CHECK(count(o) == 4, "the second list stays with its saved copy: %d items", count(o));
  CHECK(strstr(str(o, "error"), "strongt1me") != NULL && strstr(str(o, "error"), "404") != NULL, "the error names the list and the status: %s", str(o, "error"));
  CHECK(strstr(str(o, "error"), "itsPLK") == NULL, "and not the list that came");
  CHECK(strstr(last_warn, "strongt1me") != NULL, "the log names the failed list: %s", last_warn);
  cJSON_Delete(o);

  restart();
  net_fail[0] = 1; net_fail[1] = 1;
  sc = refresh_action(msg, sizeof msg);
  CHECK(sc == 502, "no list came: 502 (got %d)", sc);
  CHECK(strstr(msg, "itsPLK") && strstr(msg, "strongt1me"), "both reasons in the message: %s", msg);
  o = ps5tm_paystore_json(0);
  CHECK(count(o) == 4, "nothing is lost: %d items from the saved copies", count(o));
  cJSON_Delete(o);

  restart();
  net_fail[0] = 0; net_fail[1] = 0; bad_json_b = 1;
  sc = refresh_action(msg, sizeof msg);
  o = ps5tm_paystore_json(0);
  CHECK(sc == 200 && count(o) == 4, "a list that is no JSON does not replace the saved one (%d, %d items)", sc, count(o));
  CHECK(strstr(str(o, "error"), "strongt1me") && strstr(str(o, "error"), "JSON"), "its error: %s", str(o, "error"));
  cJSON_Delete(o);
  bad_json_b = 0;

  /* ------------------------------------------------------------ a list that never came */
  restart();
  unlink(g_srcs[1].file);
  net_fail[0] = 0; net_fail[1] = 404;
  sc = refresh_action(msg, sizeof msg);
  o = ps5tm_paystore_json(0);
  CHECK(sc == 200 && count(o) == 2, "only the first list: 2 items (%d, %d)", sc, count(o));
  CHECK(item(o, "ps5medic") == NULL, "nothing of the second");
  CHECK(strstr(str(o, "error"), "strongt1me") != NULL, "the reason: %s", str(o, "error"));
  cJSON_Delete(o);

  /* ------------------------------------------------------------ when the page asks by itself */
  restart();
  net_fail[0] = 0; net_fail[1] = 0;
  cJSON_Delete(ps5tm_paystore_json(2));                 /* both fresh */
  net_calls = 0;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 0, "fresh lists: opening the page asks nothing (%d)", net_calls);
  now_ms += 4ull * 3600 * 1000;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 2, "after four hours the page asks both once (%d)", net_calls);
  net_calls = 0;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 0, "and not again at once");

  /* a list that stays away: tried again after 10 minutes, then after 20, never at every visit */
  restart();
  unlink(g_srcs[1].file);
  net_fail[1] = 404;
  net_calls = 0;
  cJSON_Delete(ps5tm_paystore_json(2));                 /* the button: a first try (second list fails) */
  CHECK(net_calls == 2, "the button asks both (%d)", net_calls);
  net_calls = 0;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 0, "a visit right after a miss does not ask (%d)", net_calls);
  now_ms += 11 * 60 * 1000;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 2, "after 10 minutes it is tried again (%d)", net_calls);
  net_calls = 0;
  now_ms += 11 * 60 * 1000;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 0, "the second wait is 20 minutes (%d)", net_calls);
  now_ms += 10 * 60 * 1000;
  net_fail[1] = 0;
  cJSON_Delete(ps5tm_paystore_json(1));
  CHECK(net_calls == 2, "after 21 minutes again, and now it comes (%d)", net_calls);
  o = ps5tm_paystore_json(0);
  CHECK(count(o) == 4 && !cJSON_GetObjectItem(o, "error"), "all 4 items, no error: %d / %s", count(o), str(o, "error"));
  cJSON_Delete(o);

  restart();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
