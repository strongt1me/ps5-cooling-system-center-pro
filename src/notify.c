/* On-screen notifications and LAN address discovery.
 *
 * A notification is how a payload confirms it actually started — the person
 * sending it is looking at the TV, not at a serial log — and since 1.44.0 it
 * is also how the controller shows temperature and fan speed: first by the
 * PS button, since 1.46.0 by a double press of the microphone button.
 *
 * ── Two ways onto the screen
 *
 * The PS5's own notifications, the toasts at the top RIGHT, go through
 * libSceNotification: sceNotificationSend(user, logged, json), the JSON naming
 * a view template and carrying the texts and an icon. That route is tried
 * first. The template and fields used here — InteractiveToastTemplateB,
 * channel "ServiceFeedback", use case "IDC", an icon read from a file — are
 * what the SDK's notify sample and ShadowMountPlus send, and ShadowMountPlus's
 * write-up of the overlay (TOASTS.md) lists the four fields the overlay
 * insists on. Those are GPL-3.0; the facts are taken here, not the code.
 *
 * sceKernelSendNotificationRequest, the PS4-era call, stays as the fallback
 * for a firmware without the module. It shows plain text, and on the PS5 it
 * lands at the top LEFT — which the user noticed on 27.09.2026 and asked to
 * have moved.
 */

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "dynsym.h"
#include "ps5tm.h"

/* The first 45 bytes are ignored by the notification service; the text lives
   at a fixed offset. Layout taken from the payload SDK. */
typedef struct {
  char unused[45];
  char message[3075];
} notify_request_t;

#ifndef PS5TM_HOST_TEST
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
#endif

/* Shown to every user on the console, the system's own user id. */
#define NOTIFY_USER_SYSTEM 0xFE
/* Written through our own view of the file system, referenced through the
   shell's. The two differ: /data is /user/data, but the overlay resolves only
   the latter — with "/data/…" the toast showed an empty placeholder where the
   icon belongs (27.09.2026). ShadowMountPlus references /user/data as well. */
#define NOTIFY_ICON_PATH   PS5TM_DATA_DIR "/notify-icon.png"
#define NOTIFY_ICON_URL    "/user" PS5TM_DATA_DIR "/notify-icon.png"
#define NOTIFY_ICON_ASSET  "/img/notify-icon.png"

#ifndef PS5TM_HOST_TEST
typedef int (*fn_notification_send_t)(int user, int logged, const char *json);

static pthread_once_t         g_once = PTHREAD_ONCE_INIT;
static fn_notification_send_t g_send;
static int                    g_icon_ok;

/* The toast shows an icon from a file. The picture — thermometer and fan,
   the user's design of 27.09.2026 — travels embedded with the web files and
   is written out once, again whenever its size changes. */
static int
write_icon(void) {
  const ps5tm_asset_t *icon = NULL;
  for(unsigned i = 0; i < ps5tm_assets_count; i++)
    if(!strcmp(ps5tm_assets[i].path, NOTIFY_ICON_ASSET)) icon = &ps5tm_assets[i];
  if(!icon) return 0;

  struct stat st;
  if(stat(NOTIFY_ICON_PATH, &st) == 0 && st.st_size == (off_t)icon->len)
    return 1;

  FILE *f = fopen(NOTIFY_ICON_PATH, "wb");
  if(!f) return 0;
  size_t n = fwrite(icon->data, 1, icon->len, f);
  fclose(f);
  return n == icon->len;
}

static void
resolve_once(void) {
#ifndef PS5TM_HOST_TEST
  g_send = (fn_notification_send_t)ps5tm_dynsym("libSceNotification.sprx",
                                                "sceNotificationSend");
  g_icon_ok = write_icon();
  PS5TM_INFO("notify_route",
             "Meldungen: %s%s.",
             g_send ? "PS5-Benachrichtigung (oben rechts)"
                    : "nur der alte Weg (oben links), sceNotificationSend fehlt",
             g_send && !g_icon_ok ? ", ohne eigenes Symbol" : "");
#endif
}

/* JSON string contents: quotes, backslashes and control characters escaped,
   line breaks turned into spaces — the overlay gets one line per field. */
