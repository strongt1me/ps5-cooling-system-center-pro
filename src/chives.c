/* PS5 Cooling & System Center - Pro: the sidecar of an exported save (chives.h). */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chives.h"
#include "third_party/cJSON.h"

static uint64_t
le(const uint8_t *p, int n) {
  uint64_t v = 0;
  for(int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
  return v;
}

void
chives_init(chives_t *g) {
  memset(g, 0, sizeof(*g));
}

void
chives_free(chives_t *g) {
  free(g->own);
  g->own = NULL;
  g->nown = g->own_cap = 0;
}

int
chives_path_safe(const char *name) {
  if(!name || !name[0] || name[0] == '/' || strchr(name, '\\') || strchr(name, ':')) return 0;
  for(const unsigned char *c = (const unsigned char *)name; *c; c++)
    if(*c < 0x20 || *c == 0x7f) return 0;
  const char *p = name;
  for(;;) {
    const char *end = strchr(p, '/');
    const size_t n = end ? (size_t)(end - p) : strlen(p);
    if(!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return 0;
    if(!end) break;
    p = end + 1;
  }
  return 1;
}

int
chives_is_sidecar(const char *name) {
  while(name[0] == '.' && name[1] == '/') name += 2;
  const char *want = "chives.json";
  while(*name && *want && tolower((unsigned char)*name) == *want) { name++; want++; }
  return !*name && !*want;
}

int
chives_own_add(chives_t *g, const char *path, uint32_t uid, uint32_t gid, unsigned mode) {
  if(!chives_path_safe(path) || strlen(path) >= CHIVES_PATH_MAX - 1 || mode > 07777) return -1;
  if(g->nown >= CHIVES_MAX_OWN_READ) return -1;
  if(g->nown == g->own_cap) {
    const unsigned cap = g->own_cap ? g->own_cap * 2 : 64;
    chives_own_t *n = realloc(g->own, (size_t)cap * sizeof(*n));
    if(!n) return -1;
    g->own = n;
    g->own_cap = cap;
  }
  chives_own_t *e = &g->own[g->nown++];
  memset(e, 0, sizeof(*e));
  snprintf(e->path, sizeof(e->path), "%s", path);
  e->uid = uid;
  e->gid = gid;
  e->mode = (uint16_t)mode;
  return 0;
}

int
chives_valid(const chives_t *g) {
  if(g->version != 2) return 0;
  if(g->block_size != 32768 && g->block_size != 65536) return 0;
  if(!g->block_count || g->block_count > CHIVES_MAX_IMAGE / g->block_size) return 0;
  if(g->has_fs_ext && (g->fs_ext >= g->block_count || (g->fs_ext && g->block_size != 65536))) return 0;
  if(g->block_count * g->block_size < (uint64_t)g->block_size * 2) return 0;
  if(g->image_size < 0x40 || g->image_size > CHIVES_MAX_IMAGE) return 0;
  return 1;
}

int
chives_from_header(const uint8_t *hdr, size_t len, uint64_t file_size, chives_t *g) {
  chives_init(g);
  if(!hdr || len < 0x40 || le(hdr + 8, 8) != UINT64_C(20130315)) return -1;
  g->version = le(hdr, 8);
  if(g->version != 2) return -1;                         /* 1 is a PS4 save: those are carried another way */
  g->mode_raw = (uint16_t)le(hdr + 0x1c, 2);
  g->block_size = (uint32_t)le(hdr + 0x20, 4);
  g->backup_header_count = (uint32_t)le(hdr + 0x24, 4);
  g->block_count = le(hdr + 0x38, 8);
  g->image_size = file_size;
  if(len >= 0x36c) {
    g->fs_ext = (uint32_t)le(hdr + 0x368, 4);
    g->has_fs_ext = 1;
  }
  return chives_valid(g) ? 0 : -1;
}

/* ------------------------------------------------------------------ writing */

static size_t
esc(const char *in, char *out, size_t cap) {
  size_t n = 0;
  for(const unsigned char *c = (const unsigned char *)in; *c && n + 8 < cap; c++) {
    if(*c == '"' || *c == '\\') { out[n++] = '\\'; out[n++] = (char)*c; }
    else if(*c < 0x20) n += (size_t)snprintf(out + n, cap - n, "\\u%04x", *c);
    else out[n++] = (char)*c;
  }
  if(cap) out[n < cap ? n : cap - 1] = 0;
  return n;
}

int
chives_write(const chives_t *g, char *out, size_t cap) {
  char t[64], d[256], f[512];
  esc(g->title_id, t, sizeof(t));
  esc(g->save_dir, d, sizeof(d));
  esc(g->image_name, f, sizeof(f));
  int n = snprintf(out, cap,
                   "{\n  \"schema_version\": 1,\n  \"platform\": \"PS5\",\n"
                   "  \"title_id\": \"%s\",\n  \"save_directory\": \"%s\",\n  \"encrypted_name\": \"%s\",\n"
                   "  \"original_image_size_bytes\": %llu,\n"
                   "  \"pfs\": {\n    \"version\": %llu,\n    \"block_size_bytes\": %u,\n    \"block_count\": %llu,\n"
                   "    \"mode_raw\": %u,\n    \"backup_header_count\": %u,\n    \"fs_ext\": %u\n  }",
                   t, d, f, (unsigned long long)g->image_size, (unsigned long long)g->version, g->block_size,
                   (unsigned long long)g->block_count, (unsigned)g->mode_raw, g->backup_header_count, g->fs_ext);
  if(n < 0 || (size_t)n >= cap) return -1;
  if(g->nown && g->nown <= CHIVES_MAX_OWN_WRITE) {
    int w = snprintf(out + n, cap - (size_t)n, ",\n  \"ownership\": [\n");
    if(w < 0 || (size_t)w >= cap - (size_t)n) return -1;
    n += w;
    for(unsigned i = 0; i < g->nown; i++) {
      char p[CHIVES_PATH_MAX * 2];
      esc(g->own[i].path, p, sizeof(p));
      w = snprintf(out + n, cap - (size_t)n, "    {\"path\":\"%s\",\"uid\":%u,\"gid\":%u,\"mode\":%u}%s\n", p, g->own[i].uid,
                   g->own[i].gid, g->own[i].mode, i + 1 == g->nown ? "" : ",");
      if(w < 0 || (size_t)w >= cap - (size_t)n) return -1;
      n += w;
    }
    w = snprintf(out + n, cap - (size_t)n, "  ]");
    if(w < 0 || (size_t)w >= cap - (size_t)n) return -1;
    n += w;
  }
  int w = snprintf(out + n, cap - (size_t)n, "\n}\n");
  if(w < 0 || (size_t)w >= cap - (size_t)n) return -1;
  return n + w;
}

/* ------------------------------------------------------------------ reading */

/* A whole non-negative number that fits the limit, from a JSON number. */
static int
num(const cJSON *o, const char *key, uint64_t max, uint64_t *out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)o, key);
  if(!cJSON_IsNumber(v)) return -1;
  const double d = v->valuedouble;
  if(!(d >= 0) || d > 9007199254740992.0 || d != floor(d) || d > (double)max) return -1;
  *out = (uint64_t)d;
  return 0;
}

