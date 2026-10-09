## Release Notes

Version: v1.55.0
Datum: 2026-10-09

Neu seit 1.54.1: eine Warteschlange für PKG-Installationen, ein neuer Kopf und ein Infos-Fenster auf der Spiele-Seite, „Pfad öffnen“ und neue Bilder.

### Neu

- **Pakete: Warteschlange.** Mehrere Pakete lassen sich vormerken und nacheinander installieren (bis 64, nur im Speicher, nur nach Klick). Jedes Paket wird erst an seiner Reihe geprüft: Schon installierte oder neuere Fassungen werden mit Begründung übersprungen,
  ein Update vor seinem Spiel ebenfalls. Läuft gerade ein Spiel oder ein anderer Auftrag, wartet die Schlange; bei einem Fehler hält sie an. Es gibt Pause nach dem aktuellen Paket und Abbrechen. Die Warteschlange löscht und überschreibt nie etwas.
  Schnittstelle: `/api/v1/packages/queue*` (siehe docs/API.md).
- **Spiele:** Kopf mit deinem Bild rechts (weich übergeblendet), „Aktualisieren“ neben der Suchleiste, größerer Titel. „Infos & Metadaten“ öffnet jetzt ein Fenster über der Seite (schließt mit demselben Knopf, ✕, Esc oder Klick daneben) statt die Karte aufzuklappen.
- **Spiele: neuer Knopf „Pfad öffnen“** rechts neben „Löschen“: öffnet den Speicherort des Spiels im Bereich „Dateien“ (bei Abbildern den Ordner der Datei).
- **Bilder:** Die Banner aller Kacheln füllen die ganze Breite; neuer Hintergrund für „Läuft gerade“ (Leiterbahnen), neues Lüfter-Bild.
- Alle neuen Texte in Deutsch, Englisch, Spanisch, Französisch, Italienisch und Russisch; Handbuch und FAQ aktualisiert.

### Wichtig

- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Noch nicht an der Konsole benutzt: die Warteschlange (nur im Test mit einer nachgebildeten Konsole), Profile ausführen / als Startprofil / Import, die Backport-Erkennung, das Löschen von Spielständen und das Überstehen des Ruhemodus.
  **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- Die veröffentlichte ELF ist nicht byte-gleich mit der an der Konsole getesteten (Versionsnummer).
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Neu: `src/pkgqueue.c`; geändert: `src/pkginstall.c`, `src/api.c`, `src/ps5tm.h`. Tests: Host-Test der Warteschlange (mit AddressSanitizer/UBSan und ThreadSanitizer), Browser-Suite 56 Szenarien ohne Befund, Wörterbücher aller fünf Sprachen ohne Fehler.
