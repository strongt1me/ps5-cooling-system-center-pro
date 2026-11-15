# PS5 Cooling & System Center – Pro v1.86.2

## Highlights
- **Alte NG-Fassungen verschwinden von selbst:** Eine AMPR-EMU-NG-Datei im Ordner `AMPR_EMU`, die eine frühere App-Fassung mitgeliefert hat und die diese nicht mehr mitliefert (zum Beispiel die fehlerhafte 0.4.0), wird nicht mehr angeboten und beim Start der App gelöscht. Die zwei Fassungen zur Auswahl sind danach wie gedacht **0.4.1 und 0.3.5**.

## Verbesserungen
- **Erkennung an der Prüfsumme:** Gelöscht werden nur Dateien, deren Inhalt genau einer früher mitgelieferten Fassung (0.3.2 bis 0.4.0) entspricht. Eine selbst abgelegte Datei mit anderem Inhalt bleibt liegen. Im Protokoll steht, wie viele Dateien entfernt wurden.
- Spiele, in die eine dieser Fassungen eingebaut ist, werden weiter richtig erkannt („eingebaut: AMPR EMU NG 0.4.0“).

## Hinweise
- Alles andere ist unverändert gegenüber v1.86.1. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.2.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.86.1...v1.86.2
