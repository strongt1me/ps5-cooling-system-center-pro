# PS5 Cooling & System Center – Pro v1.98.0

## Highlights
- **Kraken Mount 0.4.2** ist eingebaut. Es hängt jetzt auch Spiel-Abbilder ein (`.exfat`, `.ffpkg`, `.ffpfs`, `.ffpfsc`, schreibgeschützt); Spielordner, Abbilder und Debug-Pakete gelten damit gleich. Der Reiter heißt jetzt **„KrakenMount“** (vorher „FPKGMount“).
- **Ordner „Kraken-Games“ statt „FPKG-Games“:** Der alte Name klang, als gehörten dort nur FPKG hin. Die App benennt den Ordner (intern und auf jedem angesteckten Laufwerk) beim nächsten Start von Kraken Mount selbst um – nur durch Umbenennen, nichts wird kopiert oder überschrieben.
- **Konvertieren und Kopieren mit drei Zielen** (Web und Vollbild): **A · Spielbar mit ShadowMountPlus**, **B · Spielbar mit Kraken Mount** und **C · Sicherung**.

## Neu
- **Auswerfen:** Solange Kraken Mount ein Abbild eines Laufwerks eingehängt hat, lehnt die App das Auswerfen dieses Laufwerks ab und nennt das Abbild – die Konsole hätte ihm sonst das Laufwerk weggezogen. Das „Auswerfen“ von Kraken Mount selbst gibt es frei.
- **Reiter KrakenMount:** Ein Abbild-Spiel heißt „Abbild (exfat)“ usw.; „Spiel löschen“ und „Quelle“ nennen die Abbild-Datei.
- **FPKG:** Ziel B ist von Anfang an gewählt (ShadowMountPlus bindet kein `.pkg` ein). PS4-Spiele können nicht nach B: Kraken Mount spielt PS5-Spiele. Für das Abbild eines PS4-Spiels wird kein Debug-FPKG mehr angeboten.
- **Kopieren nach B:** Durchsucht Kraken Mount den Ordner `Kraken-Games` des gewählten Laufwerks nicht (seine Ordnerliste wurde auf seiner eigenen Seite geändert), sagt das Kopier-Fenster das – wie die Prüfung vor dem Konvertieren schon bisher.
- **Umbenennen des Ordners nur, wenn es sicher ist:** Kraken Mount wirklich beendet, kein Spiel und kein Auftrag läuft, und nichts ist mehr aus einem alten Ordner `FPKG-Games` eingehängt. Sonst wartet die App bis zum nächsten Start und schreibt das einmal ins Protokoll.
- **Vollbild-App:** Die Zeile „Ziel“ bietet nur die Arten, die zum Spiel und Format passen; „C · Sicherung“ war vorher womöglich nicht erreichbar.
- **Doppelte Abbilder** (ShadowMountPlus) zählt nicht mehr mit, was in den Ordnern von Kraken Mount liegt; ein dort früher beiseitegelegtes Abbild bleibt aufgeführt und lässt sich zurückholen.
- **ShadowMountPlus:** Antwortet Kraken Mount auf dem Port von ShadowMountPlus, schickt die App erst dann wieder Anfragen dorthin, wenn ShadowMountPlus selbst antwortet.
- Neue Meldungen in alle sechs Sprachen übersetzt; Handbuch und FAQ in sieben Sprachen nachgezogen.

## Hinweise
- **An der Konsole erprobt (Slim):** der Umzug FPKG-Games → Kraken-Games, von Kraken Mount eingehängte Abbild-Spiele (intern und USB), das abgelehnte Auswerfen und das Auswerfen über Kraken Mount, sowie Kopieren und Konvertieren eines Spiels (Asterix & Obelix: Heroes, 1,74 GB) nach Ziel B – das Kopieren noch einmal mit dem Stand dieses Releases. An der Pro wurde der Umzug bestätigt. Am PC geprüft: 47 Host-Tests mit Lüfter-Simulation, 396 Tests der Vollbild-App.
- **Läuft Kraken Mount, während die App aktualisiert wird,** behält es seine alte Ordnerliste, bis es neu gestartet wird. Der Reiter KrakenMount sagt das: Kraken Mount einmal aus- und wieder einschalten, dann benennt die App den Ordner um.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Danach eine offene Weboberfläche einmal neu laden.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.98.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.97.0...v1.98.0
