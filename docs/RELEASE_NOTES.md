## Release Notes

Version: v1.58.0
Datum: 2026-10-10

Neu seit 1.57.0: „Gespeicherte Avatare“ ist entfernt, die Karte „Anzeigename“ ist größer, und Tacho und Text stehen in der Mitte der Lüfter-Kachel.

### Geändert

- **„Gespeicherte Avatare“ entfernt, mit Funktion.** Die Karte (Paketname, Liste laden, In Vorschau laden, Paket löschen) und der Knopf „Als Avatar speichern“ beim Profilbild sind weg, ebenso die Schnittstellen `/api/v1/profile/avatar/library`, `…/save`, `…/load` und `…/delete`.
  Profilbild wählen, „Profilbild übernehmen“ und „Vorheriges zurückholen“ bleiben unverändert. Schon gespeicherte Pakete in `/data/PS5-Cooling-Center/Avatars` bleiben auf der Konsole liegen; die App zeigt und löscht sie nicht mehr (bei Bedarf über den Dateimanager entfernen).
- **Karte „Anzeigename“ größer:** Titel, Unterzeile, Benutzer-ID, Name im Eingabefeld, „Speichern“ und der Hinweis haben größere Schrift und mehr Platz.
- **Lüfter-Kachel:** Tacho und Text stehen als Paar in der Mitte der Kachel.

### Wichtig

- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Noch nicht an der Konsole benutzt: die Warteschlange der Pakete, Profile ausführen / als Startprofil / Import, die Backport-Erkennung, das Löschen von Spielständen und das Überstehen des Ruhemodus. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- Die veröffentlichte ELF ist nicht byte-gleich mit der an der Konsole getesteten (Versionsnummer).
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Geändert: `src/api.c` (Avatar-Paket-Schnittstellen entfernt), `web/index.html`, `web/app.js`, `web/style.css`, `docs/API.md`, `docs/HANDBUCH.md`. Tests: Browser-Suite 56 Szenarien ohne Befund, Wörterbücher aller fünf Sprachen ohne Fehler.
