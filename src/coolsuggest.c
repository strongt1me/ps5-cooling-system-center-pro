/* Profile suggestions per game, from the measured sessions (28.11.2026, wish of the user after a colleague's idea:
 * "the app learns how this console behaves with each game").
 *
 * No model and nothing that guesses: plain statistics over what playtime.c keeps per session since 28.11.2026 — the
 * time under the automatic mode, the time 1 K or more above the target in force, the fan's clear jumps (10 points),
 * and the target, profile and game rule in force. Every number a page shows can be checked against the play-time list.
 *
 * ── Which sessions count
 *
 * One of at least PS5TM_CS_MIN_PLAY_S in front, at least 80 % of it under the automatic mode, no change of target,
 * profile or rule within it (PS5TM_PT_F_MIXED: the night mode starting mid-game is one), not older than
 * PS5TM_CS_MAX_AGE_S. Per group the newest PS5TM_CS_WINDOW of them.
 *
 * ── A game without a kept profile
 *
 * From PS5TM_CS_MIN_SESSIONS on, two findings:
 *   - "peaks": PS5TM_CS_OVER_PCT % or more of the time above the target, or the temperature warning in a session.
 *     Suggested: "Kühl" with the same target. It reacts sooner and harder (narrower band, shorter window, config.c),
 *     so the console spends less time above the target; the price is a louder fan, and the page says so.
 *   - "jumpy": PS5TM_CS_JUMPS_H jumps or more per hour, without "peaks". Suggested: "Leise". Its band is wider and
 *     its step a third smaller, so the same load moves the fan in smaller steps; the target stays.
 * Nothing is suggested that is in force already (no "Kühl" when the global profile is "Kühl"), and "Leise" never
 * against peaks: it would make them worse. The suggested target is the global one, not a session's: a session in
 * the night mode carries the night target, a kept profile is in force day and night.
 *
 * ── A game with a kept profile
 *
 * Its sessions without a rule are "before", those with exactly the kept target and profile "after". From
 * PS5TM_CS_MIN_AFTER on each side the page shows the effect. A profile kept before this version has no "before"; the
 * page then shows the "after" numbers alone.
 *
 * ── Hiding
 *
 * "Ausblenden" writes the game and the kind of its suggestion to suggest-hidden.txt. The same suggestion stays hidden;
 * a different one (the console got hotter: "jumpy" became "peaks") shows again. Nothing else is ever written here. */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#ifndef CS_HIDDEN_PATH
#define CS_HIDDEN_PATH PS5TM_DATA_DIR "/suggest-hidden.txt"
#endif
#define CS_HIDDEN_MAX  64
#define CS_ROWS_MAX    3500

enum { KIND_NONE = 0, KIND_PEAKS = 1, KIND_JUMPY = 2 };

static pthread_mutex_t g_hidden_lock = PTHREAD_MUTEX_INITIALIZER;


