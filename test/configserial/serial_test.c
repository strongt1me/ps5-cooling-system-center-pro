/* Host test: the setting serial_full (the serial number on the System page, shortened by default) in src/config.c:
   default 0, any other number counts as 1, it survives save and load, and a file without it leaves the default. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps5tm.h"

/* what config.c needs from the rest of the app */
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }

uint64_t ps5tm_mono_ms(void) { return 0; }

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while(0)

static void
write_file(const char *text) {
  FILE *f = fopen(PS5TM_CONFIG_PATH, "w");
  if(!f) { printf("cannot write %s\n", PS5TM_CONFIG_PATH); exit(2); }
  fputs(text, f);
  fclose(f);
}

int
main(void) {
  ps5tm_config_t c;
  ps5tm_config_defaults(&c);
  CHECK(c.serial_full == 0);                       /* shortened by default */
  c.serial_full = 7;
  ps5tm_config_clamp(&c);
  CHECK(c.serial_full == 1);                       /* any number is an on */
  c.serial_full = 0;
  ps5tm_config_clamp(&c);
  CHECK(c.serial_full == 0);

  /* a file without the key (every file written before 21.11.2026): the default stays */
  write_file("{\"schema_version\": 1}\n");
  ps5tm_config_load();
  CHECK(g_config.serial_full == 0);

  /* a file with the key on */
  write_file("{\"schema_version\": 1, \"serial_full\": 1}\n");
  ps5tm_config_load();
  CHECK(g_config.serial_full == 1);

  /* saved and read again */
  ps5tm_config_lock();
  g_config.serial_full = 1;
  ps5tm_config_unlock();
  CHECK(ps5tm_config_save() == 0);
  g_config.serial_full = 0;
  ps5tm_config_load();
  CHECK(g_config.serial_full == 1);

  ps5tm_config_lock();
  g_config.serial_full = 0;
  ps5tm_config_unlock();
  CHECK(ps5tm_config_save() == 0);
  g_config.serial_full = 1;
  ps5tm_config_load();
  CHECK(g_config.serial_full == 0);

  /* a number other than 0/1 in the file counts as on */
  write_file("{\"schema_version\": 1, \"serial_full\": 5}\n");
  ps5tm_config_load();
  CHECK(g_config.serial_full == 1);

  printf(fails ? "serial_full: %d FAILED\n" : "serial_full: ok\n", fails);
  return fails ? 1 : 0;
}
