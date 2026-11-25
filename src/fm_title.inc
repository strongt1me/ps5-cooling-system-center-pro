/* The games of entries named after a title ID (25.11.2026, wish of the user): "PPSA01325" alone says nothing, so the folder list and the
 * search name the game ("title"). For a folder its own sce_sys/param.json (PS5) or sce_sys/param.sfo (PS4) comes first (a dump), then the
 * system's records of installed and registered titles (gamestate.c). Images (.exfat, .ffpfsc) are not mounted for it: they get a title only
 * when the system knows the game. On NTFS sticks and for links only the system's records are asked (nothing below a link is read).
 * Answers are kept (found for 10 minutes, not found for 1 minute), so a big folder costs one read per game and not one per visit.
 * Included by filemgr.c before the folder list. */

#include "titleid.h"

#define FMT_CACHE   128
#define FMT_TITLE   128

static struct {
  char     tid[10];
  char     title[FMT_TITLE];
  uint64_t at_ms;
  int      found;
  int      dir_tried;                    /* a folder's own sce_sys was read for it (a miss without it says nothing about a dump) */
} g_fmt[FMT_CACHE];
static unsigned        g_fmt_next;
static pthread_mutex_t g_fmt_lock = PTHREAD_MUTEX_INITIALIZER;

/* The title ID a name starts with: PPSA or CUSA and five digits, then the end or anything but a letter or digit ("PPSA01325",
   "PPSA01325.exfat", "CUSA05041-app"); 1 with it in tid, else 0. The app's own tiles are no game. */
static int
fmt_tid(const char *name, char tid[10]) {
  if(!name || strlen(name) < 9) return 0;
  char t[10];
  memcpy(t, name, 9);
  t[9] = 0;
  if(!ps5tm_counts_as_game(t)) return 0;
  const unsigned char c = (unsigned char)name[9];
  if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return 0;
  memcpy(tid, t, 10);
  return 1;
}

/* The game of title id tid; real_dir is the real path of a folder of that name on the console (NULL for files, links and NTFS).
   0 with the title in out (cleaned for JSON), -1 when none is known. */
static int
fmt_title(const char *real_dir, const char *tid, char *out, size_t n) {
  if(!out || n == 0) return -1;
  out[0] = 0;
  const uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_fmt_lock);
  for(unsigned i = 0; i < FMT_CACHE; i++) {
    if(strcmp(g_fmt[i].tid, tid)) continue;
    const int fresh = now - g_fmt[i].at_ms < (g_fmt[i].found ? 600000u : 60000u);
    if(fresh && (g_fmt[i].found || !real_dir || g_fmt[i].dir_tried)) {
      int rc = g_fmt[i].found ? 0 : -1;
      if(g_fmt[i].found) snprintf(out, n, "%s", g_fmt[i].title);
      pthread_mutex_unlock(&g_fmt_lock);
      return rc;
    }
    break;
  }
  pthread_mutex_unlock(&g_fmt_lock);

  char name[FMT_TITLE] = "";
  int rc = -1;
  if(real_dir) {
    char p[FM_PATH + FM_NAME + 64];
    snprintf(p, sizeof(p), "%s/sce_sys/param.json", real_dir);
    rc = ps5tm_title_from_param(p, name, sizeof(name));
    if(rc != 0) {
      snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", real_dir);
      rc = ps5tm_title_from_param(p, name, sizeof(name));
    }
  }
  if(rc != 0) rc = ps5tm_title_installed(tid, name, sizeof(name));
  char clean[FMT_TITLE + 8] = "";
  if(rc == 0) name_for_json(clean, sizeof(clean), name);
  if(!clean[0]) rc = -1;

  pthread_mutex_lock(&g_fmt_lock);
  unsigned slot = FMT_CACHE;
  for(unsigned i = 0; i < FMT_CACHE; i++)
    if(!strcmp(g_fmt[i].tid, tid)) { slot = i; break; }
  if(slot == FMT_CACHE) slot = g_fmt_next++ % FMT_CACHE;
  snprintf(g_fmt[slot].tid, sizeof(g_fmt[slot].tid), "%s", tid);
  snprintf(g_fmt[slot].title, sizeof(g_fmt[slot].title), "%s", rc == 0 ? clean : "");
  g_fmt[slot].at_ms = now;
  g_fmt[slot].found = rc == 0;
  g_fmt[slot].dir_tried = real_dir != NULL;
  pthread_mutex_unlock(&g_fmt_lock);
  if(rc == 0) snprintf(out, n, "%s", clean);
  return rc;
}

/* "title" for an entry named name in a folder (real: the folder's real path on the console, NULL on NTFS); type 'd' for a folder. */
static void
fmt_add(cJSON *x, const char *real, const char *name, char type) {
  char tid[10], t[FMT_TITLE];
  if(!fmt_tid(name, tid)) return;
  char dir[FM_PATH + FM_NAME + 64];
  const char *rd = NULL;
  if(real && type == 'd' && snprintf(dir, sizeof(dir), "%s%s%s", real, strcmp(real, "/") ? "/" : "", name) < (int)sizeof(dir)) rd = dir;
  if(fmt_title(rd, tid, t, sizeof(t)) == 0) cJSON_AddStringToObject(x, "title", t);
}
