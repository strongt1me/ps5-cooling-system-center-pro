# PS5 Cooling & System Center – Pro v1.81.0

## Highlights
- **Systemsprache:** Die Browser-App übernimmt ohne eigene Wahl die Systemsprache und zeigt Englisch, wenn sie die Sprache nicht hat. Im Browser der PS5 gilt die Sprache der Konsole, am PC und Handy die des Browsers. In der Sprachwahl oben steht dafür der neue Eintrag **Auto (System)**; eine eigene Wahl bleibt gemerkt. Die Vollbild-App folgte der Systemsprache schon und zeigt, wie die Browser-App, Englisch, wenn sie die Sprache nicht hat.
- **Ein gemeinsames Register für laufende Aufträge:** Dateimanager, Kopieren, Konvertieren, Verschieben, Löschen, Abbild-Aufträge, AMPR-Index, Paket installieren und teilen, Spielstände und das Auswerfen eines Laufwerks fragen jetzt eine einzige Liste, ob etwas anderes läuft. Aufträge, die einzelnen Listen früher fehlten (Disc-Abbild, Auswerfen, Spielstände …), werden überall berücksichtigt, und die Meldungen nennen den laufenden Auftrag.
- **Zuverlässiger bei Last und Abbrüchen:** Ein abgelehnter Upload zeigt den Grund statt eines Netzwerkfehlers; ein einmal unterbrochenes Lesen eines Abbilds sperrt das Spiel nicht mehr dauerhaft; „Verlauf zurücksetzen“ antwortet erst, wenn die leere Datei geschrieben ist.
- **Nachvollziehbar:** Die Energie-Befehle „Aus“, „Neustart“, „Ruhemodus“ und „Abgesicherter Modus“ schreiben Adresse, Origin und Gerät des Absenders ins Protokoll.

## Neu und geändert

### Oberfläche
- **Sprachwahl „Auto (System)“** (siehe Highlights). Alle Sprachen des Browsers werden der Reihe nach geprüft, nicht nur die erste.
- **Hintergründe der Vollbild-App:** Die Karte bleibt sichtbar, wenn man aus der Vollbild-App kommt und über Handbuch oder FAQ zurückkehrt; ein frischer Start über die Kachel zeigt sie nicht.

### Aufträge und Zuverlässigkeit
- **Register der Aufträge** (`src/jobreg.c`): ein neuer Auftrag wird nur noch an einer Stelle eingetragen; neuer Test mit 199 Prüfungen.
- **Abbild-Lesen:** Eine übrig gebliebene Lesemarke macht einen Titel erst beim zweiten Mal zum Fall für die Sperrliste (ein gewöhnliches Beenden, etwa beim Senden einer neuen ELF, sperrt nichts mehr); das Schreiben der Marke braucht kein `fsync` mehr.
- **Upload:** Bei einer Ablehnung mit 409 liest der Server den Rumpf zuerst leer (höchstens 15 Sekunden und 64 MiB).
- **Verlauf zurücksetzen:** Die Antwort kommt nach dem Schreiben der leeren Datei (höchstens 3 Sekunden).
- **Energie-Befehle:** Protokollzeile `power_request` mit Absender.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- **An der PS5 Pro (Firmware 12.00) geprüft:** die Sprachwahl in der Vollbild-App (folgt der Systemsprache der Konsole, im Menü „Automatisch (Deutsch)“), das Register mit Lasttests (Hochladen gegen Auswerfen eines nicht vorhandenen Laufwerks, dreimal je 80 Anfragen: kein abgerissener Aufruf, Ablehnungen mit dem richtigen Grund; 40 gleichzeitige Verschiebe-Starts) und die Ablösung der älteren Ausführung über die Sperrdatei. Die Auswahl der Sprache in der Browser-App ist am PC mit neun Fällen geprüft.
- **Danach an der Konsole bestätigt (13.11.2026):** Die Browser-App startet auf dem PS5-Browser auf Deutsch mit „Auto (System)“, und die Vollbild-App startet und läuft sauber.
- Wer die Sprache der Vollbild-App im Menü fest gewählt hat, behält sie; „Automatisch“ im Menü folgt wieder der Konsole.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.81.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG und **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.80.0...v1.81.0
