# PS5 Cooling & System Center – Pro v1.76.9

## Highlights
- **Dateimanager:** Die Auftragsanzeige nennt, **wie viele Dateien/Ordner** es sind und **von wo nach wo** es geht
- Beides in Browser-App und Vollbild-App, in allen sechs Sprachen

## Neu

### Dateimanager
- Beim Kopieren, Verschieben und Löschen steht jetzt z. B. **„Kopiere 1 von 2 Dateien/Ordnern · 37 %“** und darunter **„von /mnt/usb0/neu nach /mnt/ext1“**. Bei einem Eintrag heißt es „Kopiere 1 Datei/Ordner“; kommen die Einträge aus mehreren Ordnern, steht nur „Ziel: …“, beim Löschen „Ordner: …“.
- Im Browser zeigt die Mengenzeile zusätzlich, wie viele der enthaltenen Dateien schon fertig sind („1 von 3 Dateien“).
- Browser und Vollbild-App zeigen dasselbe; die Texte sind in allen sechs Sprachen übersetzt. Die Auftragskarte der Vollbild-App ist dafür etwas höher.
- Für Entwickler: `GET /api/v1/files/job` nennt neu `from` (der Ordner, aus dem alle Einträge kommen; leer bei gemischten Quellen).

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren; die Vollbild-App braucht die neuen Sprachdateien und die geänderte Auftragskarte.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.9.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.8...v1.76.9
