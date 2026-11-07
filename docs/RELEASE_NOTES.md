# PS5 Cooling & System Center – Pro v1.77.1

## Highlights
- **Die Kacheln ersetzen eine ältere laufende App selbst** – wer neue Kachel-Ordner kopiert, bekommt beim Antippen die neue Version, auch ohne Neustart der Konsole

## Neu

### Kacheln aktualisieren die laufende App
- Bisher schickten beide Kacheln (Vollbild **FAKE86921** und Browser **FAKE86922**) ihre eingebaute App nur, wenn gar keine lief. Wer neue Kachel-Ordner über die alten kopierte, ohne die Konsole neu zu starten, behielt die **alte App** und sah zum Beispiel weiter „1.76.9“ statt „1.77.0“.
- Jetzt vergleichen die Kacheln die Versionen. Eine **ältere** laufende App wird durch die eingebaute ersetzt (die neue beendet die alte beim Start; die Kachel wartet bis zu 30 Sekunden auf sie). Eine **gleiche oder neuere** App bleibt.
- **Läuft gerade ein Auftrag** (Packen, Kopieren, Installieren, Spielstände …), wird nichts ersetzt, denn die Ersetzung würde ihn abbrechen. Die Kachel meldet „Eine neuere App liegt bereit“ – nach dem Auftrag noch einmal öffnen.
- Geprüft am PC (Einheitstests und ein Durchlauf der Browser-Kachel gegen nachgebaute App und Loader); an der Konsole noch nicht erprobt.

## Hinweise
- **Wer von 1.77.0 oder älter kommt, muss einmal von Hand aktualisieren:** die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) oder die Konsole neu starten und dann die Kachel öffnen. Die alten Kacheln können noch nicht ersetzen; ab dieser Version geht es von allein.
- Die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- Sonst unverändert gegenüber 1.77.0 (zstd-Pakete, AMPR EMU NG 0.3.2, Löschen mit Rückfrage). Das Packen und Spielen mit zstd ist an der Konsole noch nicht erprobt.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.77.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Danke auch dem Nutzer, der die alte Versionsanzeige gemeldet hat.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.77.0...v1.77.1
