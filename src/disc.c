/* The optical drive (21.10.2026): which disc is in it, its title ID, and a copy of a PS2 DVD or PS3 disc as an ISO.
 *
 * A port of ps-disc-detect (GPL-3.0, "disc detect": a payload that watches the drive and dumps PlayStation discs;
 * the file name of the archive was ps-disc-detect-main.zip), rewritten for this app: no endless loop in main(), the
 * state goes to the web page, one polling thread that can be switched off, one dump job. What was taken over:
 *
 *   - the SCSI route: /dev/xpt0 names the pass-through device of cd0, CAMIOCOMMAND sends GET CONFIGURATION (profile),
 *     READ DISC STRUCTURE (DVD book type), SET STREAMING / SET CD SPEED (maximum read speed)
 *   - the profile numbers the console's drive reports for its own discs (0xFF50 PS1 CD ... 0xFF90 PS5 BD)
 *   - where the title ID lies: PS1/PS2 SYSTEM.CNF (BOOT / BOOT2), PS3 PS3_GAME/PARAM.SFO, PS4 bd/param.sfo (after a
 *     0x800-byte header), PS5 bd/param.json (first PS5GD content, bytes 7..16 of the content id), else the first
 *     9-character folder under app/; GENERIC_PSn when nothing can be read
 *   - the dump: /dev/cd0 read in 128 KiB pieces into an ISO, CRC-32 + MD5 + SHA-1 of the image, named after Redump
 *   - the Redump database: /data/redump.db built by tools/build_redump_db.py of that project (tables serials, images,
 *     tracks; WITHOUT ROWID, which sqlite_ro.c reads since 21.10.2026). Used for names and for checking the image.
 *
 * Not taken over: CD images (bin + cue): the PS5's drive has no laser for CDs, a PS1 or PS2 CD is never seen; the IRD
 * for PS3 (the project itself keeps it switched off, the drive gives no disc key); the embedded database (the app's
 * size limit) and the PS4. Dumping needs the person's click, or the switch "automatisch sichern".
 *
 * The polling is off until the person turns it on. It opens /dev/xpt0 every 1.5 seconds and asks the drive two
 * questions; while a dump runs it waits. State in PS5TM_DATA_DIR/disc/settings.json, images in .../disc/. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_compat.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>

#include "ps5tm.h"
#include "sqlite_ro.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#define DISC_DIR        PS5TM_DATA_DIR "/disc"
#define DISC_SETTINGS   DISC_DIR "/settings.json"
#define DISC_DEV        "/dev/cd0"
#define DISC_MNT        "/mnt/disc"
#define REDUMP_PATH     "/data/redump.db"
#define REDUMP_MAX      (64ull * 1024 * 1024)
#define POLL_US         1500000
#define DUMP_CHUNK      (128 * 1024)
#define DUMP_RESERVE    (512ll << 20)
#define EVENTS          24

/* The PS5's headers say CAM_VERSION 0x19 and a CCB of 0x4E0 bytes; CAMIOCOMMAND of scsi_pass.h already encodes that. */
_Static_assert(sizeof(union ccb) == 0x4E0, "ccb size");
_Static_assert(CAMIOCOMMAND == 0xC4E01902u, "CAMIOCOMMAND");
_Static_assert(CAMGETPASSTHRU == 0xC4E01903u, "CAMGETPASSTHRU");

enum {
  D_NO_PROFILE = 0x0000, D_DVD_ROM = 0x0010, D_DVD_R = 0x0011, D_DVD_RAM = 0x0012, D_DVD_RW = 0x0013,
  D_DVD_PLUS_RW = 0x001A, D_DVD_PLUS_R = 0x001B, D_DVD_PLUS_RW_DL = 0x002A, D_DVD_PLUS_R_DL = 0x002B,
  D_BD_ROM = 0x0040, D_BD_R = 0x0041, D_BD_R_RANDOM = 0x0042, D_BD_RE = 0x0043,
  D_PS1_CD = 0xFF50, D_PS2_CD = 0xFF60, D_PS2_DVD = 0xFF61, D_PS3_DVD = 0xFF70, D_PS3_BD = 0xFF71,
  D_PS4_BD = 0xFF80, D_PPR_BD = 0xFF90, D_INCOMPAT = 0xFFFF,
  D_NOT_PRESENT = -1, D_TIMEOUT = -2, D_ERROR = -3
};

enum { JOB_IDLE = 0, JOB_RUNNING, JOB_VERIFY, JOB_DONE, JOB_FAILED, JOB_CANCELLED };
static const char *const k_job[] = { "idle", "running", "verifying", "done", "failed", "cancelled" };

enum { TS_NONE = 0, TS_WAIT, TS_OK, TS_GENERIC };

typedef struct {
  uint32_t crc;
  uint64_t size;
  uint32_t md5_s[4];
  uint32_t sha1_s[5];
  uint8_t  buf[64];
  uint8_t  md5[16], sha1[20];
} hash_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cam_lock = PTHREAD_MUTEX_INITIALIZER;
static int  g_loaded, g_enabled, g_auto, g_poll_up, g_poll_stop;
static int  g_drive = -1;                 /* 1 found, 0 no drive, -1 not asked yet */
static int  g_type = D_NOT_PRESENT;
static int  g_tstate;
static char g_serial[32], g_rname[256];
static int  g_rnames;                     /* how many Redump names the serial has */
static uint64_t g_image_bytes;
static char g_auto_serial[32];               /* the disc the automatic dump was already started for */
static struct { uint64_t t; char text[160]; } g_ev[EVENTS];
static int  g_nev;

static struct {
  int state;
  char stem[300], final_stem[300], serial[32], err[240], note[240];
  uint64_t done, total, t0, rate_ms, rate_bytes;
  double rate;
  int cancel;
  int verified;
} g_job;

/* ---------------------------------------------------------------- small helpers */

