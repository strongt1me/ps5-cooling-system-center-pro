# PS5 Cooling & System Center – Pro v1.88.0

## Highlights
- **Spielstände übertragen jetzt auch in der Vollbild-App, mit einem USB-Laufwerk:** Auslesen und Einspielen ohne PC und ohne Handy. Im Menü eines Spiels steht „ZIP auslesen auf <Laufwerk>“, auf der anderen Konsole wählst du die ZIP-Datei vom Laufwerk und spielst sie ein. Der Browser kann weiter alles, was er konnte.
- **Alle Spielstände eines Spiels auf einmal, ohne Wahl:** Ein Spiel legt mal `SaveData`, mal `sce_sdmemory`, mal beides an. „Als ZIP“ liest jetzt alle aus (eine ZIP-Datei je Spielstand, gleiche Zeit im Namen), und beim Einspielen lassen sich mehrere ZIP-Dateien eines Spiels in einem Auftrag wählen.
- **Gründlich geprüft vor dem Release:** vier Prüfer, drei Gegenprüfer, REA, `clang --analyze` und Fuzzing (rund 40 Millionen Läufe ohne Absturz) haben das Übertragen der Spielstände durchgesehen; die bestätigten Funde sind behoben (siehe unten).

## Neu
### Vollbild-App
- **Spiele → Spielstände:** Menü eines Spiels → „ZIP auslesen auf <Laufwerk>“; unten die Liste „ZIP-DATEIEN“ der angesteckten Laufwerke, mit Menü zum Einspielen (auch mehrere Dateien eines Spiels) und einer Karte, die vor dem Start zeigt, was eingespielt wird.
- **Sicherungen nennen das Spiel:** In der Liste der Sicherungen steht zuerst das Spiel (bis zu drei Namen, dann „+N“), darunter Datum, Laufwerk und Benutzer – in der Browser-App ebenso.

### Spielstände übertragen
- **Alle Plätze eines Spiels** werden automatisch ausgelesen; niemand muss wissen, wie das Spiel seine Spielstände nennt. Hat ein Spiel mehr als 16, liest die App nichts aus und sagt es.
- **Einspielen mehrerer ZIP-Dateien** eines Spiels in einem Auftrag (vom Laufwerk, die Dateien bleiben liegen).

### Sicherer eingespielt (Prüfrunde)
- **Spielstart vor dem Tausch:** Kurz vor dem Punkt ohne Rückkehr schaut die App noch einmal, ob ein Spiel gestartet wurde, und ändert dann nichts.
- **Apps (YouTube & Co.) werden nie eingespielt**, und ihre Spielstände dienen auch nicht als Vorlage.
- **Die Spielstand-Datenbank der Konsole wird nicht mehr überschrieben**, wenn das Schreiben scheitert (zum Beispiel weil sie gerade gesperrt ist); sie wird vorher geprüft, und die Kopie trägt den Spielstand im Namen.
- **ZIP-Dateien aus 7-Zip und dem Datei-Manager von Windows** (mit Ordner-Einträgen) werden jetzt angenommen; feindliche Dateien mit falschen Größenangaben werden vorab abgelehnt.
- **Listen zeigen die neuesten ZIP-Dateien** auch bei mehr als 60 Dateien; ein abgezogener Stick lässt kein Bruchstück mehr zurück.
- **Ein unterbrochenes Einspielen** (Strom weg, die App wird ersetzt, ein Absturz) wird beim nächsten Start der App zu Ende geführt oder zurückgenommen; Reste erscheinen nicht als eigener Spielstand.
- **Der Lüfter regelt weiter**, während ein Spielstand eingehängt ist (vorher warteten Lüfterregelung und Power-Optionen, bis der Vorgang zu Ende war).
- **Aufräumen:** Von den Kopien der Spielstand-Datenbank bleiben je Benutzer die 10 neuesten; hochgeladene ZIP-Dateien, die einen Tag lang niemand eingespielt hat, werden entfernt.

## Hinweise
- **An der Konsole geprüft (Slim, Firmware 12.00, 19.11.2026), über die Schnittstelle der App:** Auslesen aller Plätze (zwei ZIP-Dateien mit gleicher Zeit), Ansicht und Einspielen zweier ZIP-Dateien in einem Auftrag vom Laufwerksordner, Ablehnung falscher Listen (verschiedene Titel, doppelte Namen, fehlende Datei).
- **Noch nicht an der Konsole geprüft:** die Bedienung der neuen Menüs der Vollbild-App mit dem Controller; die Härtungen dieser Prüfrunde (sie sind in 213 Host-Prüfungen des Einspielens, 34 Host-Tests und 375 Tests der Vollbild-App geprüft); Firmware ab 13.x.
- **Zum Weitergeben:** Die ZIP-Datei enthält keine Konto-Kennung mehr, aber die übrigen Besitzerangaben des Benutzers (das Einspielen braucht sie): nur an Menschen weitergeben, denen du das zutraust.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Alles andere ist wie in v1.87.0.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.88.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: als Quelle `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge, **drakmor** für ShadowMountPlus sowie **itsPLK** für den Payload Manager und **earthonion** für Garlic SaveMgr (Vorbild für das Format der ZIP-Datei).

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.87.0...v1.88.0
