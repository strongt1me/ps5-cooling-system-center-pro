/* The pictures behind the areas of the full-screen app (29.10.2026).
 *
 * The full-screen tile ships one picture per area (tvapp/assets/backdrops). The
 * user can replace each with one of their own: the browser page scales the file
 * to 1280 × 720, encodes it as JPEG and sends it here; the tile asks for the list
 * every few seconds and fetches a picture again when its stamp changed. The tile
 * itself has no /data (its sandbox), so the files live with the app:
 *
 *   /data/PS5-Cooling-Center/backdrops/<area>.jpg   the user's picture
 *   /data/PS5-Cooling-Center/backdrops/modes.txt    "<area>=<mode>" per line
 *
 * A mode is "standard" (the tile's own picture), "custom" (the user's) or "off"
 * (no picture, the plain gradient). An area without a line is "standard"; one
 * set to "custom" whose file is gone shows the standard picture again.
 *
 * Only the seven area names below make a path, so nothing the browser sends is
 * ever put into one. One lock guards the modes and the files; it is held for a
 * rename or a small write, never across the upload itself (api.c writes the body
 * to <area>.jpg.part first and hands it over here). */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "third_party/cJSON.h"
#include "ps5tm.h"

#define BD_DIR   PS5TM_DATA_DIR "/backdrops"
#define BD_MODES BD_DIR "/modes.txt"
#define BD_AREAS 7

/* The sidebar's areas, in its order (tvapp/src/ps5cc/backdrops.cpp has the same list). */
static const char *const k_areas[BD_AREAS] = {
  "kuehlung", "spiele", "dateien", "payloads", "system", "profil", "protokoll",
};
static const char *const k_labels[BD_AREAS] = {
  "Kühlung", "Spiele", "Dateien", "Payloads", "System", "Profil", "Protokoll",
};
static const char *const k_modes[] = { "standard", "custom", "off" };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

int
ps5tm_backdrops_area(const char *name) {
  if(!name) return -1;
  for(int i = 0; i < BD_AREAS; i++)
    if(!strcmp(name, k_areas[i])) return i;
  return -1;
}

static int
mode_index(const char *mode) {
  if(!mode) return -1;
  for(int i = 0; i < 3; i++)
    if(!strcmp(mode, k_modes[i])) return i;
  return -1;
}

int
ps5tm_backdrops_path(int area, char *out, size_t n) {
  if(area < 0 || area >= BD_AREAS) return -1;
  int len = snprintf(out, n, "%s/%s.jpg", BD_DIR, k_areas[area]);
  return len > 0 && (size_t)len < n ? 0 : -1;
}

/* modes.txt into modes[]; an area without a line keeps 0 (standard). Lock held. */
static void
read_modes(int modes[BD_AREAS]) {
  for(int i = 0; i < BD_AREAS; i++) modes[i] = 0;
  FILE *f = fopen(BD_MODES, "r");
  if(!f) return;
  char line[96];
  while(fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char *eq = strchr(line, '=');
    if(!eq) continue;
    *eq = 0;
    int a = ps5tm_backdrops_area(line);
    int m = mode_index(eq + 1);
    if(a >= 0 && m >= 0) modes[a] = m;
  }
  fclose(f);
}

/* Written beside and renamed over, so a console switched off mid-write keeps the old file. Lock held. */
static int
write_modes(const int modes[BD_AREAS]) {
  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(BD_DIR, 0755);
  FILE *f = fopen(BD_MODES ".part", "w");
  if(!f) return errno ? errno : EIO;
  for(int i = 0; i < BD_AREAS; i++)
    fprintf(f, "%s=%s\n", k_areas[i], k_modes[modes[i]]);
  int ok = fflush(f) == 0 && !ferror(f);
  if(fclose(f) != 0) ok = 0;
  if(!ok || rename(BD_MODES ".part", BD_MODES) != 0) {
    int e = errno ? errno : EIO;
    unlink(BD_MODES ".part");
    return e;
  }
  return 0;
}

