/* PS5 Cooling & System Center - Pro - the one list of the app's jobs that do not run beside each other.
 *
 * Every job module used to keep a copy of this list (filemgr, gamecopy, gameconvert, gamemove, gamedelete, imgjob, amprindex, pkginstall,
 * pkglive, pkgsplit, savebackup, library, usbeject): a new job had to be added to all of them by hand, and the ones that were forgotten (uploads,
 * the eject of a drive, the disc dump) let two jobs write into the same folder (audit 11.11.2026). A module now asks
 * ps5tm_jobs_busy_except() with its own id; a new job is added here, in k_jobs, and nowhere else. */

#include <stdio.h>

#include "ps5tm.h"

typedef struct {
  int         (*busy)(void);
  const char *what;              /* the sentence that names it, for the message of a refused start */
  int         reads_only;        /* only reads a drive: stops an eject, but does not stop another job from starting */
} job_t;

/* The cheap local checks first; gamemove asks ShadowMountPlus over HTTP (a short timeout) and comes last. */
static const job_t k_jobs[PS5TM_JOB_COUNT] = {
  [PS5TM_JOB_SAVES]       = { ps5tm_saves_busy,       "Es läuft ein Vorgang mit den Spielständen", 0 },
  [PS5TM_JOB_PKGSPLIT]    = { ps5tm_pkgsplit_busy,    "Es wird ein Paket geteilt", 0 },
  [PS5TM_JOB_PKGINST]     = { ps5tm_pkginst_busy,     "Es wird ein Paket installiert", 0 },
  [PS5TM_JOB_PKGSCAN]     = { ps5tm_pkgscan_busy,     "Die Pakete werden gerade gesucht", 1 },
  [PS5TM_JOB_GAMEDELETE]  = { ps5tm_gamedelete_busy,  "Es wird ein Spiel oder eine Sicherung gelöscht", 0 },
  [PS5TM_JOB_FILEMGR]     = { ps5tm_filemgr_busy,     "Der Dateimanager arbeitet (Kopieren, Verschieben, Löschen oder Hochladen)", 0 },
  [PS5TM_JOB_AMPR]        = { ps5tm_ampr_busy,        "Es wird ein AMPR-Index gebaut", 0 },
  [PS5TM_JOB_IMGJOB]      = { ps5tm_imgjob_busy,      "Es wird ein Abbild bearbeitet", 0 },
  [PS5TM_JOB_DISC]        = { ps5tm_disc_busy,        "Es wird ein Abbild der Disc angelegt", 0 },
  [PS5TM_JOB_USBEJECT]    = { ps5tm_usbeject_busy,    "Ein Laufwerk wird gerade ausgeworfen", 0 },
  [PS5TM_JOB_GAMECONVERT] = { ps5tm_gameconvert_busy, "Es läuft eine Konvertierung", 0 },
  [PS5TM_JOB_GAMECOPY]    = { ps5tm_gamecopy_busy,    "Es läuft eine Kopie oder ein Dump", 0 },
  [PS5TM_JOB_GAMEMOVE]    = { ps5tm_gamemove_busy,    "ShadowMountPlus verschiebt oder entpackt gerade ein Spiel für die App", 0 },
};

int
ps5tm_jobs_busy_except(int self, int flags, char *why, size_t n) {
  for(int i = 0; i < PS5TM_JOB_COUNT; i++) {
    if(i == self || !k_jobs[i].busy) continue;
    if(k_jobs[i].reads_only && !(flags & PS5TM_JOBS_READERS)) continue;
    if(i == PS5TM_JOB_GAMEMOVE && (flags & PS5TM_JOBS_NO_SMP)) {
      if(ps5tm_gamemove_busy_local()) {            /* ShadowMountPlus is not asked, but what the app itself holds still counts */
        if(why && n) snprintf(why, n, "%s", k_jobs[i].what);
        return 1;
      }
      continue;
    }
    if(k_jobs[i].busy()) {
      if(why && n) snprintf(why, n, "%s", k_jobs[i].what);
      return 1;
    }
  }
  return 0;
}
