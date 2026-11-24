# PS5 Cooling & System Center – Pro v1.94.0

## Highlights
- **NTFS-Laufwerke sind viel schneller:** Große Ordner, viele kleine Dateien und der erste Schreibvorgang nach dem Start der App dauern nicht mehr ewig. An der Konsole mit einem langsamen Stick: Eine kleine Datei hochladen dauert 0,2 s statt 2–3,5 s, ein Ordner mit 101 Dateien (83 MB) ist in 6 s kopiert, 121 Dateien sind in 3 s gelöscht, und der erste Schreibvorgang nach einem Start der App dauert 0,2 s statt 5–9 s.
- **AMPR EMU NG 0.5.1 (quer3q):** Schnelleres Lesen gepackter Spiele (etwa 10 % im pkg-Modus) und Korrekturen für Marvel's Wolverine (konnte nicht speichern), Hades (hing beim Beenden), DIRT 5 (stürzte nach 15 s ab), MARVEL Cosmic Invasion und Scott Pilgrim EX. Die App bringt jetzt 0.5.1 und 0.5.0 mit; 0.4.1 ist entfallen.
- **Sicherheitsaktualisierung für zstd-Pakete:** zstd 1.6.0 schließt die Lücke CVE-2026-124851. Die App nutzt nur die einfachen Funktionen und war vermutlich nicht betroffen; die Pakete bleiben byte-gleich, und früher gepackte lesen sich weiter.

## Neu
### Dateimanager: NTFS
- **Große Ordner:** Ein Name wird im sortierten Index gesucht, statt jeden Eintrag abzulaufen. Am PC brauchen 20 000 Namen 0,03 s statt 2,45 s; viele Dateien in einem Ordner kopieren oder löschen liest den Ordner nicht mehr immer wieder.
- **Viele kleine Dateien:** Der Treiber sammelt seine kleinen Änderungen und schreibt nur die geänderten Sektoren, geordnet, wenn ein Hochladen, ein Ordner oder ein Auftrag endet. Auf einem nachgebildeten langsamen Stick sind 300 kleine Dateien in 0,8 s statt 14,2 s angelegt und in 3,9 s statt 10,4 s gelöscht. Ein Stick, der mitten im Auftrag abgezogen wird, gilt weiter als nicht sauber getrennt.
- **Erster Schreibvorgang nach dem Start der App:** Das Windows-Journal des Sticks wird nur geleert, wenn es nicht schon leer ist; der erste Schreibvorgang schreibt nicht mehr bis zu 64 MB neu.

### Spiele
- **AMPR EMU NG 0.5.1:** Siehe Highlights. Die Prüfung „Passt der Index in AMPR EMU NG?“ rechnet für 0.5.1 – am Indexspeicher von NG hat sich nichts geändert.
- **zstd 1.6.0:** Packen und Entpacken nutzen die neue Bibliothek; die Frames sind byte-gleich mit denen von 1.5.7, vorhandene Pakete bleiben also gültig.

### Handbuch, FAQ und Bildschirmfotos
- **Erneuert:** Alle 98 Bildschirmfotos (14 Seiten × 7 Sprachen) zeigen v1.93 – NTFS im Dateimanager, die graue Firmware-Marke, die ganze Seriennummer auf Wunsch. Die Texte in allen sieben Sprachen sind auf Stand: Startschutz, Wiederholung bei „Device busy“, Heimnetz-Namen und Fehler 403, NTFS mitten im Schreiben abgezogen.
- **API-Hinweise:** Die Zeilen zur Indexprüfung und zum NG-Download beschrieben noch NG 0.4.0; jetzt stehen dort die Antworten der App.

## Fehlerbehebungen
- **Übersetzungen:** Der Ort „Interne SSD (/data)“ im Dateimanager und einige Zeilen der Seite Spielzeit blieben in den anderen Sprachen halb deutsch.

## Hinweise
- **An der Konsole erprobt** (Slim, Firmware 12.00): die NTFS-Zeiten oben mit einem 60-GB-Stick; ein Testspiel mit zstd-Paketen startete mit der Debug-Fassung von NG 0.5.1 (1 134 Blöcke in 26 s gelesen, im Kernel-Log nichts Auffälliges); zstd 1.6.0 prüfte, entpackte, packte neu (Byte für Byte mit den Originalen verglichen) und prüfte wieder – ohne defekten Block.
- **Bekannt:** Ein sehr billiger Stick kann beim Schreiben ab und zu minutenlang stocken – bei einem Stick gesehen, auch mit dem alten Treiber. Ordner mit etwa 100 000 Dateien sind seit der Beschleunigung nicht ausprobiert.
- 391 Tests der Vollbild-App, 46 Host-Tests und die Simulation bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.94.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung (Deutsch) |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Dank
An alle, deren Arbeit in der App steckt oder sie möglich macht – die ganze Liste steht in der App unter „Credits“ oder Profil → Dank. Besonders **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **quer3q** für Dump Forge, AMPR EMU NG und Kraken Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und den Mitwirkenden am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.93.0...v1.94.0