cJSON *
ps5tm_backdrops_json(void) {
  int modes[BD_AREAS];
  cJSON *o = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(o, "areas");
  pthread_mutex_lock(&g_lock);
  read_modes(modes);
  for(int i = 0; i < BD_AREAS; i++) {
    char path[128], stamp[48] = "";
    struct stat st;
    int custom = ps5tm_backdrops_path(i, path, sizeof(path)) == 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode)
                 && st.st_size > 0;
    if(custom) snprintf(stamp, sizeof(stamp), "%lld-%lld", (long long)st.st_mtime, (long long)st.st_size);
    cJSON *a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "id", k_areas[i]);
    cJSON_AddStringToObject(a, "label", k_labels[i]);
    /* What the tile shows: "custom" without a file is the standard picture again. */
    cJSON_AddStringToObject(a, "mode", k_modes[modes[i] == 1 && !custom ? 0 : modes[i]]);
    cJSON_AddBoolToObject(a, "custom", custom);
    cJSON_AddStringToObject(a, "stamp", stamp);
    cJSON_AddNumberToObject(a, "bytes", custom ? (double)st.st_size : 0.0);
    cJSON_AddItemToArray(arr, a);
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}

int
ps5tm_backdrops_set_mode(int area, const char *mode) {
  int m = mode_index(mode);
  if(area < 0 || area >= BD_AREAS || m < 0) return EINVAL;
  int modes[BD_AREAS];
  pthread_mutex_lock(&g_lock);
  read_modes(modes);
  int rc = 0;
  if(m == 1) {
    char path[128];
    struct stat st;
    if(ps5tm_backdrops_path(area, path, sizeof(path)) != 0 || stat(path, &st) != 0) rc = ENOENT;
  }
  if(!rc && modes[area] != m) {
    modes[area] = m;
    rc = write_modes(modes);
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}

int
ps5tm_backdrops_take(int area, const char *part) {
  char path[128];
  if(ps5tm_backdrops_path(area, path, sizeof(path)) != 0) return EINVAL;
  /* Only what the page makes: a JPEG (FF D8 FF). Anything else would fail in the tile's decoder anyway. */
  unsigned char magic[3] = { 0, 0, 0 };
  FILE *f = fopen(part, "rb");
  size_t got = f ? fread(magic, 1, sizeof(magic), f) : 0;
  if(f) fclose(f);
  if(got != 3 || magic[0] != 0xFF || magic[1] != 0xD8 || magic[2] != 0xFF) {
    unlink(part);
    return EILSEQ;
  }
  int modes[BD_AREAS];
  pthread_mutex_lock(&g_lock);
  int rc = rename(part, path) == 0 ? 0 : (errno ? errno : EIO);
  if(!rc) {
    read_modes(modes);
    modes[area] = 1;                                    /* a new picture is meant to be seen */
    rc = write_modes(modes);
  }
  pthread_mutex_unlock(&g_lock);
  if(rc) unlink(part);
  else PS5TM_INFO("backdrop_saved", "Hintergrundbild für %s gespeichert.", k_labels[area]);
  return rc;
}

int
ps5tm_backdrops_remove(int area) {
  char path[128];
  if(ps5tm_backdrops_path(area, path, sizeof(path)) != 0) return EINVAL;
  int modes[BD_AREAS];
  pthread_mutex_lock(&g_lock);
  int rc = unlink(path) == 0 || errno == ENOENT ? 0 : errno;
  read_modes(modes);
  if(!rc && modes[area] == 1) {
    modes[area] = 0;
    rc = write_modes(modes);
  }
  pthread_mutex_unlock(&g_lock);
  if(!rc) PS5TM_INFO("backdrop_removed", "Eigenes Hintergrundbild für %s gelöscht.", k_labels[area]);
  return rc;
}
