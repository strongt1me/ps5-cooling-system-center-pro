# PS5 Cooling & System Center – Pro v1.76.0

## Highlights
- **Offline-Konto und Fake-Anmeldung** – neuer Bereich im Profil-Reiter: Konto aktivieren, Fake-Anmeldung per np-fake-signin v1.3 (earthonion) injizieren und alles zurücksetzen

## Neu

### Profil
- **Offline-Konto aktivieren:** setzt Name, Account-ID und Flags über die Registry (OffAct-Verfahren, GPL-3.0, John Törnblom); Statusanzeige zeigt, ob das Konto aktiv ist.
- **Fake-Anmeldung schreiben:** injiziert die np-fake-signin v1.3 ELF an den lokalen ELF-Lader (127.0.0.1:9021). Die ELF liest den aktiven Benutzer selbst aus und schreibt auth.dat, config.dat sowie alle Registrierungseinträge — keine Eingaben nötig.
- **Alles zurücksetzen:** löscht auth.dat/config.dat und leert alle Registry-Schlüssel beider Funktionen.

## Fehlerbehebungen
- Lüfter-Kachel wurde bei Fensterbreiten 981–1259 px abgeschnitten.
- Farbkontrast der Payload-Store-Symbole im Design „Hell" zu gering.
- 44 Protokollmeldungen und 38 sichtbare Texte blieben in anderen Sprachen deutsch.
- PS4 von USB: Content-IDs mit `"` oder `\` machten Antworten ungültig; zu kurze IDs konnten Ordner löschen.
- Kernel-Log speichern: NUL-Byte statt Zeilenumbruch bei exakter Pufferfüllung.

## Hinweise
- Fake-Anmeldung setzt voraus, dass das Offline-Konto bereits aktiviert wurde und elfldr auf Port 9021 läuft.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits" bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.7...v1.76.0
