# PS5 Cooling & System Center – Pro v1.86.4

## Highlights
- **Keine inhaltlichen Änderungen gegenüber v1.86.3.** Dieses Release trägt nur eine neue Versionsnummer; Funktionen und Verhalten sind dieselben.

## Hinweise
- Stand von v1.86.3, der in diesem Release steckt: Im Reiter Disc startet „Starten“ das Spiel und dumpt es gleich mit (Meldungen „Dump gestartet / fertig / gescheitert“), der Ordner FPKG-Games ist als richtiger Ort für FPKG herausgestellt, die Prüfung auf eine neue AMPR-EMU-NG-Fassung zeigt sofort, dass sie läuft, und ein Spiel, das bei Kraken Mount und bei ShadowMountPlus liegt, wird nach dem Beenden bei ShadowMountPlus ausgehängt.
- An der Konsole gesehen: der Disc-Dump mit „Starten“. Das Aushängen nach dem Beenden ist nur im Host-Test geprüft (`test/fpkgdup`), nicht an der Konsole.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.4.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.86.3...v1.86.4
