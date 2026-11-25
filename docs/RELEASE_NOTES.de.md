# PS5 Cooling & System Center – Pro v1.96.1

## Highlights
- **Alte Seite, neue App:** Kommt eine neue App, während die Weboberfläche offen ist, sagt die Seite das jetzt: „Neue Version … läuft“ mit dem Knopf **Seite neu laden**. Vorher war nur die Versionsanzeige neu, neue Knöpfe und Felder fehlten – deshalb fand ein Nutzer die Suche im Dateimanager aus 1.96.0 nicht.
- **Alte Kachel, neue App:** Die Vollbild-App zeigt die Version der Kachel in Gelb neben der App-Version und einmal die Meldung „Kachel aus dem Release neu kopieren“, wenn die Kachel älter ist als die laufende App.

## Neu
- **Weboberfläche:** Die Seite merkt sich die App-Version beim ersten Laden. Meldet die App eine andere, erscheint der Hinweis oben auf jeder Seite. Er bleibt, bis neu geladen wird; selbst lädt die Seite nicht neu, damit keine Eingabe verloren geht.
- **Vollbild-App:** Die Kachel liest die Version der App, die sie mitbringt, und vergleicht sie mit der laufenden. Eine neuere Kachel ersetzt eine ältere App wie bisher selbst.
- Alles aus v1.96.0 (Suche im Dateimanager, Spieltitel, Kraken Mount 0.4.0) ist enthalten – siehe deren Release-Notizen.

## Hinweise
- **An der Konsole erprobt:** Dieser Stand läuft auf der Pro und der Slim (mit der Versionsnummer 1.96.0, gleicher Code). Der Hinweis selbst erscheint nur, wenn sich die Versionsnummer ändert; er wurde deshalb in der Vorführung (Web, 1.95.1 → 1.96.0, auch auf Englisch) und als PC-Bild (Vollbild-App) geprüft. An der Konsole zeigt er sich mit diesem Release zum ersten Mal.
- Alle 46 Host-Tests, 396 Tests der Vollbild-App und die Wörterbuchprüfung bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Danach eine offene Weboberfläche einmal neu laden.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.96.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.96.0...v1.96.1
