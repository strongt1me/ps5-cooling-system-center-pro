# PS5 Cooling & System Center – Pro v1.95.1

## Highlights
- **Kraken Mount ist keine Beta mehr:** Der Reiter FPKGMount (Browser und Vollbild-App) nennt Kraken Mount nicht mehr „noch eine Beta“, und das Handbuch sagt stattdessen, was die Readme von Kraken Mount 0.3.2 empfiehlt: Für große, anspruchsvolle Spiele laufen zstd-Pakete besser als eine `.pkg`.

## Neu
- Nur Text, in allen sieben Sprachen (Hinweis im Reiter, Wörterbücher, Handbuch). Alles andere ist wie in v1.95.0 – siehe deren Release-Notizen zu Kraken Mount 0.3.2, den sichereren NTFS-Laufwerken, der kompletten Prüfung der App und der Korrektur bei „Ersetzen“.

## Hinweise
- **An der Konsole erprobt:** Dieser Stand läuft auf der Pro und der Slim (mit der Versionsnummer 1.95.0, gleicher Code).
- 392 Tests der Vollbild-App und die Wörterbuchprüfung bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.95.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.95.0...v1.95.1
