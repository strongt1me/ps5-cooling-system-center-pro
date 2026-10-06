/* The home-screen tile package, embedded at build time by
 * tools/gen_pkg_blob.py. ps5tm_tile_pkg_len is 0 when the build had no
 * package to embed, which the tile code treats as "nothing to install". */
#ifndef PS5TM_TILE_PKG_H
#define PS5TM_TILE_PKG_H

extern const unsigned char ps5tm_tile_pkg[];
extern const unsigned long ps5tm_tile_pkg_len;

#endif
