/* Installing packages: the internals of pkgstream.c (the server the system's installer reads a package from) and
 * pkginstall.c (the job that starts the helper, watches the installation and checks the result). What the HTTP layer
 * needs is declared in ps5tm.h; this header is for the two files and their tests. */
#ifndef PS5TM_PKGINST_H
#define PS5TM_PKGINST_H

#include <stddef.h>
#include <stdint.h>

#include "ps5tm.h"

/* One slice of the package: bytes [file_off, file_off + size) of the file at path are the bytes
   [logical, logical + size) of the whole package. A package in one file is one slice; the parts of a split
   package are one slice each, in order, without gaps. */
typedef struct {
  char     path[PS5TM_PKG_PATH];
  uint64_t file_off, size, logical;
  uint64_t file_size;                     /* the file as the search saw it: its length ... */
  int64_t  mtime;                         /* ... and when it was last written; 0 and 0: not known, not checked */
} ps5tm_pkgslice_t;

typedef struct {
  const ps5tm_pkgslice_t *slices;
  unsigned                nslices;
  uint64_t                total;          /* the length of the whole package */
  const char             *name;           /* the one file name the package is served under */
  const unsigned char    *icon;           /* optional: the icon, under icon_name */
  size_t                  icon_n;
  const char             *icon_name;
  int                     port;           /* 0: PKGI_STREAM_PORT */
} ps5tm_pkgstream_cfg_t;

typedef struct {
  int      running;
  uint64_t total;
  uint64_t served;                        /* every byte sent, one that went twice counts twice */
  uint64_t covered;                       /* bytes of the package sent at least once (an estimate once coverage_lost) */
  int      complete;                      /* every byte of the package has been sent at least once (exact; never 1 once coverage_lost) */
  int      coverage_lost;                 /* the list of what was sent outgrew its bound: only `served` can be told */
  uint64_t requests;
  uint64_t read_errors;                   /* a slice that could not be read (the file changed or went away) */
  unsigned conns, peak_conns;
  uint64_t idle_ms;                       /* since the last byte moved; 0 while nothing has */
} ps5tm_pkgstream_stats_t;

/* The pieces of the package under a search id (pkgscan.c): one slice for a package in one file, a slice per part for a
   split one. 0 with the number of slices and the length of the package, or -1 with the reason in err. */
int  ps5tm_pkgscan_slices(const char *id, ps5tm_pkgslice_t *out, unsigned max, unsigned *n, uint64_t *total,
                          char *err, size_t err_len);

/* What the package inside a set of parts says about itself (its container, read through the slices), not what the
   header of the first part claims (pkgparse.c). 0, or -1 when it cannot be read as a package. */
int  ps5tm_pkg_parse_slices(const ps5tm_pkgslice_t *sl, unsigned n, uint64_t total, ps5tm_pkg_t *out);

/* 0, or -1 with a reason in err (the port is taken, the slices have a gap, ...). */
int  ps5tm_pkgstream_start(const ps5tm_pkgstream_cfg_t *cfg, char *err, size_t err_len);
void ps5tm_pkgstream_stop(void);
void ps5tm_pkgstream_stats(ps5tm_pkgstream_stats_t *st);

#endif
