/* PS5 Cooling & System Center - Pro: the folder the app gives Kraken Mount is called Kraken-Games since 27.11.2026, before that FPKG-Games (krakenmig.h).
 * Wish of the user: Kraken Mount (page FPKGMount) takes game folders and images as well as .pkg files, and the old name made it look as if only FPKGs
 * belonged there; on a console set up before, the folders are renamed ("Ordner umbenennen"). kraken.c runs this right before it starts Kraken Mount
 * (it does not run then, and so no game of it runs: it refuses to stop while one does) and writes the log; the decisions and the renaming are here,
 * with the folders as parameters, so a host test can try them on folders of its own (test/krakenmig).
 *
 * Rules: only the app's own list in Kraken Mount's config.toml is changed (the line the app writes, under the old name); a list changed by hand stays,
 * and so do its folders. A folder is only renamed (rename(), on its own drive): nothing is copied, nothing overwritten, nothing deleted but an emptied
 * old folder. Where Kraken-Games is there already, the entries move one by one and a name taken there stays where it is. */
#ifndef PS5TM_KRAKENMIG_H
#define PS5TM_KRAKENMIG_H

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PS5TM_KRAKEN_GAMES
#define PS5TM_KRAKEN_GAMES     "Kraken-Games"
#define PS5TM_KRAKEN_GAMES_OLD "FPKG-Games"
#endif

/* The line "scanpath = [...]" the app writes for the folder name `folder`: /data/<folder>, /mnt/usb0..7/<folder>, /mnt/ext0..1/<folder>, as
   Kraken Mount writes it back when its page saves the settings (one line, ", " between the quoted paths). 0 when it fits into buf. */
static inline int
ps5tm_krmig_scanpath(const char *folder, char *buf, size_t n) {
  if(!folder || !buf || !n) return -1;
  size_t o = (size_t)snprintf(buf, n, "scanpath = [\"/data/%s\"", folder);
  for(int i = 0; i < 8 && o < n; i++) o += (size_t)snprintf(buf + o, n - o, ", \"/mnt/usb%d/%s\"", i, folder);
  for(int i = 0; i < 2 && o < n; i++) o += (size_t)snprintf(buf + o, n - o, ", \"/mnt/ext%d/%s\"", i, folder);
  if(o < n) o += (size_t)snprintf(buf + o, n - o, "]");
  return o < n ? 0 : -1;
}

enum { PS5TM_KRMIG_CFG_OTHER = 0, PS5TM_KRMIG_CFG_NEW = 1, PS5TM_KRMIG_CFG_OLD = 2 };

/* What the config text says about the app's folders: CFG_NEW (its list under the new name), CFG_OLD (under the old name: *out is then the same
   text, malloc'd, with that one line in the new name and everything else, the line ends too, as it was), CFG_OTHER (no such line: Kraken Mount's
   own defaults or a list changed by hand). Spaces and a CR at the end of the line do not count. */
static inline int
ps5tm_krmig_config(const char *text, size_t len, char **out) {
  if(out) *out = NULL;
  char want_old[640], want_new[640];
  if(!text || ps5tm_krmig_scanpath(PS5TM_KRAKEN_GAMES_OLD, want_old, sizeof want_old) ||
     ps5tm_krmig_scanpath(PS5TM_KRAKEN_GAMES, want_new, sizeof want_new)) return PS5TM_KRMIG_CFG_OTHER;
  const size_t old_l = strlen(want_old), new_l = strlen(want_new);
  for(size_t at = 0; at < len;) {
    const char *line = text + at;
    const char *end = memchr(line, '\n', len - at);
    const size_t full = end ? (size_t)(end - line) : len - at;
    size_t k = full;
    while(k && (line[k - 1] == '\r' || line[k - 1] == ' ' || line[k - 1] == '\t')) k--;
    if(k == new_l && !memcmp(line, want_new, k)) return PS5TM_KRMIG_CFG_NEW;
    if(k == old_l && !memcmp(line, want_old, k)) {
      if(out) {
        const size_t rest = len - (at + k);
        char *o = malloc(at + new_l + rest + 1);
        if(!o) return PS5TM_KRMIG_CFG_OTHER;
        memcpy(o, text, at);
        memcpy(o + at, want_new, new_l);
        memcpy(o + at + new_l, line + k, rest);
        o[at + new_l + rest] = 0;
        *out = o;
      }
      return PS5TM_KRMIG_CFG_OLD;
    }
    at += full + 1;
  }
  return PS5TM_KRMIG_CFG_OTHER;
}

enum {
  PS5TM_KRMIG_NONE = 0,     /* no old folder there */
  PS5TM_KRMIG_RENAMED,      /* the old folder now has the new name */
  PS5TM_KRMIG_MERGED,       /* Kraken-Games was there: every entry moved, the emptied old folder is gone */
  PS5TM_KRMIG_PARTLY,       /* Kraken-Games was there: some entries stayed (their name is taken there), the old folder stays */
  PS5TM_KRMIG_FAILED,       /* the rename was refused (err) */
  PS5TM_KRMIG_NOT_DIR       /* Kraken-Games is there and is no folder: nothing moved */
};

typedef struct {
  int      state;
  int      err;              /* errno of a refused rename */
  unsigned moved, kept;
  char     first_kept[96];   /* the first entry that stayed */
} ps5tm_krmig_folder_t;

/* <base>/FPKG-Games becomes <base>/Kraken-Games (base: "/data", "/mnt/usb0", ... or a test folder). */
static inline void
ps5tm_krmig_folder(const char *base, ps5tm_krmig_folder_t *r) {
  memset(r, 0, sizeof(*r));
  char from[320], to[320];
  if(snprintf(from, sizeof from, "%s/%s", base, PS5TM_KRAKEN_GAMES_OLD) >= (int)sizeof from ||
     snprintf(to, sizeof to, "%s/%s", base, PS5TM_KRAKEN_GAMES) >= (int)sizeof to) return;
  struct stat st, st2;
  if(lstat(from, &st) != 0 || !S_ISDIR(st.st_mode)) return;
  if(lstat(to, &st2) != 0) {
    if(rename(from, to) == 0) r->state = PS5TM_KRMIG_RENAMED;
    else { r->state = PS5TM_KRMIG_FAILED; r->err = errno; }
    return;
  }
  if(!S_ISDIR(st2.st_mode)) { r->state = PS5TM_KRMIG_NOT_DIR; return; }
  DIR *d = opendir(from);
  if(!d) { r->state = PS5TM_KRMIG_FAILED; r->err = errno; return; }
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char a[640], b[640];
    const int fits = snprintf(a, sizeof a, "%s/%s", from, e->d_name) < (int)sizeof a && snprintf(b, sizeof b, "%s/%s", to, e->d_name) < (int)sizeof b;
    /* never over something in Kraken-Games: rename() would replace a file there without asking */
    if(!fits || lstat(b, &st2) == 0 || errno != ENOENT || rename(a, b) != 0) {
      if(!r->kept++) snprintf(r->first_kept, sizeof r->first_kept, "%.95s", e->d_name);   /* a longer name is cut */
      continue;
    }
    r->moved++;
  }
  closedir(d);
  r->state = !r->kept && rmdir(from) == 0 ? PS5TM_KRMIG_MERGED : PS5TM_KRMIG_PARTLY;
}

#endif
