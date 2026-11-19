# PS5 Cooling & System Center – Pro v1.87.0

## Highlights
- **Spielstände auf eine andere Konsole übertragen (PS5 und PS4):** Bei jedem Titel gibt es den Knopf **„Als ZIP“**. Die Konsole liest den Spielstand aus, die App packt die Dateien in eine ZIP-Datei, und auf der anderen Konsole spielst du sie ein. Eine Sicherung der App passt nur zu der Konsole, die sie gemacht hat; so geht es auch auf eine zweite.
- **Sicher eingespielt:** Der bisherige Stand wird vorher gesichert (samt Spielstand-Datenbank), jede Datei wird nach dem Schreiben zurückgelesen und geprüft, und bei einem Fehler geht alles auf den Stand davor zurück.

## Neu
### Spiele → Spielstände
- Knopf **„Als ZIP“** bei jedem Spielstand eines PS5- oder PS4-Titels. Die fertigen ZIP-Dateien stehen in der Karte „Auf eine andere Konsole übertragen“ zum Herunterladen; Löschen fragt mit dem Namen.
- **„Spielstand einspielen“:** ZIP-Datei wählen, die App zeigt Titel, Dateien und Größe, du wählst den Benutzer an dieser Konsole und drückst „Einspielen“.
- Ausgelesen und eingespielt wird nur, wenn kein Spiel läuft (auch kein pausiertes). Es läuft immer nur ein Vorgang, „Abbrechen“ räumt auf.
- **Spielstände im neueren PS5-Abbildformat** (auf der Konsole häufig) werden als klassisches Abbild angelegt; die Karte sagt das vorher.
- Die Besitzerangaben übernimmt die App aus einem Spielstand desselben Benutzers an dieser Konsole, bei PS5 und PS4. Gibt es keinen, wird trotzdem eingespielt, und die App warnt, dass das Spiel den Spielstand vielleicht nicht annimmt.
- Oberfläche in 6 Sprachen, Handbuch in 7. Neue Routen für Auslesen und Einspielen stehen in `docs/API.md`.

## Hinweise
- **An der Konsole geprüft (Firmware 12.00, 19.11.2026):** Arcade Game Zone von der Pro auf die Slim übertragen (PS5, Spielstand im neueren Abbildformat): Dateien byte-gleich, das Spiel hat Einstellungen und Rekorde übernommen. Rundlauf auf einer Konsole mit Arkanoid (PS5) und Star Wars Racer Revenge (PS4): erneut ausgelesen, alle Dateien identisch. Metal Slug Attack Reloaded (PS4) von der Pro auf die Slim übertragen: Dateien identisch.
- **Noch nicht geprüft:** der Start eines PS4-Spiels nach dem Einspielen; Firmware ab 13.x (beide Testkonsolen laufen mit 12.00); ZIP-Dateien von Garlic SaveMgr (nur im Host-Test, dessen Format folgt die ZIP-Datei); das Übertragen in der Vollbild-App (es gibt es nur im Browser, das Herunterladen und Hochladen gehören nicht auf den Controller).
- Die ZIP-Datei ist nicht verschlüsselt: Behandle sie wie den Spielstand selbst. Nichts wird ins Internet geschickt.
- Die App ist durch das eingebettete SQLite (für die Spielstand-Datenbank) rund 0,8 MB größer.
- **Im Payload Manager von itsPLK bestätigt (19.11.2026):** Die Quelle `latest/download/payload-manager.json` wird angenommen, er folgt der Weiterleitung.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Alles andere ist wie in v1.86.7.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.87.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: als Quelle `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge, **drakmor** für ShadowMountPlus sowie **itsPLK** für den Payload Manager. Neu dabei: **earthonion** für Garlic SaveMgr, das Vorbild für das Übertragen von Spielständen.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.86.7...v1.87.0