static void
event_locked(const char *fmt, ...) {
  char tmp[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  if(g_nev == EVENTS) { memmove(&g_ev[0], &g_ev[1], sizeof g_ev[0] * (EVENTS - 1)); g_nev--; }
  g_ev[g_nev].t = ps5tm_now_ms();
  snprintf(g_ev[g_nev].text, sizeof g_ev[g_nev].text, "%s", tmp);
  g_nev++;
  PS5TM_INFO("disc_event", "Laufwerk: %s", tmp);
}

static int
type_is_dumpable(int t) { return t == D_PS2_DVD || t == D_PS3_DVD || t == D_PS3_BD; }

static int
type_has_title(int t) {
  return t == D_PS1_CD || t == D_PS2_CD || t == D_PS2_DVD || t == D_PS3_DVD || t == D_PS3_BD || t == D_PS4_BD || t == D_PPR_BD;
}

static const char *
type_name(int t) {
  switch(t) {
    case D_NO_PROFILE: return "No Current Profile";
    case D_DVD_ROM: return "DVD-ROM";
    case D_DVD_R: return "DVD-R";
    case D_DVD_RAM: return "DVD-RAM";
    case D_DVD_RW: return "DVD-RW";
    case D_DVD_PLUS_RW: return "DVD+RW";
    case D_DVD_PLUS_R: return "DVD+R";
    case D_DVD_PLUS_RW_DL: return "DVD+RW DL";
    case D_DVD_PLUS_R_DL: return "DVD+R DL";
    case D_BD_ROM: return "BD-ROM";
    case D_BD_R: case D_BD_R_RANDOM: return "BD-R";
    case D_BD_RE: return "BD-RE";
    case D_PS1_CD: return "PS1 CD-ROM";
    case D_PS2_CD: return "PS2 CD-ROM";
    case D_PS2_DVD: return "PS2 DVD-ROM";
    case D_PS3_DVD: return "PS3 DVD-ROM";
    case D_PS3_BD: return "PS3 BD-ROM";
    case D_PS4_BD: return "PS4 BD-ROM";
    case D_PPR_BD: return "PS5 BD-ROM";
    case D_INCOMPAT: return "INCOMPATIBLE MEDIUM";
    case D_NOT_PRESENT: return "MEDIUM NOT PRESENT";
    case D_TIMEOUT: return "TIMEOUT";
    case D_ERROR: return "ERROR";
    default: return "Not Conforming to a Standard Profile";
  }
}

static char *
read_small(const char *path, size_t max) {
  FILE *f = fopen(path, "rb");
  if(!f) return NULL;
  char *b = malloc(max + 1);
  if(!b) { fclose(f); return NULL; }
  size_t n = fread(b, 1, max, f);
  fclose(f);
  b[n] = 0;
  return b;
}

static int
write_atomic(const char *path, const char *data, size_t n) {
  char tmp[600];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if(!f) return -1;
  size_t w = fwrite(data, 1, n, f);
  int rc = fclose(f);
  if(w != n || rc != 0) { unlink(tmp); return -1; }
  return rename(tmp, path);
}

static void
settings_save(void) {
  char b[96];
  int n = snprintf(b, sizeof b, "{\"enabled\":%s,\"auto_dump\":%s}", g_enabled ? "true" : "false", g_auto ? "true" : "false");
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(DISC_DIR, 0777);
  if(write_atomic(DISC_SETTINGS, b, (size_t)n) != 0) PS5TM_WARN("disc_settings", "Laufwerk: Die Einstellungen ließen sich nicht speichern.");
}

static void
settings_load_locked(void) {
  if(g_loaded) return;
  g_loaded = 1;
  char *b = read_small(DISC_SETTINGS, 4096);
  cJSON *j = b ? cJSON_Parse(b) : NULL;
  free(b);
  if(j) {
    g_enabled = cJSON_IsTrue(cJSON_GetObjectItem(j, "enabled"));
    g_auto = cJSON_IsTrue(cJSON_GetObjectItem(j, "auto_dump"));
    cJSON_Delete(j);
  }
}

/* ---------------------------------------------------------------- serials (disc_serial.c) */

static void
set_serial(char *dest, size_t n, const char *src) { snprintf(dest, n, "%s", src ? src : ""); }

/* "cdrom0:\SLUS_209.46;1" -> "SLUS-20946"; the letters, a hyphen, the digits. */
static void
sanitize_serial(const char *src, char *dest, size_t n) {
  dest[0] = 0;
  if(!src || n < 3) return;
  const char *start = src, *last_sep = NULL, *colon = NULL;
  while(*start == ' ' || *start == '\t') start++;
  for(const char *p = start; *p && *p != ';' && *p != '\r' && *p != '\n'; p++) {
    if(*p == '\\' || *p == '/') last_sep = p;
    else if(*p == ':' && !colon) colon = p;
  }
  if(last_sep) start = last_sep + 1;
  else if(colon) start = colon + 1;
  while(*start == ' ' || *start == '\t' || *start == '\\' || *start == '/') start++;
  size_t i = 0;
  while(*start && i < n - 1) {
    if(*start == ';' || *start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') break;
    if(isalnum((unsigned char)*start)) dest[i++] = (char)toupper((unsigned char)*start);
    start++;
  }
  dest[i] = 0;
  if(i > 4 && isalpha((unsigned char)dest[0])) {
    size_t letters = 0;
    while(letters < i && isalpha((unsigned char)dest[letters])) letters++;
    if(letters > 0 && letters < i && i + 1 < n) {
      memmove(dest + letters + 1, dest + letters, i - letters + 1);
      dest[letters] = '-';
    }
  }
}

static void
hyphenate_serial(char *dest, size_t n) {
  if(!dest[0]) return;
  char raw[32];
  set_serial(raw, sizeof raw, dest);
  sanitize_serial(raw, dest, n);
}

/* ---------------------------------------------------------------- hashes */

static uint32_t rol32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void
md5_block(uint32_t s[4], const uint8_t *p) {
  static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
    0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
    0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
    0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
    0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
  static const uint8_t R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
  uint32_t m[16], a = s[0], b = s[1], c = s[2], d = s[3];
  for(int i = 0; i < 16; i++) m[i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
  for(int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if(i < 16) { f = (b & c) | (~b & d); g = i; }
    else if(i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
    else if(i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
    else { f = c ^ (b | ~d); g = (7 * i) & 15; }
    uint32_t t = d;
    d = c;
    c = b;
    b = b + rol32(a + f + K[i] + m[g], R[i]);
    a = t;
  }
  s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}

static void
sha1_block(uint32_t s[5], const uint8_t *p) {
  uint32_t w[80], a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
  for(int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for(int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  for(int i = 0; i < 80; i++) {
    uint32_t f, k;
    if(i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
    else if(i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if(i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    uint32_t t = rol32(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol32(b, 30); b = a; a = t;
  }
  s[0] += a; s[1] += b; s[2] += c; s[3] += d; s[4] += e;
}

static void
hash_init(hash_t *h) {
  memset(h, 0, sizeof *h);
  h->md5_s[0] = 0x67452301; h->md5_s[1] = 0xefcdab89; h->md5_s[2] = 0x98badcfe; h->md5_s[3] = 0x10325476;
  h->sha1_s[0] = 0x67452301; h->sha1_s[1] = 0xEFCDAB89; h->sha1_s[2] = 0x98BADCFE; h->sha1_s[3] = 0x10325476; h->sha1_s[4] = 0xC3D2E1F0;
}

static void
hash_update(hash_t *h, const void *data, size_t n) {
  const uint8_t *p = data;
  h->crc = libdeflate_crc32(h->crc, data, n);
  while(n) {
    size_t fill = (size_t)(h->size & 63), take = 64 - fill;
    if(take > n) take = n;
    memcpy(h->buf + fill, p, take);
    h->size += take;
    p += take;
    n -= take;
    if(((h->size) & 63) == 0) { md5_block(h->md5_s, h->buf); sha1_block(h->sha1_s, h->buf); }
  }
}

static void
hash_final(hash_t *h) {
  uint64_t bits = h->size * 8;
  size_t fill = (size_t)(h->size & 63), total = fill < 56 ? 64 : 128;
  uint8_t tail[128];
  uint32_t m[4];
  memset(tail, 0, sizeof tail);
  memcpy(tail, h->buf, fill);
  tail[fill] = 0x80;
  memcpy(m, h->md5_s, sizeof m);
  for(int i = 0; i < 8; i++) tail[total - 8 + i] = (uint8_t)(bits >> (8 * i));          /* MD5: length little endian */
  for(size_t o = 0; o < total; o += 64) md5_block(m, tail + o);
  for(int i = 0; i < 8; i++) tail[total - 8 + i] = (uint8_t)(bits >> (8 * (7 - i)));    /* SHA-1: big endian */
  for(size_t o = 0; o < total; o += 64) sha1_block(h->sha1_s, tail + o);
  for(int i = 0; i < 4; i++) for(int k = 0; k < 4; k++) h->md5[4 * i + k] = (uint8_t)(m[i] >> (8 * k));
  for(int i = 0; i < 5; i++) for(int k = 0; k < 4; k++) h->sha1[4 * i + k] = (uint8_t)(h->sha1_s[i] >> (24 - 8 * k));
}

static void
hex(const uint8_t *b, size_t n, char *out) {
  for(size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

/* ---------------------------------------------------------------- the drive (CAM pass-through) */

typedef struct { int fd; uint32_t path, target, lun; } pass_t;

static union ccb g_ccb;

static uint32_t
decode_sense(const uint8_t *s) {
  unsigned key, asc, ascq;
  if((s[0] & 0x7E) == 0x72) { key = s[1] & 0x0F; asc = s[2]; ascq = s[3]; }
  else {
    key = s[2] & 0x0F;
    if(s[7] <= 4) { asc = 0; ascq = 0; }
    else { asc = s[12]; ascq = (s[7] == 5) ? 0 : s[13]; }
  }
  return (key << 16) | (asc << 8) | ascq;
}

static int
scsi_cmd(int fd, uint32_t path, uint32_t target, uint32_t lun, const uint8_t *cdb, uint8_t cdb_len, void *data, uint32_t data_len,
         uint32_t dir, uint32_t timeout_ms, uint32_t *sense_out) {
  pthread_mutex_lock(&g_cam_lock);
  union ccb *ccb = &g_ccb;
  memset(ccb, 0, sizeof *ccb);
  ccb->csio.ccb_h.retry_count = 1;
  ccb->csio.ccb_h.func_code = XPT_SCSI_IO;
  ccb->csio.ccb_h.path_id = path;
  ccb->csio.ccb_h.target_id = target;
  ccb->csio.ccb_h.target_lun = lun;
  ccb->csio.ccb_h.flags = dir;
  ccb->csio.ccb_h.timeout = timeout_ms;
  ccb->csio.data_ptr = data;
  ccb->csio.dxfer_len = data_len;
  ccb->csio.sense_len = SSD_FULL_SIZE;
  ccb->csio.cdb_len = cdb_len;
  ccb->csio.tag_action = MSG_SIMPLE_Q_TAG;
  memcpy(ccb->csio.cdb_io.cdb_bytes, cdb, cdb_len);
  int rc = ioctl(fd, CAMIOCOMMAND, ccb);
  int ret = -1;
  if(sense_out) *sense_out = 0;
  if(rc >= 0) {
    if((ccb->csio.ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) ret = 0;
    else if(sense_out) {
      uint32_t status = ccb->csio.ccb_h.status;
      if((status & ~CAM_DEV_QFRZN) == (CAM_AUTOSNS_VALID | CAM_SCSI_STATUS_ERROR) && ccb->csio.scsi_status == 2)
        *sense_out = decode_sense((const uint8_t *)&ccb->csio.sense_data);
    }
  }
  pthread_mutex_unlock(&g_cam_lock);
  return ret;
}

static int
get_passthru(int fd, const char *name, uint32_t unit, char *out_name, uint32_t *out_unit, uint32_t *path, uint32_t *target, uint32_t *lun) {
  pthread_mutex_lock(&g_cam_lock);
  union ccb *ccb = &g_ccb;
  memset(ccb, 0, sizeof *ccb);
  ccb->cgdl.ccb_h.func_code = XPT_GDEVLIST;
  if(name) {
    unsigned i;
    for(i = 0; name[i] && i < sizeof(ccb->cgdl.periph_name) - 1; i++) ccb->cgdl.periph_name[i] = name[i];
    ccb->cgdl.periph_name[i] = 0;
    ccb->cgdl.unit_number = unit;
  }
  int bad = ioctl(fd, CAMGETPASSTHRU, ccb) < 0 || ccb->cgdl.status == CAM_GDEVLIST_ERROR;
  if(!bad) {
    if(out_name) { memcpy(out_name, ccb->cgdl.periph_name, sizeof(ccb->cgdl.periph_name)); out_name[sizeof(ccb->cgdl.periph_name) - 1] = 0; }
    if(out_unit) *out_unit = ccb->cgdl.unit_number;
    if(path) *path = ccb->cgdl.ccb_h.path_id;
    if(target) *target = ccb->cgdl.ccb_h.target_id;
    if(lun) *lun = ccb->cgdl.ccb_h.target_lun;
  }
  pthread_mutex_unlock(&g_cam_lock);
  return bad ? -1 : 0;
}

/* The pass-through device of cd0. -1 when there is none (a console without a drive). */
static int
pass_open(pass_t *p) {
  p->fd = -1;
  int xpt = open("/dev/xpt0", O_RDWR);
  if(xpt < 0) return -1;
  char pass_name[16], dev[64];
  uint32_t unit = 0;
  if(get_passthru(xpt, "cd", 0, pass_name, &unit, NULL, NULL, NULL) != 0) { close(xpt); return -1; }
  close(xpt);
  snprintf(dev, sizeof dev, "/dev/%s%u", pass_name, unit);
  int fd = open(dev, O_RDWR);
  if(fd < 0) return -1;
  if(get_passthru(fd, NULL, 0, NULL, NULL, &p->path, &p->target, &p->lun) != 0) { close(fd); return -1; }
  p->fd = fd;
  return 0;
}

static void
pass_close(pass_t *p) {
  if(p->fd >= 0) close(p->fd);
  p->fd = -1;
}

static void
put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* The drive reads at its own pace unless asked: SET STREAMING to the maximum, else SET CD SPEED. */
static void
request_read_speed(uint32_t end_lba) {
  pass_t p;
  if(pass_open(&p) != 0) return;
  int ok = 0;
  if(end_lba) {
    uint8_t cdb[12] = { 0xB6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 0 };
    uint8_t pd[28];
    memset(pd, 0, sizeof pd);
    put_be32(pd + 8, end_lba);
    put_be32(pd + 12, 0x10000000u);
    put_be32(pd + 16, 1000);
    put_be32(pd + 20, 0x10000000u);
    put_be32(pd + 24, 1000);
    ok = scsi_cmd(p.fd, p.path, p.target, p.lun, cdb, 12, pd, sizeof pd, CAM_DIR_OUT, 10000, NULL) == 0;
  }
  if(!ok) {
    uint8_t cdb[12] = { 0xBB, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0 };
    scsi_cmd(p.fd, p.path, p.target, p.lun, cdb, 12, NULL, 0, CAM_DIR_NONE, 10000, NULL);
  }
  pass_close(&p);
}

static int
wants_disc_structure(uint16_t profile) {
  switch(profile) {
    case 0x10: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x1A: case 0x1B: case 0x2A: case 0x2B: return 1;
    default: return 0;
  }
}

static uint16_t
apply_book_type(uint16_t profile, unsigned book) {
  switch(book) {
    case 1: return 0x12;
    case 2: return 0x11;
    case 3: return 0x13;
    case 9: return 0x1A;
    case 10: return 0x1B;
    case 13: return 0x2A;
    case 14: return 0x2B;
    default: return profile;
  }
}

static int
classify_sense(uint32_t sense) {
  if((sense & 0xFFFF00u) == 0x023000u) return D_INCOMPAT;
  if((sense & 0xFFFF00u) == 0x023A00u) return D_NOT_PRESENT;
  return D_ERROR;
}

static int
read_profile(const pass_t *p, uint16_t *profile, uint32_t *sense) {
  static const uint8_t cdb[12] = { 0x46, 0, 0, 0, 0, 0, 0, 0x08, 0x04, 0, 0, 0 };
  uint8_t *data = calloc(1, 0x804);
  if(!data) return -1;
  int rc = scsi_cmd(p->fd, p->path, p->target, p->lun, cdb, 12, data, 0x804, CAM_DIR_IN, 10000, sense);
  if(rc == 0) *profile = (uint16_t)((data[6] << 8) | data[7]);
  free(data);
  return rc;
}

static int
read_book_type(const pass_t *p, unsigned *book, uint32_t *sense) {
  static const uint8_t cdb[12] = { 0xAD, 0, 0, 0, 0, 0, 0, 0, 0x08, 0x04, 0, 0 };
  uint8_t *data = calloc(1, 0x804);
  if(!data) return -1;
  int rc = scsi_cmd(p->fd, p->path, p->target, p->lun, cdb, 12, data, 0x804, CAM_DIR_IN, 10000, sense);
  if(rc == 0) *book = data[4] >> 4;
  free(data);
  return rc;
}

/* The disc type as ShellCore names it. D_ERROR also when there is no drive (g_drive tells which). */
static int
detect_disc(int *drive) {
  pass_t p;
  uint32_t sense = 0;
  uint16_t profile = 0;
  if(pass_open(&p) != 0) { *drive = 0; return D_ERROR; }
  *drive = 1;
  if(read_profile(&p, &profile, &sense) != 0) {
    for(int left = 3;; ) {                                       /* spin-up: ask again for a few seconds */
      int sensed = classify_sense(sense);
      if(sensed == D_INCOMPAT || sensed == D_NOT_PRESENT) { pass_close(&p); return sensed; }
      if(--left < 0) break;
      sleep(1);
      if(read_profile(&p, &profile, &sense) == 0) goto ready;
    }
    pass_close(&p);
    return D_TIMEOUT;
  }
ready:
  if(wants_disc_structure(profile)) {
    unsigned book = 0;
    if(read_book_type(&p, &book, &sense) != 0) { int s = classify_sense(sense); pass_close(&p); return s; }
    profile = apply_book_type(profile, book);
  }
  pass_close(&p);
  return (int)profile;
}

/* ---------------------------------------------------------------- the title ID of the mounted disc */

static int
name_is(const char *name, const char *wanted) {
  size_t i = 0;
  for(; wanted[i]; i++) if(toupper((unsigned char)name[i]) != toupper((unsigned char)wanted[i])) return 0;
  return name[i] == 0 || name[i] == ';';
}

static int
mount_has_files(void) {
  DIR *d = opendir(DISC_MNT);
  if(!d) return 0;
  struct dirent *e;
  int has = 0;
  while((e = readdir(d))) {
    if(e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
    has = 1;
    break;
  }
  closedir(d);
  return has;
}

static int
scan_mount(int *has_files, int *has_psx, char *cnf, size_t cnf_len) {
  *has_files = *has_psx = 0;
  cnf[0] = 0;
  DIR *d = opendir(DISC_MNT);
  if(!d) return -1;
  struct dirent *e;
  while((e = readdir(d))) {
    const char *nm = e->d_name;
    if(nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2]))) continue;
    *has_files = 1;
    if(name_is(nm, "PSX.EXE")) *has_psx = 1;
    if(!cnf[0] && name_is(nm, "SYSTEM.CNF")) snprintf(cnf, cnf_len, "%s/%s", DISC_MNT, nm);
  }
  closedir(d);
  return 0;
}

/* 1 file loaded, 0 present but unusable, -1 not there yet. */
static int
load_disc_file(const char *rel, off_t min_size, off_t max_size, unsigned char **out, size_t *n) {
  char path[512];
  struct stat st;
  *out = NULL;
  *n = 0;
  snprintf(path, sizeof path, DISC_MNT "/%s", rel);
  if(stat(path, &st) != 0) return -1;
  if(!S_ISREG(st.st_mode) || st.st_size < min_size || st.st_size > max_size) return 0;
  unsigned char *b = malloc((size_t)st.st_size);
  if(!b) return 0;
  int fd = open(path, O_RDONLY);
  if(fd < 0) { free(b); return 0; }
  size_t got = 0;
  while(got < (size_t)st.st_size) {
    ssize_t r = read(fd, b + got, (size_t)st.st_size - got);
    if(r <= 0) break;
    got += (size_t)r;
  }
  close(fd);
  if(got != (size_t)st.st_size) { free(b); return 0; }
  *out = b;
  *n = got;
  return 1;
}

/* BOOT = cdrom0:\SLUS_209.46;1 -- the key exactly (BOOT is not BOOT2), the case of the key is free. */
static int
cnf_boot_serial(const char *path, const char *key, char *dest, size_t dest_len, const char *generic) {
  dest[0] = 0;
  char *b = read_small(path, 8192);
  if(!b) return 0;
  int found = 0;
  for(char *line = b; line && *line && !found;) {
    char *nl = strchr(line, '\n');
    if(nl) *nl = 0;
    char *eq = strchr(line, '=');
    if(eq) {
      char *k = line;
      while(*k == ' ' || *k == '\t') k++;
      char *ke = eq;
      while(ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
      size_t kl = (size_t)(ke - k);
      if(kl == strlen(key) && strncasecmp(k, key, kl) == 0) {
        sanitize_serial(eq + 1, dest, dest_len);
        if(!dest[0]) set_serial(dest, dest_len, generic);
        found = 1;
      }
    }
    line = nl ? nl + 1 : NULL;
  }
  free(b);
  return found;
}

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* TITLE_ID of a PARAM.SFO (the entry's maximum length must be max_len, as in the original check). */
static int
sfo_title(const unsigned char *b, size_t n, uint32_t max_len, size_t copy_n, char *dest, size_t dest_len) {
  dest[0] = 0;
  if(n < 20 || memcmp(b, "\0PSF", 4) != 0) return 0;
  uint32_t key_off = le32(b + 8), data_off = le32(b + 12), entries = le32(b + 16);
  if(entries > 256 || 20 + (size_t)entries * 16 > n) return 0;
  for(uint32_t e = 0; e < entries; e++) {
    size_t ent = 20 + (size_t)e * 16;
    size_t kpos = (size_t)key_off + le16(b + ent);
    if(kpos >= n || n - kpos < 9 || memcmp(b + kpos, "TITLE_ID", 9) != 0) continue;
    uint32_t len = le32(b + ent + 4), maxl = le32(b + ent + 8);
    if(maxl != max_len || len == 0 || len > max_len) return 0;
    size_t val = (size_t)data_off + le32(b + ent + 12);
    if(val >= n) return 0;
    size_t take = n - val;
    if(take > copy_n) take = copy_n;
    if(take >= dest_len) take = dest_len - 1;
    size_t i;
    for(i = 0; i < take && b[val + i]; i++) dest[i] = (char)b[val + i];
    dest[i] = 0;
    if(!dest[0]) return 0;
    hyphenate_serial(dest, dest_len);
    return dest[0] != 0;
  }
  return 0;
}

/* PS5: the first PS5GD content of bd/param.json, bytes 7..16 of its content id. */
static int
json_ps5_title(const unsigned char *b, size_t n, char *dest, size_t dest_len) {
  dest[0] = 0;
  if(n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { b += 3; n -= 3; }
  cJSON *doc = cJSON_ParseWithLength((const char *)b, n);
  const cJSON *disc = cJSON_IsObject(doc) ? cJSON_GetObjectItem(doc, "disc") : NULL;
  const cJSON *d;
  cJSON_ArrayForEach(d, disc) {
    const cJSON *c, *contents = cJSON_GetObjectItem(d, "contents");
    cJSON_ArrayForEach(c, contents) {
      const cJSON *type = cJSON_GetObjectItem(c, "contentType"), *cid = cJSON_GetObjectItem(c, "contentId");
      if(cJSON_IsString(type) && !strncmp(type->valuestring, "PS5GD", 5)) {
        if(cJSON_IsString(cid) && strlen(cid->valuestring) >= 16 && dest_len > 10) {
          memcpy(dest, cid->valuestring + 7, 9);
          dest[9] = 0;
          hyphenate_serial(dest, dest_len);
        }
        cJSON_Delete(doc);
        return dest[0] != 0;
      }
    }
  }
  cJSON_Delete(doc);
  return 0;
}

static int
app_dir_title(char *dest, size_t dest_len) {
  struct stat st;
  if(stat(DISC_MNT "/app", &st) != 0) return -1;
  if(!S_ISDIR(st.st_mode)) return 0;
  DIR *d = opendir(DISC_MNT "/app");
  if(!d) return 0;
  struct dirent *e;
  while((e = readdir(d))) {
    if(e->d_fileno == 0 || e->d_type != DT_DIR || strlen(e->d_name) != 9) continue;
    int ok = 1;
    for(int i = 0; i < 9; i++) {
      unsigned char c = (unsigned char)e->d_name[i];
      if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) { ok = 0; break; }
    }
    if(!ok || dest_len <= 10) continue;
    memcpy(dest, e->d_name, 9);
    dest[9] = 0;
    hyphenate_serial(dest, dest_len);
    closedir(d);
    return dest[0] != 0;
  }
  closedir(d);
  return 0;
}

static void
read_pvd_serial(char *dest, size_t dest_len) {
  dest[0] = 0;
  int fd = open(DISC_DEV, O_RDONLY);
  if(fd < 0) return;
  unsigned char sec[2048];
  if(pread(fd, sec, sizeof sec, (off_t)16 * 2048) == (ssize_t)sizeof sec) {
    char vol[33];
    memcpy(vol, sec + 40, 32);
    vol[32] = 0;
    sanitize_serial(vol, dest, dest_len);
  }
  close(fd);
}

/* 1 and the title in dest; 0 no title for this profile; -1 while the files are still being mounted. *generic set when the
   title could not be read (GENERIC_PSn). */
static int
disc_title_id(int type, char *dest, size_t dest_len, int *generic) {
  dest[0] = 0;
  *generic = 0;
  int has_files = 0, has_psx = 0;
  char cnf[512];
  unsigned char *buf = NULL;
  size_t n = 0;
  switch(type) {
    case D_PS1_CD: {
      if(scan_mount(&has_files, &has_psx, cnf, sizeof cnf) != 0 || !has_files) return -1;
      if(!cnf[0] && !has_psx) return -1;                              /* wait for the boot file */
      char pvd[32];
      read_pvd_serial(pvd, sizeof pvd);
      if(cnf[0] && cnf_boot_serial(cnf, "BOOT", dest, dest_len, "GENERIC_PS1")) {
        if(!strcmp(dest, "PSXEXE") || !strcmp(dest, "PSX-EXE")) set_serial(dest, dest_len, pvd[0] ? pvd : "GENERIC_PS1");
      } else {
        if(!has_psx) return -1;
        set_serial(dest, dest_len, pvd[0] ? pvd : "GENERIC_PS1");
      }
      *generic = !strncmp(dest, "GENERIC_", 8);
      return 1;
    }
    case D_PS2_CD:
    case D_PS2_DVD:
      if(scan_mount(&has_files, &has_psx, cnf, sizeof cnf) != 0 || !has_files) return -1;
      if(!cnf[0] || !cnf_boot_serial(cnf, "BOOT2", dest, dest_len, "GENERIC_PS2")) return -1;
      *generic = !strncmp(dest, "GENERIC_", 8);
      return 1;
    case D_PS3_DVD:
    case D_PS3_BD: {
      int loaded = load_disc_file("PS3_GAME/PARAM.SFO", 1, 1 << 20, &buf, &n);
      int ok = loaded == 1 && sfo_title(buf, n, 16, 9, dest, dest_len);
      free(buf);
      if(ok) return 1;
      if(loaded < 0 && !mount_has_files()) return -1;
      set_serial(dest, dest_len, "GENERIC_PS3");
      *generic = 1;
      return 1;
    }
    case D_PS4_BD: {
      int loaded = load_disc_file("bd/param.sfo", 0x800, 1 << 20, &buf, &n);
      int ok = loaded == 1 && n > 0x800 && sfo_title(buf + 0x800, n - 0x800, 12, 11, dest, dest_len);
      free(buf);
      if(!ok) {
        int dir = app_dir_title(dest, dest_len);
        if(dir == 1) ok = 1;
        else if(loaded < 0 && dir < 0 && !mount_has_files()) return -1;
      }
      if(!ok) { set_serial(dest, dest_len, "GENERIC_PS4"); *generic = 1; }
      return 1;
    }
    case D_PPR_BD: {
      int loaded = load_disc_file("bd/param.json", 0x800, 0x800 + 0x20000, &buf, &n);
      int ok = loaded == 1 && n > 0x800 && json_ps5_title(buf + 0x800, n - 0x800, dest, dest_len);
      free(buf);
      if(!ok) {
        int dir = app_dir_title(dest, dest_len);
        if(dir == 1) ok = 1;
        else if(loaded < 0 && dir < 0 && !mount_has_files()) return -1;
      }
      if(!ok) { set_serial(dest, dest_len, "GENERIC_PS5"); *generic = 1; }
      return 1;
    }
    default:
      return 0;
  }
}

/* ---------------------------------------------------------------- the Redump database (optional) */

typedef struct { unsigned char *buf; size_t len; sqlro_db_t db; } rdb_t;

/* 1 loaded, 0 there is no file, -1 there is one and it is no use. Freed with rdb_close. */
static int
rdb_open(rdb_t *r) {
  memset(r, 0, sizeof *r);
  struct stat st;
  if(stat(REDUMP_PATH, &st) != 0) return errno == ENOENT ? 0 : -1;
  if(!S_ISREG(st.st_mode) || st.st_size < 512 || (unsigned long long)st.st_size > REDUMP_MAX) return -1;
  r->buf = malloc((size_t)st.st_size);
  if(!r->buf) return -1;
  int fd = open(REDUMP_PATH, O_RDONLY);
  if(fd < 0) { free(r->buf); r->buf = NULL; return -1; }
  size_t got = 0;
  while(got < (size_t)st.st_size) {
    ssize_t k = read(fd, r->buf + got, (size_t)st.st_size - got);
    if(k < 0 && errno == EINTR) continue;
    if(k <= 0) break;
    got += (size_t)k;
  }
  close(fd);
  r->len = got;
  if(got != (size_t)st.st_size || sqlro_open_mem(&r->db, r->buf, r->len) != 0) { free(r->buf); r->buf = NULL; return -1; }
  return 1;
}

static void
rdb_close(rdb_t *r) { free(r->buf); r->buf = NULL; }

typedef struct {
  const char *serial;
  int c_serial, c_name;
  char names[8][256];
  int n;
} names_ctx_t;

static int
names_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  names_ctx_t *c = ctx;
  (void)t;
  char s[48];
  if(sqlro_text(r, c->c_serial, s, sizeof s) != 0 || strcmp(s, c->serial) != 0) return 0;
  if(c->n < 8 && sqlro_text(r, c->c_name, c->names[c->n], sizeof c->names[0]) == 0) c->n++;
  return 0;
}

/* The Redump names of a serial (up to 8). */
static int
rdb_names(rdb_t *r, const char *serial, char names[8][256]) {
  sqlro_table_t *t = malloc(sizeof *t);
  if(!t) return 0;
  names_ctx_t c = { serial, -1, -1, { { 0 } }, 0 };
  if(sqlro_find_table(&r->db, "serials", t) == 0) {
    c.c_serial = sqlro_col(t, "serial");
    c.c_name = sqlro_col(t, "name");
    if(c.c_serial >= 0 && c.c_name >= 0) sqlro_scan(&r->db, t, names_row, &c);
  }
  free(t);
  memcpy(names, c.names, sizeof c.names);
  return c.n;
}

typedef struct {
  const hash_t *h;
  uint64_t size;
  int iso_only;
  int c_sha1, c_size, c_crc, c_md5, c_name, c_kind;
  char name[256];
  int hits;                                 /* rows by size only (name_for_size) */
  int by_size;
} image_ctx_t;

static int
image_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  image_ctx_t *c = ctx;
  (void)t;
  int64_t size = -1, crc = -1;
  char kind[16];
  if(sqlro_int(r, c->c_size, &size) != 0 || (uint64_t)size != c->size) return 0;
  if(sqlro_text(r, c->c_kind, kind, sizeof kind) != 0 || strcmp(kind, "iso") != 0) return 0;
  if(c->by_size) {
    if(c->hits == 0) sqlro_text(r, c->c_name, c->name, sizeof c->name);
    c->hits++;
    return c->hits > 1;
  }
  if(sqlro_int(r, c->c_crc, &crc) != 0 || (uint32_t)crc != c->h->crc) return 0;
  if(r->kind[c->c_sha1] != SQLRO_BLOB || r->len[c->c_sha1] != 20 || memcmp(r->data[c->c_sha1], c->h->sha1, 20) != 0) return 0;
  if(r->kind[c->c_md5] != SQLRO_BLOB || r->len[c->c_md5] != 16 || memcmp(r->data[c->c_md5], c->h->md5, 16) != 0) return 0;
  sqlro_text(r, c->c_name, c->name, sizeof c->name);
  c->hits = 1;
  return 1;
}

/* The Redump name of an image with exactly these checksums ("" when none). */
static int
rdb_verify(rdb_t *r, const hash_t *h, char *name, size_t name_len) {
  name[0] = 0;
  sqlro_table_t *t = malloc(sizeof *t);
  if(!t) return 0;
  image_ctx_t c;
  memset(&c, 0, sizeof c);
  c.h = h;
  c.size = h->size;
  if(sqlro_find_table(&r->db, "images", t) == 0) {
    c.c_sha1 = sqlro_col(t, "sha1"); c.c_size = sqlro_col(t, "size"); c.c_crc = sqlro_col(t, "crc");
    c.c_md5 = sqlro_col(t, "md5"); c.c_name = sqlro_col(t, "name"); c.c_kind = sqlro_col(t, "kind");
    if(c.c_sha1 >= 0 && c.c_size >= 0 && c.c_crc >= 0 && c.c_md5 >= 0 && c.c_name >= 0 && c.c_kind >= 0) sqlro_scan(&r->db, t, image_row, &c);
  }
  free(t);
  if(c.hits == 1) snprintf(name, name_len, "%s", c.name);
  return name[0] != 0;
}

/* The one Redump image of this size, for a disc whose serial could not be read. */
static void
rdb_name_for_size(rdb_t *r, uint64_t size, char *name, size_t name_len) {
  name[0] = 0;
  sqlro_table_t *t = malloc(sizeof *t);
  if(!t) return;
  image_ctx_t c;
  memset(&c, 0, sizeof c);
  c.size = size;
  c.by_size = 1;
  if(sqlro_find_table(&r->db, "images", t) == 0) {
    c.c_size = sqlro_col(t, "size"); c.c_name = sqlro_col(t, "name"); c.c_kind = sqlro_col(t, "kind");
    if(c.c_size >= 0 && c.c_name >= 0 && c.c_kind >= 0) sqlro_scan(&r->db, t, image_row, &c);
  }
  free(t);
  if(c.hits == 1) snprintf(name, name_len, "%s", c.name);
}

/* ---------------------------------------------------------------- the monitor */

static int
image_bytes_of_drive(uint64_t *out) {
  int dev = open(DISC_DEV, O_RDONLY);
  if(dev < 0) return -1;
  off_t media = -1;
  int rc = ioctl(dev, DIOCGMEDIASIZE, &media);
  close(dev);
  if(rc < 0 || media <= 0) return -1;
  *out = (uint64_t)media;
  return 0;
}

static void
disc_forget_locked(void) {
  g_serial[0] = 0;
  g_rname[0] = 0;
  g_rnames = 0;
  g_tstate = TS_NONE;
  g_image_bytes = 0;
}

static int dump_start_locked(char *err, size_t en, int automatic);

static void
title_found(int type, const char *serial, int generic) {
  rdb_t r;
  char names[8][256];
  int nn = 0;
  names[0][0] = 0;
  int have = rdb_open(&r);
  if(have == 1) {
    if(!generic) nn = rdb_names(&r, serial, names);
    rdb_close(&r);
  }
  uint64_t bytes = 0;
  if(type_is_dumpable(type) && image_bytes_of_drive(&bytes) != 0) bytes = 0;
  pthread_mutex_lock(&g_lock);
  set_serial(g_serial, sizeof g_serial, serial);
  g_tstate = generic ? TS_GENERIC : TS_OK;
  g_rnames = nn;
  snprintf(g_rname, sizeof g_rname, "%s", nn == 1 ? names[0] : "");
  g_image_bytes = bytes;
  if(g_rname[0]) event_locked("%s: %s (%s)", type_name(type), serial, g_rname);
  else event_locked("%s: %s", type_name(type), serial);
  if(g_auto && type_is_dumpable(type) && strcmp(g_auto_serial, serial) != 0) {
    char err[200];
    snprintf(g_auto_serial, sizeof g_auto_serial, "%s", serial);
    if(dump_start_locked(err, sizeof err, 1) != 200) event_locked("Automatisches Sichern: %s", err);
  }
  pthread_mutex_unlock(&g_lock);
}

static void *
poll_thread(void *arg) {
  (void)arg;
  int last = D_NOT_PRESENT, waiting = 0, shown_set = 0;
  char shown[32] = "";
  for(;;) {
    pthread_mutex_lock(&g_lock);
    int stop = g_poll_stop || !g_enabled;
    int busy = g_job.state == JOB_RUNNING || g_job.state == JOB_VERIFY;
    if(stop) { g_poll_up = 0; g_poll_stop = 0; pthread_mutex_unlock(&g_lock); return NULL; }
    pthread_mutex_unlock(&g_lock);
    if(busy) { usleep(POLL_US); continue; }

    int drive = 1;
    int type = detect_disc(&drive);
    pthread_mutex_lock(&g_lock);
    g_drive = drive;
    pthread_mutex_unlock(&g_lock);
    if(!drive) { usleep(10 * 1000 * 1000); continue; }
    if(type == D_TIMEOUT || type == D_ERROR) { usleep(POLL_US); continue; }

    if(type != last) {
      waiting = 0;
      pthread_mutex_lock(&g_lock);
      g_type = type;
      disc_forget_locked();
      pthread_mutex_unlock(&g_lock);
      if(type == D_NOT_PRESENT || type == D_NO_PROFILE) {
        pthread_mutex_lock(&g_lock);
        event_locked(type == D_NOT_PRESENT ? "Disc entnommen." : "Kein Profil (Fach leer).");
        g_auto_serial[0] = 0;
        pthread_mutex_unlock(&g_lock);
        shown[0] = 0;
        shown_set = 0;
      } else {
        char serial[32];
        int generic = 0, got = type_has_title(type) ? disc_title_id(type, serial, sizeof serial, &generic) : 0;
        if(got == 1) {
          title_found(type, serial, generic);
          set_serial(shown, sizeof shown, serial);
          shown_set = 1;
        } else {
          pthread_mutex_lock(&g_lock);
          event_locked("%s", type_name(type));
          pthread_mutex_unlock(&g_lock);
          if(got < 0) waiting = 1;
        }
      }
      last = type;
    } else if(waiting) {
      char serial[32];
      int generic = 0, got = disc_title_id(type, serial, sizeof serial, &generic);
      if(got == 1) {
        waiting = 0;
        if(!shown_set || strcmp(serial, shown) != 0) { title_found(type, serial, generic); set_serial(shown, sizeof shown, serial); shown_set = 1; }
      } else if(got == 0) {
        waiting = 0;
      }
    } else if(type_has_title(type) && !mount_has_files()) {
      waiting = 1;                                  /* the same profile, a new disc: the mount drops first */
    }
    pthread_mutex_lock(&g_lock);
    if(g_tstate == TS_NONE && waiting && type_has_title(type)) g_tstate = TS_WAIT;
    pthread_mutex_unlock(&g_lock);
    usleep(POLL_US);
  }
}

static void
poll_start_locked(void) {
  if(g_poll_up) { g_poll_stop = 0; return; }
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  g_poll_stop = 0;
  if(pthread_create(&th, &at, poll_thread, NULL) == 0) g_poll_up = 1;
  else PS5TM_WARN("disc_thread", "Laufwerk: Der Beobachter ließ sich nicht starten.");
  pthread_attr_destroy(&at);
}

/* ---------------------------------------------------------------- the dump */

static int
dump_name_ok(const char *name) {
  if(!name || !name[0] || name[0] == '.' || strlen(name) > 200) return 0;
  for(const unsigned char *p = (const unsigned char *)name; *p; p++)
    if(*p < 0x20 || *p == '/' || *p == '\\' || *p == '"') return 0;
  return 1;
}

static int
iso_complete(const char *dir, const char *stem, uint64_t size) {
  char p[600];
  struct stat st;
  snprintf(p, sizeof p, "%s/%s.iso", dir, stem);
  return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && (size == 0 || (uint64_t)st.st_size == size);
}

/* An image of this name is already there, in the app's folder or where the original project put it (/data). */
static int
have_image(const char *stem, uint64_t size) { return iso_complete(DISC_DIR, stem, size) || iso_complete("/data", stem, size); }

static void
sidecar_path(char *out, size_t n, const char *stem) { snprintf(out, n, DISC_DIR "/%s.hash.json", stem); }

static void
sidecar_write(const char *stem, const char *serial, const hash_t *h, int verified, const char *match) {
  char m5[33], s1[41], path[600], iso[32];
  hex(h->md5, 16, m5);
  hex(h->sha1, 20, s1);
  snprintf(iso, sizeof iso, "%08x", h->crc);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "serial", serial);
  cJSON_AddNumberToObject(o, "size", (double)h->size);
  cJSON_AddStringToObject(o, "crc32", iso);
  cJSON_AddStringToObject(o, "md5", m5);
  cJSON_AddStringToObject(o, "sha1", s1);
  cJSON_AddBoolToObject(o, "verified", verified);
  cJSON_AddStringToObject(o, "redump", match ? match : "");
  cJSON_AddNumberToObject(o, "time", (double)time(NULL));
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if(s) {
    sidecar_path(path, sizeof path, stem);
    write_atomic(path, s, strlen(s));
    free(s);
  }
}

static int
rename_dump(const char *from, const char *to) {
  if(!strcmp(from, to)) return 0;
  char a[600], b[600];
  snprintf(a, sizeof a, "%s/%s.iso", DISC_DIR, from);
  snprintf(b, sizeof b, "%s/%s.iso", DISC_DIR, to);
  struct stat st;
  if(stat(b, &st) == 0) return -1;                                 /* never over another image */
  if(rename(a, b) != 0) return -1;
  sidecar_path(a, sizeof a, from);
  sidecar_path(b, sizeof b, to);
  rename(a, b);
  return 0;
}

typedef struct { int type; char serial[32]; int automatic; } dump_arg_t;

static void
job_fail(const char *msg) {
  pthread_mutex_lock(&g_lock);
  g_job.state = g_job.cancel ? JOB_CANCELLED : JOB_FAILED;
  snprintf(g_job.err, sizeof g_job.err, "%s", g_job.cancel ? "Abgebrochen." : msg);
  event_locked("Sichern: %s", g_job.err);
  pthread_mutex_unlock(&g_lock);
}

static void *
dump_thread(void *arg) {
  dump_arg_t *a = arg;
  char stem[300], part[640], final_path[640];
  uint64_t media_u = 0, done = 0, t0 = ps5tm_mono_ms();
  hash_t *h = calloc(1, sizeof *h);
  unsigned char *buf = malloc(DUMP_CHUNK);
  int dev = -1, out = -1, rc = -1;
  const int generic = !strncmp(a->serial, "GENERIC_", 8);

  if(!h || !buf) { job_fail("Kein Speicher."); goto end; }
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(DISC_DIR, 0777);

  /* the name: the Redump name when the serial has exactly one, else the serial */
  {
    rdb_t r;
    char names[8][256];
    int nn = 0;
    snprintf(stem, sizeof stem, "%s", a->serial);
    int have = rdb_open(&r);
    if(have == 1) {
      if(!generic) nn = rdb_names(&r, a->serial, names);
      if(image_bytes_of_drive(&media_u) != 0) media_u = 0;
      /* already on disk under any of the names this disc can carry? */
      int dumped = 0;
      if(have_image(a->serial, media_u)) dumped = 1;
      for(int i = 0; i < nn && !dumped; i++) if(dump_name_ok(names[i]) && have_image(names[i], media_u)) dumped = 1;
      if(!dumped && generic && media_u) {
        char sole[256];
        rdb_name_for_size(&r, media_u, sole, sizeof sole);
        if(sole[0] && dump_name_ok(sole) && have_image(sole, media_u)) dumped = 1;
      }
      if(nn == 1 && dump_name_ok(names[0])) snprintf(stem, sizeof stem, "%s", names[0]);
      rdb_close(&r);
      if(dumped) {
        pthread_mutex_lock(&g_lock);
        g_job.state = JOB_DONE;
        g_job.verified = 0;
        snprintf(g_job.note, sizeof g_job.note, "Dieses Abbild liegt schon im Ordner.");
        event_locked("Sichern: %s liegt schon im Ordner.", stem);
        pthread_mutex_unlock(&g_lock);
        goto end;
      }
    } else if(image_bytes_of_drive(&media_u) != 0) {
      media_u = 0;
    }
    if(have != 1 && media_u && have_image(a->serial, media_u)) {
      pthread_mutex_lock(&g_lock);
      g_job.state = JOB_DONE;
      snprintf(g_job.note, sizeof g_job.note, "Dieses Abbild liegt schon im Ordner.");
      event_locked("Sichern: %s liegt schon im Ordner.", a->serial);
      pthread_mutex_unlock(&g_lock);
      goto end;
    }
  }
  if(!media_u) { job_fail("Die Größe der Disc ließ sich nicht lesen."); goto end; }

  /* room for it */
  {
    struct statvfs sv;
    if(statvfs(DISC_DIR, &sv) == 0 && (int64_t)((uint64_t)sv.f_bavail * sv.f_frsize) < (int64_t)media_u + DUMP_RESERVE) {
      job_fail("Zu wenig Platz im Ordner für das Abbild.");
      goto end;
    }
  }
  /* an unfinished image of this name is replaced, a finished one of another disc is not */
  if(iso_complete(DISC_DIR, stem, 0)) {
    for(int i = 2; i < 10; i++) {
      char alt[320];
      snprintf(alt, sizeof alt, "%s (%d)", stem, i);
      if(!iso_complete(DISC_DIR, alt, 0)) { snprintf(stem, sizeof stem, "%s", alt); break; }
    }
  }
  snprintf(part, sizeof part, "%s/%s.iso.part", DISC_DIR, stem);
  snprintf(final_path, sizeof final_path, "%s/%s.iso", DISC_DIR, stem);

  dev = open(DISC_DEV, O_RDONLY);
  if(dev < 0) { job_fail("Das Laufwerk ließ sich nicht öffnen."); goto end; }
  unsigned sector = 0;
  if(ioctl(dev, DIOCGSECTORSIZE, &sector) < 0) sector = 2048;
  size_t chunk = DUMP_CHUNK;
  if(sector > 0 && sector <= DUMP_CHUNK && (chunk % sector) != 0) chunk -= chunk % sector;
  if(sector > 0 && media_u / sector > 1) request_read_speed((uint32_t)(media_u / sector - 1));
  else request_read_speed(0);

  out = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(out < 0) { job_fail("Die Abbilddatei ließ sich nicht anlegen."); goto end; }

  pthread_mutex_lock(&g_lock);
  snprintf(g_job.stem, sizeof g_job.stem, "%s", stem);
  g_job.total = media_u;
  g_job.done = 0;
  g_job.t0 = t0;
  g_job.rate_ms = t0;
  g_job.rate_bytes = 0;
  g_job.rate = 0;
  event_locked("Sichern beginnt: %s (%llu MiB)", stem, (unsigned long long)(media_u >> 20));
  pthread_mutex_unlock(&g_lock);
  ps5tm_powerguard_hold();

  hash_init(h);
  rc = 0;
  while(done < media_u) {
    size_t want = chunk;
    if((uint64_t)want > media_u - done) want = (size_t)(media_u - done);
    ssize_t got;
    do { got = read(dev, buf, want); } while(got < 0 && errno == EINTR);
    if(got <= 0) { rc = -1; break; }
    const unsigned char *p = buf;
    size_t left = (size_t)got;
    while(left) {
      ssize_t w = write(out, p, left);
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { rc = -2; break; }
      p += w;
      left -= (size_t)w;
    }
    if(rc) break;
    hash_update(h, buf, (size_t)got);
    done += (uint64_t)got;
    uint64_t now = ps5tm_mono_ms();
    pthread_mutex_lock(&g_lock);
    g_job.done = done;
    if(now - g_job.rate_ms >= 2000) {
      double r = (double)(done - g_job.rate_bytes) * 1000.0 / (double)(now - g_job.rate_ms);
      g_job.rate = g_job.rate > 0 ? 0.7 * g_job.rate + 0.3 * r : r;
      g_job.rate_ms = now;
      g_job.rate_bytes = done;
    }
    int cancel = g_job.cancel;
    pthread_mutex_unlock(&g_lock);
    if(cancel) { rc = -3; break; }
  }
  ps5tm_powerguard_release();

  if(rc == 0 && fsync(out) != 0) rc = -2;
  if(close(out) != 0 && rc == 0) rc = -2;
  out = -1;
  close(dev);
  dev = -1;
  if(rc != 0) {
    unlink(part);
    job_fail(rc == -3 ? "Abgebrochen." : rc == -2 ? "Das Schreiben ist gescheitert (Platte voll oder Fehler)." : "Das Lesen der Disc ist gescheitert (Fehler am Laufwerk).");
    goto end;
  }
  hash_final(h);

  /* check against Redump, then the final name */
  pthread_mutex_lock(&g_lock);
  g_job.state = JOB_VERIFY;
  pthread_mutex_unlock(&g_lock);
  {
    char match[256] = "";
    int verified = 0;
    rdb_t r;
    if(rdb_open(&r) == 1) {
      verified = rdb_verify(&r, h, match, sizeof match);
      rdb_close(&r);
    }
    const char *final_stem = stem;
    if(rename(part, final_path) != 0) { unlink(part); job_fail("Die fertige Datei ließ sich nicht umbenennen."); goto end; }
    sidecar_write(stem, a->serial, h, verified, match);
    if(verified && dump_name_ok(match) && rename_dump(stem, match) == 0) final_stem = match;
    else if(!verified && !generic && strcmp(stem, a->serial) != 0 && rename_dump(stem, a->serial) == 0) final_stem = a->serial;
    char m5[33], s1[41];
    hex(h->md5, 16, m5);
    hex(h->sha1, 20, s1);
    uint64_t dur = ps5tm_mono_ms() - t0;
    pthread_mutex_lock(&g_lock);
    g_job.state = JOB_DONE;
    g_job.verified = verified;
    snprintf(g_job.final_stem, sizeof g_job.final_stem, "%s", final_stem);
    snprintf(g_job.note, sizeof g_job.note, "%s", verified ? "Stimmt mit Redump überein." : "Nicht in der Redump-Liste (oder keine Liste auf der Konsole).");
    event_locked("Sichern fertig: %s (%s)", final_stem, verified ? "Redump geprüft" : "ohne Redump-Treffer");
    pthread_mutex_unlock(&g_lock);
    char detail[200];
    snprintf(detail, sizeof detail, "CRC-32 %08x, MD5 %s", h->crc, m5);
    (void)s1;
    ps5tm_hist_add("disc", final_stem, detail, "done", NULL, h->size, dur);
  }
end:
  if(out >= 0) { close(out); unlink(part); }
  if(dev >= 0) close(dev);
  free(buf);
  free(h);
  free(a);
  return NULL;
}

/* Starts the dump of the disc in the drive. 200, else an HTTP status with the reason in err. g_lock held. */
static int
dump_start_locked(char *err, size_t en, int automatic) {
  if(g_job.state == JOB_RUNNING || g_job.state == JOB_VERIFY) { snprintf(err, en, "Es läuft schon ein Abbild."); return 409; }
  if(!type_is_dumpable(g_type)) { snprintf(err, en, "Diese Disc lässt sich nicht sichern (nur PS2-DVD und PS3-Discs)."); return 409; }
  if(g_tstate != TS_OK && g_tstate != TS_GENERIC) { snprintf(err, en, "Die Disc ist noch nicht erkannt (Titelkennung fehlt)."); return 409; }
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_filemgr_busy()) { snprintf(err, en, "Ein anderer Dateiauftrag läuft gerade."); return 409; }
  dump_arg_t *a = calloc(1, sizeof *a);
  if(!a) { snprintf(err, en, "Kein Speicher."); return 500; }
  a->type = g_type;
  a->automatic = automatic;
  snprintf(a->serial, sizeof a->serial, "%s", g_serial);
  memset(&g_job, 0, sizeof g_job);
  g_job.state = JOB_RUNNING;
  snprintf(g_job.serial, sizeof g_job.serial, "%s", g_serial);
  snprintf(g_job.stem, sizeof g_job.stem, "%s", g_serial);
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&th, &at, dump_thread, a);
  pthread_attr_destroy(&at);
  if(rc != 0) { free(a); g_job.state = JOB_FAILED; snprintf(err, en, "Der Auftrag ließ sich nicht starten."); return 500; }
  snprintf(err, en, "Das Abbild wird gesichert.");
  return 200;
}

/* ---------------------------------------------------------------- the page's side */

static int
list_dumps(cJSON *arr) {
  DIR *d = opendir(DISC_DIR);
  if(!d) return 0;
  struct dirent *e;
  int n = 0;
  while((e = readdir(d))) {
    size_t l = strlen(e->d_name);
    if(l < 5 || strcmp(e->d_name + l - 4, ".iso") != 0) continue;
    char stem[300], path[700];
    snprintf(stem, sizeof stem, "%.*s", (int)(l - 4), e->d_name);
    snprintf(path, sizeof path, DISC_DIR "/%s", e->d_name);
    struct stat st;
    if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", stem);
    cJSON_AddNumberToObject(o, "size", (double)st.st_size);
    cJSON_AddNumberToObject(o, "mtime", (double)st.st_mtime);
    sidecar_path(path, sizeof path, stem);
    char *b = read_small(path, 8192);
    cJSON *j = b ? cJSON_Parse(b) : NULL;
    free(b);
    if(j) {
      const cJSON *x;
      if(cJSON_IsString(x = cJSON_GetObjectItem(j, "serial"))) cJSON_AddStringToObject(o, "serial", x->valuestring);
      if(cJSON_IsString(x = cJSON_GetObjectItem(j, "crc32"))) cJSON_AddStringToObject(o, "crc32", x->valuestring);
      if(cJSON_IsString(x = cJSON_GetObjectItem(j, "md5"))) cJSON_AddStringToObject(o, "md5", x->valuestring);
      if(cJSON_IsString(x = cJSON_GetObjectItem(j, "sha1"))) cJSON_AddStringToObject(o, "sha1", x->valuestring);
      cJSON_AddBoolToObject(o, "verified", cJSON_IsTrue(cJSON_GetObjectItem(j, "verified")));
      cJSON_Delete(j);
    }
    cJSON_AddItemToArray(arr, o);
    n++;
  }
  closedir(d);
  return n;
}

cJSON *
ps5tm_disc_json(void) {
  pthread_mutex_lock(&g_lock);
  settings_load_locked();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "enabled", g_enabled);
  cJSON_AddBoolToObject(o, "auto_dump", g_auto);
  cJSON_AddStringToObject(o, "drive", g_drive == 1 ? "ok" : g_drive == 0 ? "none" : "unknown");
  int present = g_enabled && g_drive == 1 && g_type != D_NOT_PRESENT && g_type != D_NO_PROFILE;
  cJSON_AddBoolToObject(o, "present", present);
  cJSON_AddNumberToObject(o, "profile", g_type);
  cJSON_AddStringToObject(o, "type", present || g_type == D_NO_PROFILE ? type_name(g_type) : "");
  cJSON_AddStringToObject(o, "serial", g_serial);
  cJSON_AddStringToObject(o, "name", g_rname);
  cJSON_AddNumberToObject(o, "names", g_rnames);
  cJSON_AddStringToObject(o, "title_state", g_tstate == TS_OK ? "ok" : g_tstate == TS_GENERIC ? "generic" : g_tstate == TS_WAIT ? "wait" : "none");
  cJSON_AddBoolToObject(o, "has_title", type_has_title(g_type));
  cJSON_AddBoolToObject(o, "dumpable", present && type_is_dumpable(g_type) && (g_tstate == TS_OK || g_tstate == TS_GENERIC));
  cJSON_AddNumberToObject(o, "image_bytes", (double)g_image_bytes);
  cJSON *job = cJSON_AddObjectToObject(o, "job");
  cJSON_AddStringToObject(job, "state", k_job[g_job.state]);
  if(g_job.state != JOB_IDLE) {
    cJSON_AddStringToObject(job, "name", g_job.final_stem[0] ? g_job.final_stem : g_job.stem);
    cJSON_AddStringToObject(job, "serial", g_job.serial);
    cJSON_AddNumberToObject(job, "done", (double)g_job.done);
    cJSON_AddNumberToObject(job, "total", (double)g_job.total);
    cJSON_AddNumberToObject(job, "bytes_per_s", g_job.state == JOB_RUNNING ? g_job.rate : 0);
    cJSON_AddNumberToObject(job, "eta_s", g_job.state == JOB_RUNNING && g_job.rate > 1 && g_job.total > g_job.done ? (double)(g_job.total - g_job.done) / g_job.rate : -1);
    cJSON_AddBoolToObject(job, "verified", g_job.verified);
    if(g_job.err[0]) cJSON_AddStringToObject(job, "error", g_job.err);
    if(g_job.note[0]) cJSON_AddStringToObject(job, "note", g_job.note);
  }
  cJSON *ev = cJSON_AddArrayToObject(o, "events");
  for(int i = g_nev - 1; i >= 0; i--) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "time", (double)g_ev[i].t);
    cJSON_AddStringToObject(e, "text", g_ev[i].text);
    cJSON_AddItemToArray(ev, e);
  }
  pthread_mutex_unlock(&g_lock);

  struct stat st;
  cJSON *rd = cJSON_AddObjectToObject(o, "redump");
  cJSON_AddStringToObject(rd, "path", REDUMP_PATH);
  if(stat(REDUMP_PATH, &st) == 0 && S_ISREG(st.st_mode)) {
    cJSON_AddBoolToObject(rd, "present", 1);
    cJSON_AddNumberToObject(rd, "size", (double)st.st_size);
    cJSON_AddNumberToObject(rd, "mtime", (double)st.st_mtime);
    cJSON_AddBoolToObject(rd, "usable", (unsigned long long)st.st_size <= REDUMP_MAX && st.st_size >= 512);
  } else {
    cJSON_AddBoolToObject(rd, "present", 0);
  }
  cJSON_AddStringToObject(o, "dir", DISC_DIR);
  struct statvfs sv;
  cJSON_AddNumberToObject(o, "free_bytes", statvfs(PS5TM_DATA_DIR, &sv) == 0 ? (double)sv.f_bavail * (double)sv.f_frsize : -1);
  list_dumps(cJSON_AddArrayToObject(o, "dumps"));
  return o;
}

int
ps5tm_disc_action(const cJSON *b, char *msg, size_t mn) {
  const cJSON *ja = cJSON_GetObjectItem(b, "action");
  const char *a = cJSON_IsString(ja) ? ja->valuestring : "";
  int rc = 200;
  msg[0] = 0;
  pthread_mutex_lock(&g_lock);
  settings_load_locked();
  if(!strcmp(a, "enable")) {
    g_enabled = 1;
    settings_save();
    poll_start_locked();
    snprintf(msg, mn, "Die Laufwerks-Erkennung ist eingeschaltet.");
  } else if(!strcmp(a, "disable")) {
    if(g_job.state == JOB_RUNNING || g_job.state == JOB_VERIFY) { snprintf(msg, mn, "Während ein Abbild gesichert wird, bleibt die Erkennung an."); rc = 409; }
    else {
      g_enabled = 0;
      settings_save();
      disc_forget_locked();
      g_type = D_NOT_PRESENT;
      snprintf(msg, mn, "Die Laufwerks-Erkennung ist ausgeschaltet.");
    }
  } else if(!strcmp(a, "settings")) {
    const cJSON *x = cJSON_GetObjectItem(b, "auto_dump");
    if(cJSON_IsBool(x)) { g_auto = cJSON_IsTrue(x); if(!g_auto) g_auto_serial[0] = 0; settings_save(); }
    snprintf(msg, mn, "Gespeichert.");
  } else if(!strcmp(a, "dump")) {
    if(!g_enabled) { snprintf(msg, mn, "Die Laufwerks-Erkennung ist ausgeschaltet."); rc = 409; }
    else rc = dump_start_locked(msg, mn, 0);
  } else if(!strcmp(a, "cancel")) {
    if(g_job.state == JOB_RUNNING) { g_job.cancel = 1; snprintf(msg, mn, "Wird abgebrochen."); }
    else { snprintf(msg, mn, "Es läuft kein Abbild."); rc = 409; }
  } else if(!strcmp(a, "delete")) {
    const cJSON *jn = cJSON_GetObjectItem(b, "name");
    const char *nm = cJSON_IsString(jn) ? jn->valuestring : "";
    char p[700];
    if(g_job.state == JOB_RUNNING || g_job.state == JOB_VERIFY) { snprintf(msg, mn, "Während ein Abbild gesichert wird, wird nichts gelöscht."); rc = 409; }
    else if(!dump_name_ok(nm)) { snprintf(msg, mn, "Ungültiger Name."); rc = 400; }
    else {
      snprintf(p, sizeof p, DISC_DIR "/%s.iso", nm);
      struct stat st;
      if(stat(p, &st) != 0 || !S_ISREG(st.st_mode)) { snprintf(msg, mn, "Dieses Abbild gibt es nicht."); rc = 404; }
      else {
        unlink(p);
        sidecar_path(p, sizeof p, nm);
        unlink(p);
        snprintf(msg, mn, "Gelöscht.");
      }
    }
  } else {
    snprintf(msg, mn, "Unbekannte Aktion.");
    rc = 400;
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}

int
ps5tm_disc_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job.state == JOB_RUNNING || g_job.state == JOB_VERIFY;
  pthread_mutex_unlock(&g_lock);
  return b;
}

void
ps5tm_disc_boot(void) {
  pthread_mutex_lock(&g_lock);
  settings_load_locked();
  if(g_enabled) poll_start_locked();
  pthread_mutex_unlock(&g_lock);
}
