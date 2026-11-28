/* Host test of the profile suggestions (28.11.2026).
 *
 * Part 1, src/playtime.c with a clock of its own: a session under the automatic mode counts the time 1 K or more above
 * the target and the fan's jumps of 10 points, keeps the setting in force, marks a change of it, and writes a v2 row
 * that reads back as such; an old row (before 28.11.2026) still reads, without the measured part.
 * Part 2, ps5tm_cs_eval() on rows made by hand: which game gets which suggestion, what does not count, the effect of a
 * kept profile. Part 3, the JSON and hiding a suggestion (src/coolsuggest.c with the real sessions file). */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

/* ---- what the two files need from the rest of the app ---- */
ps5tm_config_t g_config;
void ps5tm_config_lock(void) {}
void ps5tm_config_unlock(void) {}
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }
static uint64_t g_now_ms = 1795900000000ull;           /* 28.11.2026 */
uint64_t ps5tm_now_ms(void)  { return g_now_ms; }
uint64_t ps5tm_mono_ms(void) { return g_now_ms; }
void ps5tm_gamestate_get(ps5tm_gamestate_t *o) { memset(o, 0, sizeof(*o)); }
int  ps5tm_gamestate_measured(void) { return 1; }
uint64_t ps5tm_gamestate_age_ms(void) { return 0; }
void ps5tm_fan_snapshot(ps5tm_snapshot_t *o) { memset(o, 0, sizeof(*o)); }
#undef pthread_create
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if(!(cond)) { g_fail++; printf("FEHLER %s:%d: ", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); printf("\n"); } } while(0)


/* ------------------------------------------------------------ part 1 */

static ps5tm_gamestate_t g_gs;

static void
run(int seconds, const ps5tm_playtime_sample_t *smp, const char *title) {
  memset(&g_gs, 0, sizeof(g_gs));
  if(title) {
    snprintf(g_gs.title_id, sizeof(g_gs.title_id), "%s", title);
    snprintf(g_gs.title_name, sizeof(g_gs.title_name), "Testspiel %s", title + 4);
    g_gs.foreground = 1;
  }
  for(int i = 0; i < seconds; i++) {
    g_now_ms += 1000;
    ps5tm_playtime_step((int64_t)(g_now_ms / 1000), g_now_ms, 1, &g_gs, smp);
  }
}

static ps5tm_playtime_sample_t
sample(int avg_c10, int fan, int target, int profile, int rule) {
  ps5tm_playtime_sample_t s = { 70, 68, fan, 0, 0, 1, avg_c10, target, profile, rule };
  return s;
}

