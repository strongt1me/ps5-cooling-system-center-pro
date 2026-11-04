# PS5 Cooling & System Center – Pro v1.76.1

## Highlights
- **Dateimanager: Ersetzen oder Beide behalten** – gibt es einen Namen im Ziel schon, fragt ein Fenster wie im Prospero Manager; in der Browser-App und in der Vollbild-App
- **Zielwahl mit Laufwerken** – „Kopieren nach“ zeigt oben die Orte der Konsole
- **Reiter „Konto“ in der Vollbild-App** – Offline-Konto, Fake-Anmeldung und Zurücksetzen mit dem Controller

## Neu

### Dateimanager
- „Kopieren nach“ und „Verschieben nach“ zeigen oben die Orte der Konsole (Laufwerke, `/data`, USB) als Schaltflächen; vorher ging es nur durch die Unterordner des aktuellen Ordners.
- Namen, die es im Ziel schon gibt: **Ersetzen**, **Beide behalten** (der neue Eintrag heißt `Name (2)`, bei Dateien `Name (2).txt`), **Überspringen** oder Abbrechen. Gilt für Kopieren, Verschieben und Einfügen aus der Ablage.
- Eine Datei wird beim Ersetzen erst neben die alte geschrieben und am Ende darübergelegt; bricht das Kopieren ab, bleibt die alte unverändert. Ein Ordner wird zusammengeführt.

### Vollbild-App
- Neue Ansicht „Konto“ im Profil (L2/R2): Offline-Konto aktivieren (Name auf der Bildschirmtastatur), Fake-Anmeldung schreiben, alles zurücksetzen.

### Kachel für „nur ELF“
- Wer nur die ELF sendet und keine Kachel hat, bekommt wieder eine: Etwa eine Minute nach dem Start legt die App die Medien-Kachel einmal selbst an (öffnet die Weboberfläche). Nur wenn gerade kein Spiel läuft und keine der Kacheln `FAKE86921`/`FAKE86922` da ist. Wartet unter `/user/app` noch ein anderer Titel, meldet die App das und versucht es beim nächsten Start erneut.

## Fehlerbehebungen
- „Alles zurücksetzen“ löschte `auth.dat` und `config.dat` nie, und die Anzeige „Fake-Anmeldung vorhanden“ blieb bei „keine“: Sie suchten unter einem falsch geschriebenen Ordnernamen.
- Die Profilseite zeigte beim Offline-Konto „?“ statt des Namens.

## Hinweise
- Mit aktiver Fake-Anmeldung hält sich die Konsole für bei PSN angemeldet und versucht, die Server zu erreichen. Sind sie per DNS gesperrt, kann der Login nach der Benutzerwahl länger dauern, und Apps wie der Y2JB zeigen eine Fehlermeldung. Die Fake-Anmeldung deshalb nur einschalten, wenn man sie braucht: „Alles zurücksetzen“ und danach „Offline-Konto aktivieren“ nimmt sie wieder weg.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.0...v1.76.1
