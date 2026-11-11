# PS5 Cooling & System Center – Pro v1.80.0

## Highlights
- **Alle 28 Befunde der Sicherheits- und Stabilitätsprüfung sind bewertet, die 20 bestätigten behoben.** Die wichtigsten: Die App schließt ihren Webserver nicht mehr, wenn der Konsole Dateien, Puffer oder Speicher knapp werden; die Vollbild-Kachel ersetzt die App nicht mehr mitten in einem Auftrag; die Spieleliste blockiert nicht mehr alle Anfragen; laufende Uploads sind für alle anderen Aufträge sichtbar.
- **Eine Ausführung der App, ohne Netzwerk erkannt:** Eine neu gesendete App findet ihre ältere über eine Sperrdatei, auch wenn die ältere so überlastet ist, dass sie nicht mehr antwortet, und beendet sie sauber (der Lüfter geht zurück an die Konsole).
- **Spielkarten auf der PS5:** Die kompakte Ansicht ist jetzt Standard, die Karten haben immer dieselbe Höhe (vorher sprangen sie), und beide Ansichten sind leichter zu zeichnen. Neu ist der Knopf „Automatisch messen“ für den Scroll-Test.
- **Aufgeräumte Oberfläche:** kein Fokusrahmen des Browsers mehr um Knöpfe und Felder; der Expertenmodus steht nur noch im Banner der Seite System (ab Werk aus); „Covers & Metadaten“ arbeiten immer und haben keine Anzeige mehr; die Karte der Hintergründe erscheint nur noch, wenn die Browser-App aus der Vollbild-App geöffnet wird.
- **Neue README-Bilder** mit erfundenen Beispieldaten für alle Seiten.

## Neu und geändert

### Zuverlässigkeit (Audit vom 11.11.2026)
- **Webserver:** Bei Mangel an Datei-Handles, Puffern oder Speicher bleibt der Zugang offen (ein Reserve-Handle, kurze Pause); die Warteschlange der Verbindungen ist 128 statt 16 groß; Verbindungen von der Konsole selbst (Kachel, Browser) haben eine eigene Grenze von 12.
- **Vollbild-Kachel:** Neue Route `GET /api/v1/jobs/active`; die Kachel fragt nur sie und lässt die alte App in Ruhe, wenn sie nicht antwortet. Die ältere App bekommt zuerst `SIGTERM`, erst nach drei Sekunden `SIGKILL`. Bei einer langsamen App macht die Kachel eine wachsende Pause.
- **Spieleliste:** Wartezeiten sind begrenzt (höchstens 20 Sekunden), Ordnergrößen haben ein Zeitbudget; die Seite bekommt auch nach einer Änderung sofort die bisherige Liste.
- **Aufträge sehen einander:** Uploads, Auswerfen eines USB-Laufwerks und der Start eines Verschiebens zählen als laufender Auftrag für alle anderen; Löschen, Kopieren, Konvertieren und Verschieben laufen nicht mehr beiseite.
- **Daten:** Der Texteditor speichert mit `fsync` (nach einem Stromausfall steht die alte oder die neue Datei da, nie eine leere); übrig gebliebene Upload-Teile blockieren nichts mehr; ein abgelehnter Upload hält die Sperre höchstens 15 Sekunden; der AMPR-Index wird beim Signieren laufend korrigiert; der Payload-Store ändert seinen Index nur noch unter einer Sperre.
- **Pakete:** Das Verzeichnis eines LZ4-Pakets wird vor der Speicherreservierung geprüft; die Dateiliste von zstd bricht bei der Grenze ab.
- **Abstürze:** Beim Packen im Spielordner wird vor dem Entfernen der Originale und vor dem Signieren noch einmal geprüft, ob das Spiel läuft. Reißt das direkte Lesen eines Abbilds die App um, wird es nicht bei jedem Start wiederholt.
- **Bau:** Stack-Schutz (`-fstack-protector-strong`); die Host-Tests laufen bei jedem Push auf GitHub.

### Spielkarten und Oberfläche
- **Kompakte Ansicht ist Standard** (wer „groß“ gewählt hatte, behält es); die Karten haben immer dieselbe Höhe.
- **Leichtere Karten auf der PS5** in beiden Ansichten (vorgerundete Titelbilder, weniger Rundungen und Schatten), nach Messungen mit dem neuen Scroll-Test.
- **Kein Fokusrahmen** des PS5-Browsers mehr um Knöpfe, Auswahllisten und Felder; die eigene Markierung bleibt.
- **Expertenmodus** nur im Banner „Deine Konsole“ der Seite System, ab Werk aus.
- **Covers & Metadaten:** Der Speicher arbeitet immer; Schalter und Anzeige sind weg (Browser- und Vollbild-App).
- **Hintergründe der Vollbild-App** erscheinen in der Browser-App nur, wenn sie aus der Vollbild-App geöffnet wurde (Options → Weboberfläche im Browser öffnen).
- **Darstellung der Spielkarten wählbar** („groß“ oder „kompakt“), Marke „FW x“ mit der Firmware, die ein Spiel verlangt, helles Design lesbarer.

### Konvertieren und Packen
- **Prüfstufe „Schnell“** für exFAT-Abbilder und größere Lesestücke; SHA-256 mit den SHA-Befehlen der CPU (Selbsttest beim Start).
- **zstd und LZ4** nur noch bei Dump-Ordnern, zstd eigens benannt; unsignierte Programmdateien werden als FSELF geschrieben (nach ps5-dump-forge 0.5.4).
- **Fremdbausteine:** AMPR EMU NG 0.3.5, LZ4 1.11.1, Dump Forge 0.5.5 (`.ini` bleibt lose), APR-EMU-Versionen in drei Gruppen.

### Weitere Änderungen
- **Scroll-Test** (Expertenmodus, System): Knopf „Automatisch messen“ mit feineren Schaltern.
- **Texte und Handbuch** (sieben Sprachen) an alle Änderungen angepasst; keine 10-MB-Grenze für die ELF: aufgefüllte Kopien mit 12, 16 und 25 MB liefen an der Konsole.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- **An der PS5 Pro (Firmware 12.00) geprüft:** die Fixes mit Lasttests (16 Verbindungen von der Konsole: 12 angenommen, 4 abgewiesen; gleichzeitiges Auswerfen und Hochladen; 40 gleichzeitige Verschiebe-Starts: einer kommt durch), die Oberfläche in beiden Apps und die Ablösung der älteren Ausführung über die Sperrdatei (mehrfach). Die ELF dieses Releases trägt zusätzlich nur die Versionsnummer 1.80.0.
- Ein Programm, das mit der Konsole selbst Verbindungen zum Webserver aufbaut, sollte nicht mehr als zwölf gleichzeitig offen halten.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.80.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG und **drakmor** für ShadowMountPlus und die Hinweise zur neuen Einstellung.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.79.0...v1.80.0
