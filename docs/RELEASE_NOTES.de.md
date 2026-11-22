# PS5 Cooling & System Center – Pro v1.91.0

## Highlights
- **PS4Mount-Spiele lassen sich löschen:** Das Spiel-Löschen (Browser und Vollbild) erkennt jetzt ein PS4-Spiel, das PS4Mount von einem USB-Laufwerk einhängt, und gibt es an „Entfernen“ von PS4Mount weiter: PS4Mount hängt es zuerst aus, dann deinstalliert es die Konsole. Die PKG-Dateien bleiben auf dem Laufwerk, und „Kachel wieder anlegen“ auf der Seite PS4Mount holt das Spiel zurück. Bisher lehnte das Löschen solche Spiele ab, oder die Konsole lehnte mit dem Fehler 0x80AF5002 ab. An der Slim erprobt.
- **Dump Forge 0.5.7 steckt in der App** (bisher 0.5.6): Der eigene AMPR-Index eines Spiels, dessen Tabelle an einer 8-Byte-Grenze beginnt (zum Beispiel Crisis Core), gilt nicht mehr als beschädigt; er bleibt beim Konvertieren erhalten. Das Auftragsprotokoll nennt den Grund, wenn ein Index beschädigt ist, und `.ffpfsc`-Abbilder, deren Datei am letzten Datenbyte endet, lassen sich lesen. An der Slim erprobt: ein Debug-FPKG aus einem 1,74-GB-Spiel, in 94 Sekunden gebaut und gründlich geprüft.

## Neu
### Spiele
- **PS4Mount-Spiel löschen:** Das Löschfenster nennt die Art „Spiel von PS4Mount“ und sagt, dass die PKG-Dateien auf dem USB-Laufwerk liegen bleiben und PS4Mount das Spiel nicht von selbst wieder aufnimmt. Das geht auch, wenn PS4Mount ausgeschaltet ist oder die PKG schon gelöscht wurde. Ist PS4Mount gerade beschäftigt, wartet das Löschen bis zu 2 Minuten.
- **„Entfernen“ von PS4Mount** meldet „Die PKG bleibt auf dem USB-Laufwerk“ nur noch, wenn sie wirklich noch da ist.

### Konvertieren
- **Dump Forge 0.5.7:** Die Schnittstelle, die die App benutzt, die Arbeitsschritte, die Kompressionsstufen, die Namensgrenze (63 Bytes) und das mitgebrachte AMPR EMU NG (0.4.1) sind gleich geblieben. Zum Packen beim Konvertieren braucht die App weiter Dump Forge 0.5.6 oder neuer.
- **Anleitung im Konvertieren-Fenster:** In der Vollbild-App zeigt „Weg 1“ („Die App lädt eine neuere Fassung selbst“) jetzt auf die eigene Zeile „Neue Fassung laden“ direkt über „Anleitung“, statt in den Browser zu schicken. In beiden Oberflächen hatte ein Satz Subjekt und Objekt vertauscht („Dump Forge startet die App …“); jetzt heißt es „Die App startet Dump Forge …“ (die Übersetzungen waren schon richtig).

## Hinweise
- **An der Konsole erprobt** (Slim, Firmware 12.00): ein PS4Mount-Spiel (Tetris Ultimate, PKG auf einem USB-Stick) über die Spielkarte gelöscht und mit „Kachel wieder anlegen“ zurückgeholt; Dump Forge 0.5.7 aus der App gestartet, ein Debug-FPKG gebaut und geprüft und danach wieder beendet. Die Anleitung der Vollbild-App wurde in der Vorschau am PC geprüft (Deutsch, Englisch, Russisch), nicht an der Konsole.
- **Nicht erprobt:** das Löschen eines PS4Mount-Spiels mit Zusatzinhalten (keine PS4-Zusatzinhalte zur Hand).
- 387 Tests der Vollbild-App, 43 Host-Tests und die Simulation bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.91.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung (Deutsch) |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Dank
An alle, deren Arbeit in der App steckt oder sie möglich macht – die ganze Liste steht in der App unter „Credits“ oder Profil → Dank. Besonders **quer3q** für Dump Forge 0.5.7, AMPR EMU NG und Kraken Mount, **gasb07** für PS4Mount (sein „Entfernen“ dient jetzt auch dem Spiel-Löschen), **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und den Mitwirkenden am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.90.0...v1.91.0
