/* Wrapper that silences strict warnings from the upstream cJSON sources. */

#pragma clang diagnostic ignored "-Wunreachable-code-generic-assoc"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wint-conversion"

/* Request bodies are parsed on connection threads with a modest stack: 1000 nested arrays (the upstream default) would run it
   out and take the whole app down (review 15.10.2026). Nothing the app reads nests deeper than a handful of levels. */
#define CJSON_NESTING_LIMIT 64

#include "cJSON_real.c"
