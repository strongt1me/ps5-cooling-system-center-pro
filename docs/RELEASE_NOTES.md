# PS5 Cooling & System Center – Pro v1.76.4

## Highlights
- **Hintergrundbild im Startmenü** – die Kacheln `FAKE86921` und `FAKE86922` zeigen beim Anwählen und Starten wieder ihr großes Bild
- **Hochladen in einen vergebenen Namen** – keine Verbindungsabbrüche mehr, dafür die Rückfrage Ersetzen / Beide behalten / Überspringen

## Neu

### Kacheln
- Die Konsole liest das Bild, das beim Anwählen (`pic0`) und beim Starten (`pic1`) einer Kachel erscheint, nur als **BC7-DDS in 3840×2160**; ein PNG daneben wird nicht gelesen. Die beiden Kacheln enthalten jetzt `pic0.dds` und `pic1.dds`.
- Die Kachel `PSCC69690`, die die ELF selbst anlegt, bleibt bei PNG: Ein 8-MB-Bild passt nicht in die ELF.

### Dateimanager
- Beim Hochladen einer Datei, deren Namen es im Ziel schon gibt, fragt dieselbe Rückfrage wie beim Kopieren: **Ersetzen**, **Beide behalten** (`Name (2).ext`) oder **Überspringen**.

## Fehlerbehebungen
- Eine große Datei unter einem schon vergebenen Namen hochzuladen endete mit „Die Verbindung brach ab“, weil der Server antwortete, bevor er die Datei empfangen hatte. Er liest die Übertragung jetzt zu Ende und meldet den Fehler lesbar.

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren. Die Zip-Dateien sind größer geworden (je rund 17 MB mehr), wegen der Hintergrundbilder.
- **Bereits angemeldete Kacheln:** Die Konsole kopiert die Bilder nur bei der ersten Anmeldung. Damit das Bild bei einer schon vorhandenen Kachel erscheint, `pic0.dds` und `pic1.dds` zusätzlich nach `/user/app/<ID>/sce_sys/` und `/user/appmeta/<ID>/` kopieren, oder die Kachel entfernen und neu anlegen lassen.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.4.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.3...v1.76.4
