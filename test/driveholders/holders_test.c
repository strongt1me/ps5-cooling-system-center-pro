/* Host test of src/driveholders.c (who has a file open on a drive), the Linux variant that reads /proc (21.11.2026). */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static int
holder_of(const ps5tm_holder_t *h, int n, int pid) {
  for(int i = 0; i < n; i++) if(h[i].pid == pid) return i;
  return -1;
}

int
main(void) {
  char dir[] = "/tmp/ps5tm-holders-XXXXXX";
  if(!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
  char file[300], other[300];
  snprintf(file, sizeof(file), "%s/a.bin", dir);
  snprintf(other, sizeof(other), "%sx", dir);                 /* a name that only begins like the mount: not under it */
  mkdir(other, 0755);
  char file2[320];
  snprintf(file2, sizeof(file2), "%s/b.bin", other);

  ps5tm_holder_t h[16];
  int me = (int)getpid();
  int n = ps5tm_drive_holders(dir, h, 16);
  CHECK(n >= 0 && holder_of(h, n, me) < 0);                    /* nothing open yet */

  int fd = open(file, O_CREAT | O_RDWR, 0644);
  int fd2 = open(file2, O_CREAT | O_RDWR, 0644);               /* beside the mount, not on it */
  CHECK(fd >= 0 && fd2 >= 0);
  n = ps5tm_drive_holders(dir, h, 16);
  int i = holder_of(h, n, me);
  CHECK(n >= 1 && i >= 0);
  if(i >= 0) {
    CHECK(h[i].count == 1);                                    /* the file on the mount, not the one beside it */
    CHECK(strstr(h[i].sample, "a.bin") != NULL);
    CHECK(h[i].name[0] != 0);
  }

  DIR *d = opendir(dir);                                       /* a folder counts, too */
  n = ps5tm_drive_holders(dir, h, 16);
  i = holder_of(h, n, me);
  CHECK(i >= 0 && h[i].count == 2);
  closedir(d);
  close(fd);
  n = ps5tm_drive_holders(dir, h, 16);
  CHECK(n >= 0 && holder_of(h, n, me) < 0);                    /* closed: gone from the list */

  char cwd[300];
  if(!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
  if(chdir(dir) == 0) {                                        /* the working directory counts */
    n = ps5tm_drive_holders(dir, h, 16);
    i = holder_of(h, n, me);
    CHECK(i >= 0 && h[i].count >= 1);
    if(cwd[0] && chdir(cwd) != 0) { /* the test ends anyway */ }
  }
  n = ps5tm_drive_holders(dir, h, 16);
  CHECK(holder_of(h, n, me) < 0);
  CHECK(ps5tm_drive_holders(NULL, h, 16) == -1 && ps5tm_drive_holders(dir, NULL, 16) == -1 && ps5tm_drive_holders(dir, h, 0) == -1);

  close(fd2);
  unlink(file);
  unlink(file2);
  rmdir(other);
  rmdir(dir);
  printf("driveholders: %d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