static void
str(const cJSON *o, const char *key, char *out, size_t cap) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)o, key);
  out[0] = 0;
  if(!cJSON_IsString(v) || !v->valuestring) return;
  for(const unsigned char *c = (const unsigned char *)v->valuestring; *c; c++)
    if(*c < 0x20 || *c == 0x7f) return;                  /* a name with a control character is no name */
  snprintf(out, cap, "%s", v->valuestring);
}

int
chives_parse(const char *json, size_t len, chives_t *g) {
  chives_init(g);
  if(!json || !len || len > CHIVES_MAX_JSON) return -1;
  cJSON *root = cJSON_ParseWithLength(json, len);
  if(!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }
  int rc = -1;
  uint64_t v;
  const cJSON *plat = cJSON_GetObjectItemCaseSensitive(root, "platform");
  const cJSON *pfs = cJSON_GetObjectItemCaseSensitive(root, "pfs");
  if(num(root, "schema_version", 1, &v) != 0 || v != 1) goto out;
  if(!cJSON_IsString(plat) || !plat->valuestring || strcmp(plat->valuestring, "PS5")) goto out;
  if(num(root, "original_image_size_bytes", CHIVES_MAX_IMAGE, &g->image_size) != 0) goto out;
  if(!cJSON_IsObject(pfs)) goto out;
  if(num(pfs, "version", 2, &g->version) != 0) goto out;
  if(num(pfs, "block_size_bytes", 65536, &v) != 0) goto out;
  g->block_size = (uint32_t)v;
  if(num(pfs, "block_count", CHIVES_MAX_IMAGE, &g->block_count) != 0) goto out;
  if(num(pfs, "mode_raw", 0xFFFF, &v) == 0) g->mode_raw = (uint16_t)v;
  if(num(pfs, "backup_header_count", 0xFFFFFFFFu, &v) == 0) g->backup_header_count = (uint32_t)v;
  if(num(pfs, "fs_ext", 0xFFFFFFFFu, &v) == 0) { g->fs_ext = (uint32_t)v; g->has_fs_ext = 1; }
  str(root, "title_id", g->title_id, sizeof(g->title_id));
  str(root, "save_directory", g->save_dir, sizeof(g->save_dir));
  str(root, "encrypted_name", g->image_name, sizeof(g->image_name));
  if(g->image_name[0] && (!chives_path_safe(g->image_name) || strchr(g->image_name, '/'))) goto out;
  if(!chives_valid(g)) goto out;

  const cJSON *own = cJSON_GetObjectItemCaseSensitive(root, "ownership");
  if(own) {
    if(!cJSON_IsArray(own)) goto out;
    const cJSON *e;
    cJSON_ArrayForEach(e, own) {
      uint64_t uid, gid, mode;
      const cJSON *p = cJSON_GetObjectItemCaseSensitive((cJSON *)e, "path");
      if(!cJSON_IsObject(e) || !cJSON_IsString(p) || !p->valuestring) goto out;
      if(num(e, "uid", 0xFFFFFFFFu, &uid) != 0 || num(e, "gid", 0xFFFFFFFFu, &gid) != 0 || num(e, "mode", 07777, &mode) != 0) goto out;
      for(unsigned i = 0; i < g->nown; i++)
        if(!strcmp(g->own[i].path, p->valuestring)) goto out;           /* a path twice is not a sound list */
      if(chives_own_add(g, p->valuestring, (uint32_t)uid, (uint32_t)gid, (unsigned)mode) != 0) goto out;
    }
  }
  rc = 0;
out:
  cJSON_Delete(root);
  if(rc != 0) chives_free(g);
  return rc;
}
