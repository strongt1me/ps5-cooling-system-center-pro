# PS5 Cooling & System Center – Pro v1.86.0

## Highlights
- **Neue AMPR-EMU-NG-Fassung per Knopf:** Im Fenster „APR & LZ4“ fragt der Knopf **Auf neue AMPR-EMU-NG-Fassung prüfen und laden** GitHub nach der neuesten stabilen Veröffentlichung von ampr-emu-ng. Ist sie neuer als alles Angebotene, lädt die App sie (Prüfsumme gegen `SHA256SUMS`), stellt sie ganz nach oben und lässt die älteste Fassung entfallen. Browser-App und Vollbild-App.
- **AMPR EMU NG v0.4.1 und Kraken Mount v0.2.0 eingebaut:** Zur Auswahl stehen **0.4.1 und 0.3.5** (die fehlerhafte 0.4.0 ist nicht dabei). Kraken Mount v0.2.0 läuft weiter neben ShadowMountPlus, FPKGMount meldet also auch dann Spiele an.
- **Knopf „Anleitung“ beim Format FPKG:** zeigt in Browser-App und Vollbild-App, wie Dump Forge bei der reinen ELF-Version eingerichtet wird.

## Neu
### AMPR EMU NG
- Geladen wird nur auf Knopfdruck, nie von selbst im Hintergrund, und nur eine **stabile** Veröffentlichung (eine Beta nimmt die App nie).
- Die App prüft die Prüfsumme gegen die `SHA256SUMS` der Veröffentlichung, den SELF-Kopf und die Kennung `ampr-emu-ng <Version>`; beide Dateien (normal und Debug) werden erst abgelegt, wenn beide stimmen. Schlägt das Ablegen der zweiten fehl, wird die erste wieder entfernt.
- Es stehen immer **zwei Fassungen** zur Auswahl. Eine ältere fällt aus der Liste; ihre von der App abgelegten Dateien werden gelöscht, selbst abgelegte bleiben liegen. Kaputte Dateien im Ordner nehmen keinen Platz einer guten Fassung ein.
- Ist die neueste Fassung schon da, sagt die App es.

### Fremdbausteine
- **AMPR EMU NG v0.4.1** (quer3q): neue Dateiindex-Ansicht ohne die feste 48-MiB-Grenze, Korrekturen für große Spiele (Final Fantasy XVI, Demon's Souls, Returnal), Spider-Man: Miles Morales, Spielstand-Einhängen, Horizon Forbidden West und Ordner-Handles. „Passt der Index in AMPR EMU NG?“ rechnet jetzt für 0.4.1 (80 MiB).
- **Kraken Mount v0.2.0** (quer3q): alle FPKG-Dateien auch mit Lücken, Oberfläche in 11 Sprachen, verschobene Spiele werden neu verknüpft statt neu gebaut, neue Spiele melden sich auch dort an, wo nur elfldr läuft. Die Seite FPKGMount zeigt „wartet auf ShadowMountPlus“ bzw. „wartet auf kstuff“ an.

## Verbesserungen
- **Vollbild-App:** Die Prüfung auf eine neue NG-Fassung wartet bis zu 190 Sekunden auf die Antwort der App (vorher 30).
- Handbuch und FAQ in sieben Sprachen, Übersetzungen der neuen Texte in sechs Sprachen.

## Hinweise
- Kraken Mount v0.2.0 wartet von sich aus, bis ShadowMountPlus beendet ist. Damit FPKGMount wie bisher daneben arbeitet, ändert die App in der eingebetteten ELF (und in einer von GitHub geladenen) ein Byte des Prozessnamens `shadowmountplus.elf`; das Warten auf kstuff bleibt. Das ist eine veränderte Kopie unter GPL-3.0 und steht in `ORIGIN.md` und im Handbuch.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- Getestet an der Konsole (PS5 Pro, ELF-Stand `24d7297`, auch die Kacheln): Knopf, Laden, Aufräumen einer dritten älteren Fassung. Die Release-ELF trägt nur die neue Versionsnummer.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.85.1...v1.86.0