static void
part1(void) {
  /* 700 s of one game: 100 s at 65 °C, 300 s at 68 °C (target 66: 1 K or more above it), 300 s at 65.5 °C; the fan
     swings 30 -> 45 -> 30 every 50 s while it is hot (each move of 15 points is a jump) */
  ps5tm_playtime_sample_t s = sample(650, 30, 66, 1, 0);
  run(100, &s, "PPSA00001");
  for(int k = 0; k < 6; k++) {
    s = sample(680, k % 2 ? 30 : 45, 66, 1, 0);
    run(50, &s, "PPSA00001");
  }
  s = sample(655, 30, 66, 1, 0);
  run(300, &s, "PPSA00001");
  run(60, NULL, NULL);                                   /* gone for longer than the grace: the session ends */

  ps5tm_pt_row_t *rows = NULL;
  int n = ps5tm_playtime_rows(&rows, 100);
  CHECK(n == 1, "eine Sitzung erwartet, %d", n);
  if(n >= 1) {
    const ps5tm_pt_row_t *r = &rows[n - 1];
    CHECK(r->measured, "Zeile v2 erwartet");
    CHECK(r->play >= 695 && r->play <= 700, "Spielzeit %u", r->play);
    CHECK(r->auto_s >= 695 && r->auto_s <= 700, "auto_s %d", r->auto_s);
    CHECK(r->over_s >= 298 && r->over_s <= 300, "über dem Ziel %d (300 erwartet)", r->over_s);
    CHECK(r->jumps == 6, "6 Sprünge erwartet (30->45 und zurück, dreimal), %d", r->jumps);
    CHECK(r->target_c == 66 && r->profile == 1, "Einstellung %d/%d", r->target_c, r->profile);
    CHECK(!(r->flags & PS5TM_PT_F_MIXED) && !(r->flags & PS5TM_PT_F_RULE), "Marken %u", r->flags);
    CHECK(!strcmp(r->title, "PPSA00001"), "Titel %s", r->title);
  }
  free(rows);

  /* a second game whose target changes mid-session (the night mode starts): marked, and under a rule */
  s = sample(650, 30, 66, 1, 1);
  run(400, &s, "PPSA00002");
  s = sample(650, 30, 62, 1, 1);
  run(400, &s, "PPSA00002");
  /* a third that runs half the time under observe: the automatic share is what counts */
  s = sample(650, 30, 66, 1, 0);
  run(200, &s, "PPSA00003");
  s.automatic = 0;
  s.fan_pct = 80;                                          /* the console's own fan: no jump on the way back */
  run(200, &s, "PPSA00003");
  s.automatic = 1;
  s.fan_pct = 30;
  run(200, &s, "PPSA00003");
  run(60, NULL, NULL);

  rows = NULL;
  n = ps5tm_playtime_rows(&rows, 100);
  CHECK(n == 3, "drei Sitzungen erwartet, %d", n);
  if(n == 3) {
    CHECK(rows[1].flags & PS5TM_PT_F_MIXED, "Zielwechsel nicht markiert (%u)", rows[1].flags);
    CHECK(rows[1].flags & PS5TM_PT_F_RULE, "Spielprofil nicht markiert (%u)", rows[1].flags);
    CHECK(rows[1].target_c == 62, "letztes Ziel %d", rows[1].target_c);
    CHECK(rows[2].auto_s >= 395 && rows[2].auto_s <= 400, "Automatik-Anteil %d", rows[2].auto_s);
    CHECK(rows[2].jumps == 0, "Sprünge über den Beobachten-Teil gezählt: %d", rows[2].jumps);
  }
  free(rows);

  /* a row of the format before 28.11.2026 reads on, without the measured part */
  FILE *f = fopen(PS5TM_DATA_DIR "/sessions.csv", "a");
  CHECK(f != NULL, "sessions.csv nicht offen");
  if(f) {
    fprintf(f, "1795000000,1795003600,3500,70,65,68,63,40,55,0,PPSA00004,Altes Spiel, mit Komma\n");
    fclose(f);
  }
  rows = NULL;
  n = ps5tm_playtime_rows(&rows, 100);
  CHECK(n == 4, "vier Zeilen erwartet, %d", n);
  if(n == 4) {
    CHECK(!rows[3].measured && rows[3].jumps == -1 && rows[3].auto_s == -1, "alte Zeile mit Messwerten?");
    CHECK(!strcmp(rows[3].name, "Altes Spiel, mit Komma"), "Name %s", rows[3].name);
  }
  free(rows);

  cJSON *j = ps5tm_playtime_json(10);
  CHECK(j != NULL, "JSON");
  if(j) {
    const cJSON *ss = cJSON_GetObjectItem(j, "sessions");
    const cJSON *newest = cJSON_GetArrayItem(ss, 0), *first = cJSON_GetArrayItem(ss, 3);
    CHECK(newest && !cJSON_GetObjectItem(newest, "jumps"), "alte Zeile hat jumps im JSON");
    CHECK(first && cJSON_GetObjectItem(first, "jumps") && cJSON_GetObjectItem(first, "jumps")->valueint == 6,
          "jumps der ersten Sitzung im JSON");
    cJSON_Delete(j);
  }
  unlink(PS5TM_DATA_DIR "/sessions.csv");
}


/* ------------------------------------------------------------ part 2 */

static ps5tm_pt_row_t g_rows[200];
static unsigned g_n;

