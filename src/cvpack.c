/* The choice to pack a game while an image is written: see cvpack.h. */
#include <stdio.h>
#include <string.h>

#include "cvpack.h"
#include "third_party/cJSON.h"

int
ps5tm_cvpack_parse(const cJSON *body, ps5tm_cvpack_t *p, char *err, size_t err_len) {
  p->kind = p->level = p->runtime = 0;
  const cJSON *jk = body ? cJSON_GetObjectItem(body, "pack") : NULL;
  if(!jk || cJSON_IsNull(jk)) return 0;
  if(!cJSON_IsString(jk)) { snprintf(err, err_len, "Unbekannte Art zu packen."); return 400; }
  const char *k = jk->valuestring;
  if(!*k || !strcmp(k, "none")) return 0;
  if(!strcmp(k, "lz4")) p->kind = 1;
  else if(!strcmp(k, "zstd")) p->kind = 2;
  else { snprintf(err, err_len, "Unbekannte Art zu packen."); return 400; }
  int lvl = p->kind == 1 ? 9 : 5;
  const cJSON *jl = cJSON_GetObjectItem(body, "pack_level");
  if(jl && !cJSON_IsNull(jl)) {
    if(!cJSON_IsNumber(jl) || jl->valuedouble < 0 || jl->valuedouble > 100 || (double)(int)jl->valuedouble != jl->valuedouble) {
      p->kind = 0;
      snprintf(err, err_len, "Die Stufe zum Packen muss eine ganze Zahl sein.");
      return 400;
    }
    lvl = (int)jl->valuedouble;
    if(p->kind == 1 && !(lvl == 0 || (lvl >= 3 && lvl <= 12))) { p->kind = 0; snprintf(err, err_len, "Bei LZ4 gibt es die Stufen 0 und 3 bis 12."); return 400; }
    if(p->kind == 2 && !(lvl >= 1 && lvl <= 22)) { p->kind = 0; snprintf(err, err_len, "Bei zstd gibt es die Stufen 1 bis 22."); return 400; }
  }
  p->level = lvl;
  const cJSON *jr = cJSON_GetObjectItem(body, "pack_runtime");
  if(jr && !cJSON_IsNull(jr)) {
    if(!cJSON_IsString(jr) || (strcmp(jr->valuestring, "ng") && strcmp(jr->valuestring, "ampr_emu"))) {
      p->kind = 0;
      snprintf(err, err_len, "Unbekannte Laufzeit zum Packen.");
      return 400;
    }
    if(!strcmp(jr->valuestring, "ampr_emu")) {
      if(p->kind == 2) { p->kind = 0; snprintf(err, err_len, "zstd-Packs liest nur AMPR EMU NG."); return 400; }
      p->runtime = 1;
    }
  }
  return 0;
}
