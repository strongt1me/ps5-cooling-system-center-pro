/* Keeps the home-screen tile of a helper program away (14.11.2026, wish of the user: Dump Forge "ohne Kachel, wie bei Kraken Mount und PS4Mount").
 *
 * Kraken Mount (kraken.c) and Dump Forge (dumpforge.c) are programs of their own, and each makes a tile of its own at every start:
 * /user/app/<title id> with sce_sys/param.json, registered with the system. The app wants only its page or its format, no tile. Both
 * programs leave the place alone when /user/app/<title id> exists and is not a folder (Kraken Mount: ensure_tile, when its marker is
 * missing; Dump Forge: tile.rs, "is not a folder"), so before every start the app puts an empty file there. A tile made earlier (a marker of
 * the program says it is theirs: Kraken Mount's tile.ok, Dump Forge's tile-<title id>.owner) is uninstalled first and the marker removed; a
 * folder with no marker is another app's and is left as it is. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

void
ps5tm_tile_keep_away(const char *tile_id, const char *name, const char *const *markers, int n_markers) {
  char path[64], param[96];
  snprintf(path, sizeof path, "/user/app/%s", tile_id);
  snprintf(param, sizeof param, "%s/sce_sys/param.json", path);
  struct stat st;
  int marker = 0;
  for(int i = 0; i < n_markers; i++)
    if(stat(markers[i], &st) == 0) marker = 1;
  int present = lstat(path, &st) == 0;
  if(marker) {
    if(present && S_ISDIR(st.st_mode) && stat(param, &st) == 0) {
      int pat = 0, add = 0;
      char why[300] = "";
      int rc = ps5tm_pkginst_uninstall(tile_id, &pat, &add, why, sizeof why);
      if(rc != 0) {
        PS5TM_WARN("tile_keep_away", "Die Kachel von %s (%s) ließ sich nicht entfernen: %s", name, tile_id, why[0] ? why : "ohne Angabe");
        return;                                         /* marker and tile stay together: nothing is made worse */
      }
      PS5TM_INFO("tile_keep_away", "Die Kachel von %s (%s) ist entfernt.", name, tile_id);
      for(int i = 0; i < 50 && lstat(path, &st) == 0; i++) usleep(100 * 1000);
    }
    for(int i = 0; i < n_markers; i++) unlink(markers[i]);
    present = lstat(path, &st) == 0;
  }
  if(!present) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if(fd >= 0) close(fd);
    else PS5TM_WARN("tile_keep_away", "%s ließ sich nicht anlegen: %s", path, strerror(errno));
  }
}
