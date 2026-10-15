/* The words for a read or write that failed (05.10.2026).
 *
 * A USB drive on a bad cable, a loose port or too little power drops off the bus in the middle of a write:
 * the console unmounts it, and from then on every access fails with ENXIO. Its text, "Device not configured",
 * says nothing to a person; four conversions ended with exactly that on 05.10.2026, and the cause was a cable
 * (the kernel log showed the drive detach and attach again). So ENXIO and ENODEV get a sentence of their own:
 * what happened, what to try first, and that the console is best restarted when things go wrong afterwards — a
 * drive that left in the middle of a transfer can leave the system around it in an odd state, and what the person
 * notices is then that no game starts any more.
 *
 * Header only (static inline): the converters (conv_*.c, checkfile.c) are built on the host without the rest of
 * the app, and this adds no file to link. Everything but the cases below is strerror() as before, so a call site
 * can swap one for the other without looking at which errno it is.
 *
 * The longest message built from this text is the job's error field; those buffers are sized for it
 * (gameconvert.c, gamecopy.c, savebackup.c, pkgsplit.c). A log line holds 383 bytes and may cut it off after the
 * first sentence, which is the one that names the system's own words. */
#ifndef PS5TM_IOERR_H
#define PS5TM_IOERR_H

#include <errno.h>
#include <string.h>

#define PS5TM_IO_LOST_TEXT                                                                           \
  "Der Datenträger hat sich abgemeldet (Device not configured). Meist ist das USB-Kabel, der "     \
  "Anschluss oder die Stromversorgung schuld: ein anderes, kurzes Kabel direkt an der Konsole "     \
  "probieren. Treten danach Störungen auf oder startet kein Spiel mehr, die Konsole neu starten."

#define PS5TM_IO_FAULT_TEXT                                                                          \
  "Der Datenträger meldet einen Lese- oder Schreibfehler (Input/output error). Ist er noch "       \
  "richtig angeschlossen und in Ordnung?"

/* The most bytes either text takes, with its end — what a buffer that is to hold one of them plus a label and a
   path has to leave room for. */
#define PS5TM_IO_TEXT_MAX 320

_Static_assert(sizeof(PS5TM_IO_LOST_TEXT) <= PS5TM_IO_TEXT_MAX && sizeof(PS5TM_IO_FAULT_TEXT) <= PS5TM_IO_TEXT_MAX,
               "an I/O error text no longer fits the room the job buffers leave for it");

/* 1 when e says the drive itself is gone (not merely that one access failed). */
static inline int
ps5tm_io_lost(int e) {
  return e == ENXIO || e == ENODEV;
}

static inline const char *
ps5tm_io_strerror(int e) {
  if(ps5tm_io_lost(e)) return PS5TM_IO_LOST_TEXT;
  if(e == EIO) return PS5TM_IO_FAULT_TEXT;
  /* German words for the errors a person meets most (review 15.10.2026: "No space left on device" reached the German page);
     the system's own words stay in brackets, so that a report can still be looked up. */
  switch(e) {
  case ENOSPC:       return "Auf dem Laufwerk ist kein Platz mehr frei (No space left on device).";
  case EDQUOT:       return "Der Speicherplatz dafür ist aufgebraucht (Disc quota exceeded).";
  case EROFS:        return "Das Laufwerk ist schreibgeschützt eingehängt (Read-only file system).";
  case EACCES:       return "Keine Berechtigung für diese Datei oder diesen Ordner (Permission denied).";
  case EPERM:        return "Das ist hier nicht erlaubt (Operation not permitted).";
  case EEXIST:       return "Eine Datei oder ein Ordner dieses Namens gibt es schon (File exists).";
  case ENOENT:       return "Die Datei oder der Ordner ist nicht (mehr) da (No such file or directory).";
  case ENOTDIR:      return "Ein Teil des Pfades ist kein Ordner (Not a directory).";
  case EISDIR:       return "Das ist ein Ordner, keine Datei (Is a directory).";
  case ENOTEMPTY:    return "Der Ordner ist nicht leer (Directory not empty).";
  case ENAMETOOLONG: return "Der Name oder Pfad ist zu lang (File name too long).";
  case EFBIG:        return "Die Datei ist zu groß für dieses Laufwerk; FAT32 nimmt keine Datei ab 4 GiB (File too large).";
  case EMFILE:
  case ENFILE:       return "Zu viele offene Dateien; bitte gleich noch einmal versuchen (Too many open files).";
  case ENOMEM:       return "Zu wenig Arbeitsspeicher frei (Cannot allocate memory).";
  case EXDEV:        return "Das geht nicht über zwei Laufwerke hinweg (Cross-device link).";
  case EBUSY:        return "Die Datei oder das Laufwerk ist gerade in Benutzung (Device busy).";
  case ETIMEDOUT:    return "Es kam keine Antwort rechtzeitig (Operation timed out).";
  case ECANCELED:    return "Abgebrochen.";
  case EINVAL:       return "Ein ungültiger Wert (Invalid argument).";
  default:           return strerror(e);
  }
}

#endif