static void
json_text(char *out, size_t out_len, const char *in, size_t in_len) {
  size_t o = 0;
  for(size_t i = 0; i < in_len && in[i] && o + 7 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    if(c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
    else if(c == '\n' || c == '\r' || c == '\t') out[o++] = ' ';
    else if(c < 0x20) o += (size_t)snprintf(out + o, out_len - o, "\\u%04x", c);
    else out[o++] = (char)c;
  }
  out[o] = '\0';
}

/* The first line of the text becomes the toast's message, the rest its extra
   line; the app's name sits above both. Returns the call's result, or -1. */
static int
send_toast(const char *text) {
  if(!g_send) return -1;

  const char *nl = strchr(text, '\n');
  size_t first = nl ? (size_t)(nl - text) : strlen(text);
  char msg[512], extra[1024];
  json_text(msg, sizeof(msg), text, first);
  json_text(extra, sizeof(extra), nl ? nl + 1 : "", nl ? strlen(nl + 1) : 0);

  char icon[160];
  if(g_icon_ok)
    snprintf(icon, sizeof(icon),
             "{\"type\":\"Url\",\"parameters\":{\"url\":\"%s\"}}",
             NOTIFY_ICON_URL);
  else
    snprintf(icon, sizeof(icon),
             "{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"community\"}}");

  char extra_field[1100] = "";
  if(extra[0])
    snprintf(extra_field, sizeof(extra_field),
             ",\"extraMessage\":{\"body\":\"%s\"}", extra);

  char json[3072];
  int w = snprintf(json, sizeof(json),
    "{\"rawData\":{"
      "\"viewTemplateType\":\"InteractiveToastTemplateB\","
      "\"channelType\":\"ServiceFeedback\","
      "\"useCaseId\":\"IDC\","
      "\"toastOverwriteType\":\"No\","
      "\"isImmediate\":true,"
      "\"priority\":100,"
      "\"viewData\":{"
        "\"icon\":%s,"
        "\"subMessage\":{\"body\":\"%s\"},"
        "\"message\":{\"body\":\"%s\"}%s"
      "},"
      "\"platformViews\":{\"previewDisabled\":{\"viewData\":{"
        "\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"community\"}},"
        "\"message\":{\"body\":\"%s\"}"
      "}}}"
    "}}",
    icon, PS5TM_APP_NAME, msg, extra_field, msg);
  if(w < 0 || (size_t)w >= sizeof(json)) return -1;

  return g_send(NOTIFY_USER_SYSTEM, 1, json);
}
#endif /* !PS5TM_HOST_TEST */


/* A toast with one button that opens a deep link — "psgm:play?id=PPSA20396",
 * the link behind the game's own tile on the home screen.
 *
 * Starting a game this way leaves the launcher to the shell. The app never
 * calls it: on current firmware Sony's launcher only accepts calls from
 * ShellUI itself (ps5upload's notes, FW 9.60-12.x), and the only way around
 * that is to run the call inside ShellUI with ptrace — turned down by the
 * user on 28.09.2026, like the overlay before it. A button in a toast goes
 * through LinkingPS.openURLArg, the path a tile press takes (TOASTS.md,
 * "Actions": DeepLink with parameters.actionUrl).
 *
 * logged = 1 keeps it in the notification list, so a missed toast can still
 * be opened there. Returns the service's result, or -1. */
int
ps5tm_notify_action(const char *message, const char *sub, const char *icon_path,
                    const char *action_name, const char *action_url) {
#ifdef PS5TM_HOST_TEST
  printf("[NOTIFY] %s / %s [%s -> %s]\n", message, sub, action_name, action_url);
  fflush(stdout);
  (void)icon_path;
  return 0;
#else
  pthread_once(&g_once, resolve_once);
  if(!g_send || !message || !action_name || !action_url) return -1;

  char msg[384], subm[384], aname[96], aurl[256], ipath[200];
  json_text(msg,   sizeof(msg),   message,     strlen(message));
  json_text(subm,  sizeof(subm),  sub ? sub : "", sub ? strlen(sub) : 0);
  json_text(aname, sizeof(aname), action_name, strlen(action_name));
  json_text(aurl,  sizeof(aurl),  action_url,  strlen(action_url));

  /* The overlay reads /user/... paths only (see NOTIFY_ICON_URL). */
  char icon[256];
  if(icon_path && !strncmp(icon_path, "/user/", 6)) {
    json_text(ipath, sizeof(ipath), icon_path, strlen(icon_path));
    snprintf(icon, sizeof(icon), "{\"type\":\"Url\",\"parameters\":{\"url\":\"%s\"}}",
             ipath);
  } else if(g_icon_ok) {
    snprintf(icon, sizeof(icon), "{\"type\":\"Url\",\"parameters\":{\"url\":\"%s\"}}",
             NOTIFY_ICON_URL);
  } else {
    snprintf(icon, sizeof(icon),
             "{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"community\"}}");
  }

  char json[3072];
  int w = snprintf(json, sizeof(json),
    "{\"rawData\":{"
      "\"viewTemplateType\":\"InteractiveToastTemplateB\","
      "\"channelType\":\"ServiceFeedback\","
      "\"useCaseId\":\"IDC\","
      "\"toastOverwriteType\":\"No\","
      "\"isImmediate\":true,"
      "\"priority\":100,"
      "\"viewData\":{"
        "\"icon\":%s,"
        "\"message\":{\"body\":\"%s\"},"
        "\"subMessage\":{\"body\":\"%s\"},"
        "\"actions\":[{"
          "\"actionName\":\"%s\","
          "\"actionType\":\"DeepLink\","
          "\"defaultFocus\":true,"
          "\"parameters\":{\"actionUrl\":\"%s\"}"
        "}]"
      "},"
      "\"platformViews\":{\"previewDisabled\":{\"viewData\":{"
        "\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"community\"}},"
        "\"message\":{\"body\":\"%s\"}"
      "}}}"
    "}}",
    icon, msg, subm, aname, aurl, msg);
  if(w < 0 || (size_t)w >= sizeof(json)) return -1;

  return g_send(NOTIFY_USER_SYSTEM, 1, json);
#endif
}


void
ps5tm_notify(const char *fmt, ...) {
  notify_request_t req;
  memset(&req, 0, sizeof(req));

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, ap);
  va_end(ap);

#ifdef PS5TM_HOST_TEST
  printf("[NOTIFY] %s\n", req.message);
  fflush(stdout);
#else
  pthread_once(&g_once, resolve_once);
  int rc = send_toast(req.message);
  if(rc == 0) return;

  /* Logged once: a refused toast is worth knowing about, a stream of the same
     complaint is not. The text still reaches the screen, the old way. */
  static int logged = 0;
  if(g_send && !logged) {
    logged = 1;
    PS5TM_WARN("notify_fallback",
               "PS5-Benachrichtigung abgelehnt (0x%08X), Meldungen kommen "
               "auf dem alten Weg (oben links).", (unsigned)rc);
  }
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
#endif
}


int
ps5tm_local_ip(char *out, size_t out_len) {
  if(!out || out_len < INET_ADDRSTRLEN) return -1;
  snprintf(out, out_len, "127.0.0.1");

  struct ifaddrs *list = NULL;
  if(getifaddrs(&list) != 0) return -1;

  int found = 0;
  for(struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
    if(!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
    if(ifa->ifa_name && !strncmp(ifa->ifa_name, "lo", 2)) continue;

    char buf[INET_ADDRSTRLEN] = {0};
    struct sockaddr_in *in = (struct sockaddr_in *)ifa->ifa_addr;
    if(!inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf))) continue;

    /* Skip unconfigured interfaces and link-local autoconfiguration. */
    if(!strncmp(buf, "0.", 2))       continue;
    if(!strncmp(buf, "127.", 4))     continue;
    if(!strncmp(buf, "169.254.", 8)) continue;

    snprintf(out, out_len, "%s", buf);
    found = 1;
    /* Keep scanning: on a console with both Wi-Fi and Ethernet up, the last
       configured interface is the one the user most likely reaches. */
  }

  freeifaddrs(list);
  return found ? 0 : -1;
}