static void
row(const char *title, int64_t end, unsigned play, int auto_s, int over_s, int jumps, int hot, unsigned flags,
    int target, int profile) {
  ps5tm_pt_row_t *r = &g_rows[g_n++];
  memset(r, 0, sizeof(*r));
  snprintf(r->title, sizeof(r->title), "%s", title);
  snprintf(r->name, sizeof(r->name), "Spiel %s", title + 4);
  r->start = end - play; r->end = end; r->play = play; r->flags = flags;
  r->cpu_max = hot; r->soc_max = hot - 3; r->cpu_avg = hot - 5; r->soc_avg = hot - 8;
  r->fan_avg = 40; r->fan_max = 60;
  r->measured = 1; r->auto_s = auto_s; r->over_s = over_s; r->jumps = jumps;
  r->target_c = target; r->profile = profile;
}

static const ps5tm_cs_game_t *
game(const ps5tm_cs_game_t *g, unsigned n, const char *title) {
  for(unsigned i = 0; i < n; i++) if(!strcmp(g[i].title, title)) return &g[i];
  return NULL;
}

static void
part2(void) {
  const int64_t now = 1795900000;
  const int B = PS5TM_PROFILE_BALANCED, C = PS5TM_PROFILE_COOL;
  g_n = 0;
  int64_t t = now - 30 * 86400;
  /* A: 4 sessions 25 % above the target -> "peaks", Kühl at the global target */
  for(int i = 0; i < 4; i++) row("PPSA00010", t += 3600, 1200, 1200, 300, 2, 80 + i, 0, 66, B);
  /* B: 3 hours, never above, 30 jumps an hour -> "jumpy", Leise */
  for(int i = 0; i < 3; i++) row("PPSA00011", t += 7200, 3600, 3600, 0, 30, 70, 0, 66, B);
  /* C: 2 sessions -> collecting */
  for(int i = 0; i < 2; i++) row("PPSA00012", t += 3600, 1200, 1200, 600, 0, 75, 0, 66, B);
  /* D: 3 calm sessions -> fine */
  for(int i = 0; i < 3; i++) row("PPSA00013", t += 3600, 1800, 1800, 60, 2, 68, 0, 66, B);
  /* E: kept cool 66: 3 without the rule, 2 with exactly it, 1 with another setting (ignored) */
  for(int i = 0; i < 3; i++) row("PPSA00014", t += 3600, 3600, 3600, 900, 20, 82, 0, 66, B);
  for(int i = 0; i < 2; i++) row("PPSA00014", t += 3600, 3600, 3600, 180, 8, 78, PS5TM_PT_F_RULE, 66, C);
  row("PPSA00014", t += 3600, 3600, 3600, 3000, 99, 90, PS5TM_PT_F_RULE, 70, B);
  /* F: kept, one session with it -> measuring */
  row("PPSA00015", t += 3600, 1800, 1800, 0, 1, 70, PS5TM_PT_F_RULE, 70, B);
  /* G: 3 sessions, but one short, one mostly observe, one mixed -> nothing counts -> collecting with n 0 -> dropped? no:
     n counts usable ones only, 0, and no rule -> the game is left out */
  row("PPSA00016", t += 3600, 500, 500, 400, 50, 85, 0, 66, B);
  row("PPSA00016", t += 3600, 1800, 1000, 900, 50, 85, 0, 66, B);
  row("PPSA00016", t += 3600, 1800, 1800, 1500, 50, 85, PS5TM_PT_F_MIXED, 66, B);
  /* H: 3 sessions with 10 % above, but the warning in one -> peaks */
  for(int i = 0; i < 3; i++) row("PPSA00017", t += 3600, 1800, 1800, 180, 2, 76, i == 1 ? PS5TM_PT_F_WARNING : 0, 66, B);
  /* I: peaks and jumpy -> peaks wins */
  for(int i = 0; i < 3; i++) row("PPSA00018", t += 3600, 1800, 1800, 900, 40, 84, 0, 66, B);
  /* J: old sessions (200 days) do not count */
  for(int i = 0; i < 3; i++) row("PPSA00019", now - 200 * 86400 + i * 3600, 1800, 1800, 900, 2, 84, 0, 66, B);
  /* K: rows of a game without the measured part (old format) do not count */
  for(int i = 0; i < 3; i++) {
    row("PPSA00020", t += 3600, 1800, 1800, 900, 2, 84, 0, 66, B);
    g_rows[g_n - 1].measured = 0;
  }

  ps5tm_game_rule_t rules[3];
  memset(rules, 0, sizeof(rules));
  snprintf(rules[0].title_id, sizeof(rules[0].title_id), "PPSA00014"); rules[0].target_temp_c = 66; rules[0].profile = C;
  snprintf(rules[1].title_id, sizeof(rules[1].title_id), "PPSA00015"); rules[1].target_temp_c = 70; rules[1].profile = B;
  snprintf(rules[2].title_id, sizeof(rules[2].title_id), "PPSA00030"); rules[2].target_temp_c = 64; rules[2].profile = C;
  snprintf(rules[2].title_name, sizeof(rules[2].title_name), "Nur Regel");

  ps5tm_cs_game_t out[PS5TM_CS_MAX_GAMES];
  unsigned n = ps5tm_cs_eval(g_rows, g_n, rules, 3, 66, B, now, out, PS5TM_CS_MAX_GAMES);
  const ps5tm_cs_game_t *g;

  g = game(out, n, "PPSA00010");
  CHECK(g && g->state == PS5TM_CS_SUGGEST && g->kind == 1 && g->profile == C && g->target_c == 66, "A: Kühl erwartet");
  CHECK(g && g->n == 4 && g->over_n == 4 && g->now.over_min_h == 15, "A: Werte n=%d over_n=%d over=%d",
        g ? g->n : -1, g ? g->over_n : -1, g ? g->now.over_min_h : -1);
  CHECK(g && g->now.hot_c == 82, "A: Median der Höchstwerte 80..83 = 82 (gerundet), %d", g ? g->now.hot_c : -1);
  g = game(out, n, "PPSA00011");
  CHECK(g && g->state == PS5TM_CS_SUGGEST && g->kind == 2 && g->profile == PS5TM_PROFILE_COMFORT, "B: Leise erwartet");
  CHECK(g && g->now.jumps_h == 30, "B: 30 Sprünge/Std., %d", g ? g->now.jumps_h : -1);
  g = game(out, n, "PPSA00012");
  CHECK(g && g->state == PS5TM_CS_COLLECTING && g->n == 2, "C: sammelt");
  g = game(out, n, "PPSA00013");
  CHECK(g && g->state == PS5TM_CS_FINE, "D: nichts vorzuschlagen");
  g = game(out, n, "PPSA00014");
  CHECK(g && g->state == PS5TM_CS_EFFECT && g->has_rule, "E: Wirkung");
  CHECK(g && g->before.n == 3 && g->after.n == 2, "E: 3 vorher, 2 nachher (die mit anderem Ziel zählt nicht): %d/%d",
        g ? g->before.n : -1, g ? g->after.n : -1);
  CHECK(g && g->before.over_min_h == 15 && g->after.over_min_h == 3, "E: über dem Ziel 15 -> 3, %d -> %d",
        g ? g->before.over_min_h : -1, g ? g->after.over_min_h : -1);
  CHECK(g && g->before.jumps_h == 20 && g->after.jumps_h == 8, "E: Sprünge 20 -> 8");
  g = game(out, n, "PPSA00015");
  CHECK(g && g->state == PS5TM_CS_MEASURING && g->after.n == 1, "F: wird gemessen");
  CHECK(game(out, n, "PPSA00016") == NULL, "G: ohne zählende Sitzung nicht in der Liste");
  g = game(out, n, "PPSA00017");
  CHECK(g && g->state == PS5TM_CS_SUGGEST && g->kind == 1 && g->warn_n == 1, "H: Warnung -> Kühl");
  g = game(out, n, "PPSA00018");
  CHECK(g && g->state == PS5TM_CS_SUGGEST && g->kind == 1, "I: Spitzen gehen vor Sprüngen");
  CHECK(game(out, n, "PPSA00019") == NULL, "J: alte Sitzungen zählen nicht");
  CHECK(game(out, n, "PPSA00020") == NULL, "K: Zeilen ohne Messwerte zählen nicht");
  g = game(out, n, "PPSA00030");
  CHECK(g && g->state == PS5TM_CS_MEASURING && g->n == 0 && !strcmp(g->name, "Nur Regel"), "L: Regel ohne Sitzung");
  for(unsigned i = 1; i < n; i++)
    CHECK(out[i - 1].last >= out[i].last, "Reihenfolge: das neueste Spiel zuerst (%u)", i);

  /* the global profile is "Kühl" already: no "peaks" suggestion, and "jumpy" stays */
  n = ps5tm_cs_eval(g_rows, g_n, rules, 3, 66, C, now, out, PS5TM_CS_MAX_GAMES);
  g = game(out, n, "PPSA00010");
  CHECK(g && g->state == PS5TM_CS_FINE, "Kühl schon aktiv: kein Vorschlag");
  g = game(out, n, "PPSA00011");
  CHECK(g && g->state == PS5TM_CS_SUGGEST && g->kind == 2, "Leise bleibt");
  /* ... and "Leise" already: no "jumpy" */
  n = ps5tm_cs_eval(g_rows, g_n, rules, 3, 66, PS5TM_PROFILE_COMFORT, now, out, PS5TM_CS_MAX_GAMES);
  g = game(out, n, "PPSA00011");
  CHECK(g && g->state == PS5TM_CS_FINE, "Leise schon aktiv: kein Vorschlag");
}


