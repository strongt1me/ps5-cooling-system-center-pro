/* The messages between the app (pkginstall.c) and its install helper (pkginst_helper.c).
 *
 * Installing a package is one call into the console's system library (AppInstUtil), and that call is made in a
 * process of its own: a small ELF, started through the payload loader for one installation and gone afterwards.
 * Whatever the system keeps in that process — a half-initialised session, a slot it has not given back, a call that
 * never returns — then ends with the process, and the app, which has the fans to look after, is never the one that
 * hangs. The two talk over one TCP connection on the loopback address. Both ends are built from the same sources
 * and run on the same machine, so the structures cross the socket as they are, and no pointer is in them.
 *
 * What the helper sends back at the start (PKGI_OP_READY) carries a token. The app patches a fresh random token
 * into the helper's image before it hands the image to the loader (pkginst_token_slot below), so a connection that
 * is not from that very process, however fast it is to the port, does not know it. */
#ifndef PS5TM_PKGINST_IPC_H
#define PS5TM_PKGINST_IPC_H

#include <stdint.h>

#define PKGI_MAGIC        0x50434931u      /* "PCI1" */
#define PKGI_VERSION      4u
#ifndef PKGI_IPC_PORT
#define PKGI_IPC_PORT     18853            /* the helper connects here; not the port of other package tools */
#endif
#ifndef PKGI_STREAM_PORT
#define PKGI_STREAM_PORT  18851            /* what the system's installer reads the package from */
#endif
#define PKGI_TOKEN_LEN    16               /* characters, without the end */

/* Where the token sits in the helper's image: this text, then PKGI_TOKEN_LEN placeholder characters ('.'), which
   the app replaces. The text itself is never changed, so the app can find the place again. */
#define PKGI_TOKEN_MARK   "PS5CC-PKGI-TOKEN:"

/* The helper's own log. It writes there (it may die before it can say anything over the socket); the app empties it
   before it starts a helper and, when an installation fails, puts its lines into its own log. */
#ifndef PKGI_LOG_PATH
#define PKGI_LOG_PATH "/data/PS5-Cooling-Center/pkginst-helper.log"
#endif

enum {
  PKGI_OP_READY = 1,   /* helper -> app, once: the system library is initialised (result 0), or why not */
  PKGI_OP_INSTALL,     /* app -> helper: start the installation of uri; the answer carries the content id */
  PKGI_OP_STATUS,      /* app -> helper: how far is it; the content id says which installation, so that a helper that
                          did not make the call (a new one, after the first had gone) can ask as well */
  PKGI_OP_CLOSE,       /* app -> helper: leave (without ending the library's session: see pkginst_helper.c) */
  PKGI_OP_UNINSTALL    /* app -> helper: take title_id off the console the system's way (the game, then its updates and
                          add-ons); saved games stay. Like an installation, one per helper. */
};

/* Local failures, distinct from what the system library returns. */
#define PKGI_E_UNAVAILABLE   (-2001)
#define PKGI_E_DISCONNECTED  (-2002)
#define PKGI_E_CANCELED      (-2003)
#define PKGI_E_TIMEOUT       (-2004)
#define PKGI_E_BADREPLY      (-2005)

typedef struct {
  uint32_t magic, version, op, seq;
  char     uri[1024];
  char     name[320];
  char     icon_url[512];
  char     content_id[48];       /* PKGI_OP_STATUS: the installation asked about (what the answer to the call named) */
  char     title_id[16];         /* PKGI_OP_UNINSTALL: the title to take off */
} pkgi_request_t;

typedef struct {
  uint32_t magic, version, op, seq;
  int32_t  result;               /* the return value of the call that answered (0 = good), or a PKGI_E_ code */
  int32_t  pid;
  uint32_t native_ms;            /* how long the call took */
  int32_t  native_errno;
  char     token[PKGI_TOKEN_LEN + 1];
  char     build[24];
  /* PKGI_OP_INSTALL: what the system made of the package */
  char     content_id[48];
  int32_t  type, platform;
  /* PKGI_OP_STATUS: sceAppInstUtilGetInstallStatus */
  char     status[16];           /* "playable", "completed", "running", "error", ... as the system spells it */
  char     src_type[8];
  uint32_t remain_time;
  uint32_t promote_progress;
  uint64_t downloaded, total, initial_chunk;
  int32_t  error_code;
  int32_t  error_version;
  char     error_type[9];
  char     error_desc[512];
  int32_t  local_copy_percent;
  uint32_t copy_only;
  /* PKGI_OP_UNINSTALL: result is sceAppInstUtilAppUnInstall; these the calls for the updates and the add-ons after it */
  int32_t  result_pat, result_addcont;
} pkgi_response_t;

#endif
