# np-fake-signin

**Version:** 1.3  
**Autor:** earthonion  
**Quelle:** inoffiziell verbreitet, kein öffentliches Repository / keine Lizenz angegeben  
**Datei:** `np-fake-signin_v1.3.elf` (PS5 ELF, 117 KiB)

## Verwendung

Die fertig gebaute ELF wird als Blob eingebettet und bei Klick auf „Fake-Anmeldung schreiben"
über den lokalen ELF-Lader (127.0.0.1:9021) injiziert. Die ELF liest den aktiven Vordergrund-
Benutzer selbst aus und schreibt auth.dat, config.dat sowie die Registrierungsschlüssel.
Sie setzt voraus, dass das Offline-Konto bereits aktiviert wurde (via offact/account_activate).

## Lizenz

Kein Lizenztext öffentlich verfügbar. Die ELF wird ausschließlich als binäres Blob
eingebettet; kein Quellcode wurde übernommen. Wissen über Dateiformat und Registrierungs-
schlüssel wurde eigenständig re-implementiert (src/account.c).