/* ------------------------------------------------------------ part 3 */

static void
write_rows(int jumps, int over) {
  FILE *f = fopen(PS5TM_DATA_DIR "/sessions.csv", "w");
  if(!f) return;
  for(int i = 0; i < 3; i++)
    fprintf(f, "v2,%lld,%lld,1800,80,75,77,70,42,60,0,1800,%d,%d,66,1,PPSA00040,Spiel 40\n",
            (long long)(1795800000 + i * 3600), (long long)(1795800000 + i * 3600 + 1800), over, jumps);
  fclose(f);
}

static int
hidden_of(const char *title, const char **state) {
  cJSON *j = ps5tm_cs_json();
  int h = -1;
  *state = "";
  static char st[32];
  const cJSON *g;
  cJSON_ArrayForEach(g, cJSON_GetObjectItem(j, "games")) {
    if(strcmp(cJSON_GetObjectItem(g, "id")->valuestring, title)) continue;
    snprintf(st, sizeof(st), "%s", cJSON_GetObjectItem(g, "state")->valuestring);
    *state = st;
    const cJSON *x = cJSON_GetObjectItem(g, "hidden");
    h = x ? cJSON_IsTrue(x) : -1;
  }
  cJSON_Delete(j);
  return h;
}

static void
part3(void) {
  memset(&g_config, 0, sizeof(g_config));
  g_config.target_temp_c = 66;
  g_config.profile = PS5TM_PROFILE_BALANCED;
  const char *st;

  write_rows(2, 600);                                     /* 33 % above: peaks */
  CHECK(hidden_of("PPSA00040", &st) == 0 && !strcmp(st, "suggest"), "Vorschlag sichtbar (%s)", st);
  CHECK(ps5tm_cs_hide("PPSA00040") == 0, "Ausblenden");
  CHECK(hidden_of("PPSA00040", &st) == 1, "nach dem Ausblenden verborgen");
  CHECK(ps5tm_cs_hide("PPSA00099") != 0, "ohne Vorschlag: nichts auszublenden");
  CHECK(ps5tm_cs_hide("../x") != 0, "ungültiger Titel abgelehnt");

  write_rows(40, 0);                                       /* now jumpy: another suggestion, shown again */
  CHECK(hidden_of("PPSA00040", &st) == 0, "anderer Vorschlag wieder sichtbar");

  /* a kept profile: the suggestion turns into "measuring" */
  g_config.game_rule_count = 1;
  snprintf(g_config.game_rules[0].title_id, sizeof(g_config.game_rules[0].title_id), "PPSA00040");
  g_config.game_rules[0].target_temp_c = 66;
  g_config.game_rules[0].profile = PS5TM_PROFILE_COMFORT;
  CHECK(hidden_of("PPSA00040", &st) == -1 && !strcmp(st, "measuring"), "mit Profil: wird gemessen (%s)", st);
}


int
main(void) {
  part1();
  part2();
  part3();
  printf("%d checks, %d Fehler\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
