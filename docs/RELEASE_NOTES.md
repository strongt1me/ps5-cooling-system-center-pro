# PS5 Cooling & System Center – Pro v1.75.6

## Highlights
- **Das Protokoll übersteht jetzt einen Neustart** – die App schreibt jedes Ereignis zusätzlich in `/data/PS5-Cooling-Center/app.log`, damit nach einem Neustart oder Absturz der Konsole nachzulesen ist, was vorher geschah

## Neu
- Die Datei `app.log` hat denselben Aufbau wie der Export im Reiter „Protokoll“: Zeit der Konsole, Stufe, Kennung, Text.
- Die App schreibt alle 2 Sekunden, was neu ist, Warnungen und Fehler sofort, und sichert die Datei jedes Mal auf die Platte. Der Lüfter wartet dabei nie auf die Platte.
- Jeder Start der App beginnt in der Datei mit einer Zeile `===== … gestartet =====`.
- Ab 1 MB wird die alte Datei zu `app.old.log` und eine neue beginnt (zusammen höchstens etwa 2 MB).
- Kamen Einträge schneller, als die App sie wegschreiben konnte, steht in der Datei, wie viele fehlen; im Kernel-Log stehen sie trotzdem.
- Lesen kannst du die Datei im Dateimanager der App oder per FTP.
- Das Handbuch nennt die Datei in allen sieben Sprachen.

## Hinweise
- Die ersten Zeilen nach dem Start stehen in Weltzeit (mit `Z` hinter der Uhrzeit), bis die App die Zeitzone der Konsole kennt; danach in Ortszeit.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.6.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.5...v1.75.6
