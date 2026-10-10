## Release Notes

Version: v1.56.0
Datum: 2026-10-10

Neu seit 1.55.0: Die Lüfter-Kachel zeigt einen Tacho statt eines drehenden Lüfters, installierte Spiele heißen „Installiert“, und „Pfad öffnen“ ist dort grau.

### Neu

- **Tacho statt drehendem Lüfter.** Die Kachel „Lüfter“ auf der Seite Kühlung zeigt das Zifferblatt des Users mit Nadel: von links (0 %) bis rechts (100 %), die Nadel fährt weich auf jeden neuen Wert. Der Regenbogenbogen leuchtet nur bis zur Nadel.
  Nadel, Zahl und Leuchtrand gehen wie bisher von Blau über Grün zu Rot (grau und abgedunkelt ohne Messwert). Die Auswahl „Animation“ bleibt: „reduziert“ lässt die Nadel springen, „aus“ nimmt zusätzlich ihr Leuchten weg.
  Zwischen zwei Änderungen läuft nichts mehr im Browser; das behebt auch das Ruckeln beim Zoomen und das zähe Scrollen auf der Seite Kühlung, das der Lüfter an der Konsole verursacht hatte.
- **Installierte Spiele:** Titel, deren Daten als `app.pkg` unter `/user/app/<ID>` liegen, trugen die Marke „Abbild“, wenn ShadowMountPlus sie kannte. Jetzt heißt die Marke bei allen installierten Spielen **„Installiert“** (vorher „PS4 PKG“ / „PS5 PKG“), und
  **„Pfad öffnen“**, „Verschieben“ und „Kopieren“ sind dort ausgeschaltet. „Pfad öffnen“ bei Spielen mit Ordner oder Abbild öffnet den Speicherort im Bereich „Dateien“ (seit 1.55.0).
- Handbuch (alle Sprachen) und `docs/HANDBUCH.md` beschreiben den Tacho.

### Wichtig

- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Noch nicht an der Konsole benutzt: die Warteschlange der Pakete, Profile ausführen / als Startprofil / Import, die Backport-Erkennung, das Löschen von Spielständen und das Überstehen des Ruhemodus. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- Die veröffentlichte ELF ist nicht byte-gleich mit der an der Konsole getesteten (Versionsnummer).
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Geändert: `web/ps5-fan.js` (Tacho; die Nadel ist ein eigenes `<svg>`, das als Ganzes gedreht wird, Mitte in Prozent – eine Drehung innerhalb des SVG lag im gezoomten Browser der Konsole neben der Mitte), neues Bild `web/img/gauge-dial.png` (168 KB), `src/library.c` (Erkennung `app.pkg`), `web/app.js`, `web/style.css`.
  Tests: Browser-Suite 56 Szenarien ohne Befund, Wörterbücher aller fünf Sprachen ohne Fehler.
