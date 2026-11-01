# PS5 Cooling & System Center – Pro v1.75.2

## Highlights
- **Schnellerer, sauberer Start** – die App versucht beim Start nicht mehr, die alte Medien-Kachel anzulegen

## Fehlerbehebungen
- Bis 1.75.1 versuchte die App mit jeder neuen Version einmal, die frühere Medien-Kachel (PSCC69690) anzulegen – obwohl sie nicht mehr gebraucht wird. Wo das gelang, tauchte die alte Kachel wieder auf, und die Lüfterregelung startete bis zu 20 Sekunden später. Beides ist vorbei.

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) starten die App wie bisher selbst. Wer sie nutzt, kopiert die Ordner aus diesem Release neu, damit sie die neue Fassung starten.
- Eine noch vorhandene alte Medien-Kachel kann man in der Konsole löschen; sie kommt nicht wieder.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.2.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.1...v1.75.2
