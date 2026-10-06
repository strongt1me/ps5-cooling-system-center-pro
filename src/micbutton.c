/* The console's temperature and fan speed on a double press of the
 * controller's microphone button.
 *
 * A payload cannot read the controller — scePadOpen() is refused, see
 * dualsense.c — but the system itself logs every press of the microphone
 * button. Captured live from the kernel log on 02.10.2026, one press:
 *
 *   <118>[Umm] MicMuteKeyPressed D=0x50301 U=0x10000001 PT=0
 *   <118>@ mbusSetUserMuteStatus:L5949
 *   <118>[Umm] onPostUmmStatusChanged() {"ummStatusInfo":[{…,"state":2,…}]}
 *
 * One press gives exactly one MicMuteKeyPressed line — checked with one, two
 * and three presses in a row, which gave one, two and three lines. Quick
 * presses followed each other 0.4 to 0.7 s apart. The "state" in the third
 * line flips between 1 and 2 with every press: a press toggles the microphone,
 * so two presses leave it as it was, which is what makes the double press a
 * trigger that costs the user nothing. A single press does what it always did
 * and shows nothing here.
 *
 * Up to 1.45.x the PS button was the trigger ([onPSButtonPressed]). On request
 * it moved here in 1.46.0 and is no longer watched for this; dualsense.c still
 * counts those lines, but only to tell that a controller is in use.
 *
 * The buffer has no timestamps, so the count is the clock, as in dualsense.c:
 * the first pass only sets the baseline, a growing count means presses, and a
 * falling one only means old lines scrolled out of the ring. A press that
 * coincides with an old line leaving is missed — harmless, the next one works.
 *
 * It is read once a second, so "twice in quick succession" is judged from what
 * each read finds: two or more new lines in one read, or one in each of two
 * reads that are at most MIC_PAIR_MS apart. That lets presses up to about two
 * seconds apart count as a pair — generous on purpose: a pair that was not
 * meant costs one notification, a pair that was missed costs another try.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps5tm.h"

#define MIC_MARK     "MicMuteKeyPressed"
/* At most one notification this often — about as long as one stays on screen,
   so a run of presses does not stack them up. It was 20 s at first (with the
   PS button), which swallowed eight deliberate presses in a row on the console
   (26.09.2026) and read as "works only once". Anyone who finds even 5 s too
   chatty switches it off. */
#define MIC_GAP_MS   5000ull
/* How long a lone press waits for its partner. The reads are a second apart,
   a little more when the probe thread has other work, so this covers the next
   read and one late one. */
#define MIC_PAIR_MS  2500ull

static int      g_have_baseline = 0;
static unsigned g_last_count    = 0;
/* A press that came alone and waits for a second one; g_lone_ms is when the
   read that found it took place. */
static int      g_lone          = 0;
static uint64_t g_lone_ms       = 0;
static uint64_t g_last_shown_ms = 0;

/* Our own lines go into the very buffer this reads: ps5tm_log() mirrors every
   entry to klog, and a few of them carry text a stranger chose — the path of
   a refused request, for one. A line like
     <118>[ps5tm.elf] [WARN] http_refused: … POST /MicMuteKeyPressed …
   would count as a press and put a temperature toast on the TV. The kernel
   prefixes each line with the name of the process that wrote it
   (main() sets "ps5tm.elf"), so lines of this payload are told apart by that
   tag — no assumption is made about what the system's own lines look like.
   Anchoring the marker to its position in a line would be stricter, but it
   would also stop counting for good should the console format them
   differently from the klog captures it was tuned on. */
#define MIC_SELF_TAG "[ps5tm.elf]"

/* Whether the line the marker sits on was written by this payload. */
static int
own_line(const char *buf, const char *hit) {
  const char *ls = hit;
  for(int back = 0; ls > buf && ls[-1] != '\n'; back++) {
    if(back > 1024) return 0;       /* no line is this long: not one of ours */
    ls--;
  }
  if(*ls == '<') {                  /* the priority tag: "<118>" */
    const char *q = ls + 1;
    while(*q >= '0' && *q <= '9') q++;
    if(q > ls + 1 && *q == '>') ls = q + 1;
  }
  return !strncmp(ls, MIC_SELF_TAG, sizeof(MIC_SELF_TAG) - 1);
}

static unsigned
count_presses(const char *buf) {
  unsigned n = 0;
  for(const char *p = buf; (p = strstr(p, MIC_MARK)) != NULL;
      p += sizeof(MIC_MARK) - 1)
    if(!own_line(buf, p)) n++;
  return n;
}

/* fresh: presses this read found beyond the last one's; now: when it took
   place. True when they make a double press. */
static int
double_press(unsigned fresh, uint64_t now) {
  if(g_lone && now - g_lone_ms > MIC_PAIR_MS) g_lone = 0;  /* nobody came */
  if(!fresh) return 0;
  if(fresh >= 2 || g_lone) {        /* a pair at once, or the lone one's partner */
    g_lone = 0;                     /* used up: the next press starts afresh */
    return 1;
  }
  g_lone    = 1;                    /* the first of a possible pair */
  g_lone_ms = now;
  return 0;
}

/* Probe thread only, once a second. */
void
ps5tm_micbutton_poll(void) {
  ps5tm_config_lock();
  int on = g_config.ps_button_status != 0;
  ps5tm_config_unlock();

  /* Switched off: nothing is read at all. Switched back on, the first pass
     sets a fresh baseline, so presses from the meantime raise no toast. */
  if(!on) {
    g_have_baseline = 0;
    g_lone          = 0;
    return;
  }

  size_t len = 0;
  char  *buf = ps5tm_msgbuf_read(&len);
  if(!buf) return;
  unsigned n = count_presses(buf);
  free(buf);

  if(!g_have_baseline) {
    g_have_baseline = 1;
    g_last_count    = n;
    return;
  }
  unsigned fresh = n > g_last_count ? n - g_last_count : 0;
  g_last_count   = n;

  uint64_t now = ps5tm_mono_ms();
  if(!double_press(fresh, now)) return;
  if(g_last_shown_ms && now - g_last_shown_ms < MIC_GAP_MS) return;
  g_last_shown_ms = now;

  ps5tm_snapshot_t snap;
  ps5tm_fan_snapshot(&snap);

  char cpu[16] = "–", fan[16] = "–";
  if(snap.sensors.cpu_valid)
    snprintf(cpu, sizeof(cpu), "%d °C", snap.sensors.cpu_c);
  if(snap.sensors.fan_duty_valid)
    snprintf(fan, sizeof(fan), "%d %%", snap.sensors.fan_duty_pct);

  PS5TM_INFO("mic_button_shown",
             "Mikrofon-Taste zweimal gedrückt: Prozessor %s, Lüfter %s als "
             "Meldung gezeigt.", cpu, fan);
  ps5tm_notify("Prozessor %s · Lüfter %s", cpu, fan);
}
