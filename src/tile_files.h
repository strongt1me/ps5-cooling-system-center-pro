/* The home-screen tile as a folder of files, embedded at build time by
 * tools/gen_tile_files.py from tile/sce_sys/.
 *
 * This is what the app writes to /user/app/<id>/sce_sys before it asks the
 * console to register the tile — the same few files the other home-screen
 * launchers on a console consist of (param.json, icon0.png, a background).
 * No package, and no second program to send. A file that two names share
 * (pic0.png and pic1.png are one picture) is stored once. */
#ifndef PS5TM_TILE_FILES_H
#define PS5TM_TILE_FILES_H

typedef struct {
  const char          *name;    /* file name inside sce_sys/               */
  const unsigned char *data;
  unsigned long        len;
} ps5tm_tile_file_t;

extern const ps5tm_tile_file_t ps5tm_tile_files[];
extern const unsigned          ps5tm_tile_file_count;

#endif
