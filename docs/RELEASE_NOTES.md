# PS5 Cooling & System Center – Pro v1.76.6

## Highlights
- **Dateimanager:** Beim Kopieren und Verschieben stehen jetzt die **Übertragungsrate** (MB/s) und die **geschätzte Restzeit** da
- **Versionsnummer links:** direkt unter „FAQ“ in der Seitenleiste, ohne nach unten zu scrollen

## Neu

### Dateimanager
- Die Auftragsanzeige zeigt beim Kopieren und Verschieben z. B. `1,2 GB von 3,2 GB · 82 MB/s · noch ca. 12 Min.`, im Browser und in der Vollbild-App (dort steht der Dateiname in einer eigenen Zeile darunter).
- Die App misst beides selbst: einmal pro Sekunde aus den schon übertragenen Bytes, leicht geglättet. Stockt die Übertragung länger als vier Sekunden (langsamer Stick, große Datei wird geschrieben), sinkt die Anzeige auf das, was wirklich ankommt. In den ersten Sekunden und beim Zählen und Löschen steht nichts da.
- „noch ca. …“ ist in allen sechs Sprachen übersetzt.

### Browser-Oberfläche
- Die Versionsnummer („Version 1.76.6“) steht in der Seitenleiste unter „FAQ“ statt in einer Fußzeile ganz unten. Über dem Lüfterbild liegt sie auf einer dunklen Unterlage; in der schmalen Leiste oben (Handy) steht sie als schlichter Text unter den Reitern.

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren, sonst fehlt Rate und Restzeit in der Vollbild-App.
- Für Entwickler: `GET /api/v1/files/job` nennt neu `rate_bps` (Bytes pro Sekunde) und `eta_s` (Sekunden bis zum Ende, 0 = unbekannt).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.6.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.5...v1.76.6