static int
title_ok(const char *t) {
  if(!t || strlen(t) != 9) return 0;
  for(int i = 0; i < 4; i++) if(t[i] < 'A' || t[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(t[i] < '0' || t[i] > '9') return 0;
  return 1;
}


static int
usable(const ps5tm_pt_row_t *r, int64_t now_s) {
  if(!r->measured || r->play < PS5TM_CS_MIN_PLAY_S || r->auto_s < 0 || r->over_s < 0 || r->jumps < 0) return 0;
  if((int64_t)r->auto_s * 10 < (int64_t)r->play * 8) return 0;          /* mostly under the automatic mode */
  if(r->flags & PS5TM_PT_F_MIXED) return 0;
  if(now_s > 0 && r->end < now_s - PS5TM_CS_MAX_AGE_S) return 0;
  return 1;
}


typedef struct {
  int       n, over_n, warn_n;
  long long auto_s, over_s, jumps, fan_w, fan_play;
  int       hot[PS5TM_CS_WINDOW];
  int       nhot;
} acc_t;


static void
acc_add(acc_t *a, const ps5tm_pt_row_t *r) {
  a->n++;
  a->auto_s += r->auto_s;
  a->over_s += r->over_s;
  a->jumps  += r->jumps;
  if(r->fan_avg >= 0) { a->fan_w += (long long)r->fan_avg * r->play; a->fan_play += r->play; }
  int hot = r->cpu_max > r->soc_max ? r->cpu_max : r->soc_max;
  if(hot >= 0 && a->nhot < PS5TM_CS_WINDOW) a->hot[a->nhot++] = hot;
  if(r->auto_s > 0 && (long long)r->over_s * 100 >= (long long)r->auto_s * PS5TM_CS_OVER_PCT) a->over_n++;
  if(r->flags & (PS5TM_PT_F_WARNING | PS5TM_PT_F_SAFETY)) a->warn_n++;
}


static int
cmp_int(const void *x, const void *y) {
  int a = *(const int *)x, b = *(const int *)y;
  return (a > b) - (a < b);
}


static ps5tm_cs_stats_t
acc_stats(const acc_t *a) {
  ps5tm_cs_stats_t s = { a->n, -1, -1, -1, -1 };
  if(a->auto_s > 0) {
    s.over_min_h = (int)((a->over_s * 60 + a->auto_s / 2) / a->auto_s);
    s.jumps_h    = (int)((a->jumps * 3600 + a->auto_s / 2) / a->auto_s);
  }
  if(a->nhot > 0) {                          /* the typical hottest point: the median, not one bad evening */
    int h[PS5TM_CS_WINDOW];
    memcpy(h, a->hot, sizeof(int) * (size_t)a->nhot);
    qsort(h, (size_t)a->nhot, sizeof(int), cmp_int);
    s.hot_c = a->nhot % 2 ? h[a->nhot / 2] : (h[a->nhot / 2 - 1] + h[a->nhot / 2] + 1) / 2;
  }
  if(a->fan_play > 0) s.fan_avg = (int)((a->fan_w + a->fan_play / 2) / a->fan_play);
  return s;
}


static ps5tm_cs_game_t *
find_or_add(ps5tm_cs_game_t *out, unsigned *n, unsigned max, const char *title) {
  for(unsigned i = 0; i < *n; i++) if(!strcmp(out[i].title, title)) return &out[i];
  if(*n >= max) return NULL;
  ps5tm_cs_game_t *g = &out[(*n)++];
  memset(g, 0, sizeof(*g));
  snprintf(g->title, sizeof(g->title), "%s", title);
  return g;
}


static int
cmp_game(const void *x, const void *y) {
  const ps5tm_cs_game_t *a = x, *b = y;
  return (a->last < b->last) - (a->last > b->last);   /* newest first */
}


unsigned
ps5tm_cs_eval(const ps5tm_pt_row_t *rows, unsigned nrows,
              const ps5tm_game_rule_t *rules, unsigned nrules,
              unsigned global_target_c, int global_profile, int64_t now_s,
              ps5tm_cs_game_t *out, unsigned max) {
  unsigned n = 0;
  if(!out || !max) return 0;

  /* The games: every title with a row (the newest row gives its name and its place) and every kept rule. */
  for(unsigned i = nrows; i-- > 0;) {
    const ps5tm_pt_row_t *r = &rows[i];
    ps5tm_cs_game_t *g = find_or_add(out, &n, max, r->title);
    if(g && !g->name[0]) {
      ps5tm_copy_utf8(g->name, sizeof(g->name), r->name[0] ? r->name : r->title);
      g->last = r->end;
    }
  }
  for(unsigned k = 0; k < nrules; k++) {
    ps5tm_cs_game_t *g = find_or_add(out, &n, max, rules[k].title_id);
    if(g && !g->name[0])
      ps5tm_copy_utf8(g->name, sizeof(g->name), rules[k].title_name[0] ? rules[k].title_name : rules[k].title_id);
  }

  for(unsigned j = 0; j < n; j++) {
    ps5tm_cs_game_t *g = &out[j];
    const ps5tm_game_rule_t *rule = NULL;
    for(unsigned k = 0; k < nrules; k++)
      if(!strcmp(rules[k].title_id, g->title)) { rule = &rules[k]; break; }

    acc_t now, before, after;
    memset(&now, 0, sizeof(now)); memset(&before, 0, sizeof(before)); memset(&after, 0, sizeof(after));
    for(unsigned i = nrows; i-- > 0;) {                 /* newest first: the window is the newest sessions */
      const ps5tm_pt_row_t *r = &rows[i];
      if(strcmp(r->title, g->title) != 0 || !usable(r, now_s)) continue;
      int with_rule = (r->flags & PS5TM_PT_F_RULE) != 0;
      if(rule) {
        if(with_rule && r->target_c == (int)rule->target_temp_c && r->profile == (int)rule->profile) {
          if(after.n < PS5TM_CS_WINDOW) acc_add(&after, r);
        } else if(!with_rule) {
          if(before.n < PS5TM_CS_WINDOW) acc_add(&before, r);
        }
      } else if(!with_rule && now.n < PS5TM_CS_WINDOW) {
        acc_add(&now, r);
      }
    }

    if(rule) {
      g->has_rule      = 1;
      g->rule_profile  = (int)rule->profile;
      g->rule_target_c = (int)rule->target_temp_c;
      g->before = acc_stats(&before);
      g->after  = acc_stats(&after);
      g->n      = before.n + after.n;
      g->state  = after.n >= PS5TM_CS_MIN_AFTER ? PS5TM_CS_EFFECT : PS5TM_CS_MEASURING;
      continue;
    }

    g->now    = acc_stats(&now);
    g->n      = now.n;
    g->over_n = now.over_n;
    g->warn_n = now.warn_n;
    if(now.n < PS5TM_CS_MIN_SESSIONS) { g->state = PS5TM_CS_COLLECTING; continue; }

    int peaks = now.auto_s > 0 && now.over_s * 100 >= now.auto_s * PS5TM_CS_OVER_PCT;
    if(now.warn_n > 0) peaks = 1;
    int jumpy = g->now.jumps_h >= PS5TM_CS_JUMPS_H;
    g->state = PS5TM_CS_FINE;
    if(peaks && global_profile != PS5TM_PROFILE_COOL) {
      g->state = PS5TM_CS_SUGGEST; g->kind = KIND_PEAKS; g->profile = PS5TM_PROFILE_COOL;
    } else if(!peaks && jumpy && global_profile != PS5TM_PROFILE_COMFORT) {
      g->state = PS5TM_CS_SUGGEST; g->kind = KIND_JUMPY; g->profile = PS5TM_PROFILE_COMFORT;
    }
    g->target_c = (int)global_target_c;
  }

  /* Games that have neither a measured session nor a rule say nothing: they go. */
  unsigned keep = 0;
  for(unsigned j = 0; j < n; j++)
    if(out[j].has_rule || out[j].n > 0) out[keep++] = out[j];
  qsort(out, keep, sizeof(*out), cmp_game);
  return keep;
}


/* ------------------------------------------------------------------ hidden */

typedef struct { char title[16]; int kind; } hidden_t;

static unsigned
hidden_read(hidden_t *h, unsigned max) {
  unsigned n = 0;
  FILE *f = fopen(CS_HIDDEN_PATH, "r");
  if(!f) return 0;
  char line[64];
  while(n < max && fgets(line, sizeof(line), f)) {
    char t[16];
    int  kind = 0;
    if(sscanf(line, "%15s %d", t, &kind) == 2 && title_ok(t) && (kind == KIND_PEAKS || kind == KIND_JUMPY)) {
      snprintf(h[n].title, sizeof(h[n].title), "%s", t);
      h[n].kind = kind;
      n++;
    }
  }
  fclose(f);
  return n;
}


static int
is_hidden(const hidden_t *h, unsigned n, const ps5tm_cs_game_t *g) {
  for(unsigned i = 0; i < n; i++)
    if(!strcmp(h[i].title, g->title) && h[i].kind == g->kind) return 1;
  return 0;
}


/* ------------------------------------------------------------------- glue */

static const char *
profile_name(int p) {
  return p == PS5TM_PROFILE_COOL ? "cool" : p == PS5TM_PROFILE_BALANCED ? "balanced" : "comfort";
}


static const char *
state_name(ps5tm_cs_state_t s) {
  switch(s) {
    case PS5TM_CS_FINE:      return "fine";
    case PS5TM_CS_SUGGEST:   return "suggest";
    case PS5TM_CS_MEASURING: return "measuring";
    case PS5TM_CS_EFFECT:    return "effect";
    default:                 return "collecting";
  }
}


static void
add_stats(cJSON *o, const char *key, const ps5tm_cs_stats_t *s) {
  cJSON *x = cJSON_AddObjectToObject(o, key);
  if(!x) return;
  cJSON_AddNumberToObject(x, "n", s->n);
  cJSON_AddNumberToObject(x, "over_min_h", s->over_min_h);
  cJSON_AddNumberToObject(x, "jumps_h", s->jumps_h);
  cJSON_AddNumberToObject(x, "hot_c", s->hot_c);
  cJSON_AddNumberToObject(x, "fan_avg", s->fan_avg);
}


/* The games with what they suggest, as now; -1 when out of memory. *out is malloc'd. */
static int
evaluate(ps5tm_cs_game_t **out) {
  *out = NULL;
  ps5tm_pt_row_t *rows = NULL;
  int nrows = ps5tm_playtime_rows(&rows, CS_ROWS_MAX);
  if(nrows < 0) return -1;

  ps5tm_game_rule_t rules[PS5TM_MAX_GAME_RULES];
  ps5tm_config_lock();
  unsigned nrules = g_config.game_rule_count < PS5TM_MAX_GAME_RULES ? g_config.game_rule_count : PS5TM_MAX_GAME_RULES;
  memcpy(rules, g_config.game_rules, sizeof(rules[0]) * nrules);
  unsigned target  = g_config.target_temp_c;
  int      profile = (int)g_config.profile;
  ps5tm_config_unlock();

  ps5tm_cs_game_t *games = calloc(PS5TM_CS_MAX_GAMES, sizeof(*games));
  if(!games) { free(rows); return -1; }
  unsigned n = ps5tm_cs_eval(rows, (unsigned)nrows, rules, nrules, target, profile,
                             (int64_t)(ps5tm_now_ms() / 1000), games, PS5TM_CS_MAX_GAMES);
  free(rows);
  *out = games;
  return (int)n;
}


cJSON *
ps5tm_cs_json(void) {
  ps5tm_cs_game_t *games = NULL;
  int n = evaluate(&games);
  if(n < 0) return NULL;

  hidden_t hidden[CS_HIDDEN_MAX];
  pthread_mutex_lock(&g_hidden_lock);
  unsigned nh = hidden_read(hidden, CS_HIDDEN_MAX);
  pthread_mutex_unlock(&g_hidden_lock);

  cJSON *root = cJSON_CreateObject();
  if(!root) { free(games); return NULL; }
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddNumberToObject(root, "min_sessions", PS5TM_CS_MIN_SESSIONS);
  cJSON_AddNumberToObject(root, "min_after", PS5TM_CS_MIN_AFTER);
  cJSON_AddNumberToObject(root, "min_play_s", PS5TM_CS_MIN_PLAY_S);
  cJSON_AddNumberToObject(root, "over_pct", PS5TM_CS_OVER_PCT);
  cJSON_AddNumberToObject(root, "jumps_h", PS5TM_CS_JUMPS_H);
  cJSON_AddNumberToObject(root, "jump_pct", PS5TM_PT_JUMP_PCT);
  cJSON *list = cJSON_AddArrayToObject(root, "games");
  for(int i = 0; i < n && list; i++) {
    const ps5tm_cs_game_t *g = &games[i];
    cJSON *o = cJSON_CreateObject();
    if(!o) break;
    cJSON_AddStringToObject(o, "id", g->title);
    cJSON_AddStringToObject(o, "name", g->name[0] ? g->name : g->title);
    cJSON_AddStringToObject(o, "state", state_name(g->state));
    cJSON_AddNumberToObject(o, "n", g->n);
    cJSON_AddNumberToObject(o, "over_n", g->over_n);
    cJSON_AddNumberToObject(o, "warn_n", g->warn_n);
    if(g->has_rule) {
      cJSON *r = cJSON_AddObjectToObject(o, "rule");
      if(r) {
        cJSON_AddStringToObject(r, "profile", profile_name(g->rule_profile));
        cJSON_AddNumberToObject(r, "target_c", g->rule_target_c);
      }
      add_stats(o, "before", &g->before);
      add_stats(o, "after", &g->after);
    } else {
      add_stats(o, "now", &g->now);
    }
    if(g->state == PS5TM_CS_SUGGEST) {
      cJSON *s = cJSON_AddObjectToObject(o, "suggest");
      if(s) {
        cJSON_AddStringToObject(s, "kind", g->kind == KIND_PEAKS ? "peaks" : "jumpy");
        cJSON_AddStringToObject(s, "profile", profile_name(g->profile));
        cJSON_AddNumberToObject(s, "target_c", g->target_c);
      }
      cJSON_AddBoolToObject(o, "hidden", is_hidden(hidden, nh, g));
    }
    cJSON_AddItemToArray(list, o);
  }
  free(games);
  return root;
}


int
ps5tm_cs_hide(const char *title_id) {
  if(!title_ok(title_id)) return -EINVAL;

  ps5tm_cs_game_t *games = NULL;
  int n = evaluate(&games);
  if(n < 0) return -ENOMEM;
  int kind = KIND_NONE;
  for(int i = 0; i < n; i++)
    if(!strcmp(games[i].title, title_id) && games[i].state == PS5TM_CS_SUGGEST) kind = games[i].kind;
  free(games);
  if(kind == KIND_NONE) return -ENOENT;                 /* nothing suggested for it (any more) */

  pthread_mutex_lock(&g_hidden_lock);
  hidden_t h[CS_HIDDEN_MAX];
  unsigned nh = hidden_read(h, CS_HIDDEN_MAX), w = 0;
  for(unsigned i = 0; i < nh; i++)                      /* the game's older entry goes, its new one comes last */
    if(strcmp(h[i].title, title_id) != 0) h[w++] = h[i];
  if(w >= CS_HIDDEN_MAX) { memmove(h, h + 1, sizeof(h[0]) * (CS_HIDDEN_MAX - 1)); w--; }
  snprintf(h[w].title, sizeof(h[w].title), "%s", title_id);
  h[w].kind = kind;
  w++;

  mkdir(PS5TM_DATA_DIR, 0755);
  char tmp[sizeof(CS_HIDDEN_PATH) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", CS_HIDDEN_PATH);
  int rc = 0;
  FILE *f = fopen(tmp, "w");
  if(!f) {
    rc = -errno;
  } else {
    for(unsigned i = 0; i < w; i++) fprintf(f, "%s %d\n", h[i].title, h[i].kind);
    if(fflush(f) != 0 || ferror(f)) rc = -EIO;
    if(fclose(f) != 0 && !rc) rc = -EIO;
    if(!rc && rename(tmp, CS_HIDDEN_PATH) != 0) rc = -errno;
    if(rc) unlink(tmp);
  }
  pthread_mutex_unlock(&g_hidden_lock);
  if(!rc) PS5TM_INFO("suggest_hidden", "Profilvorschlag für Titel %s ausgeblendet.", title_id);
  return rc;
}
