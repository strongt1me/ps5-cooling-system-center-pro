## Release Notes

Version: v1.57.0
Datum: 2026-10-10

Neu seit 1.56.0: eine eigene runde Dateiauswahl (auch an der Konsole auf Deutsch), runde Kästchen, drei Korrekturen und kleine Änderungen an der Lüfter-Kachel.

### Neu

- **Dateiauswahl.** Das Feld des Browsers schrieb an der Konsole „Choose File“ / „No file chosen“ auf Englisch und war eckig. Jetzt öffnet ein eigener runder Knopf „Datei auswählen“ die Auswahl, daneben steht „Keine Datei ausgewählt.“ oder der Dateiname
  („n Dateien ausgewählt“ bei mehreren). Betrifft alle sechs sichtbaren Stellen (Profilbild, Web-Profil und Regeln importieren, Kachel-Paket, Referenz-Snapshot, Konfiguration importieren); in allen Sprachen übersetzt.
- **Runde Kästchen** (Mitlaufen im Kernel-Log, „Alle auswählen“ im Dateimanager, Auswahl bei den Spielständen) und der Regler der Zieltemperatur bekommen auch im alten Browser der Konsole die runde Form; die Schalter bleiben, wie sie waren.
- **Lüfter-Kachel:** Die Auswahl „Animation“ ist entfernt (es läuft immer „voll“); der Text steht zentriert zwischen Tacho und rechter Kante und ist größer. Kachel „Läuft gerade“: mehr Abstand zwischen Controller und Text.

### Behoben

- **Schnellwahl** (Kühl / Ausgewogen / Leise) wurde an der Konsole erst nach „Übernehmen“ sichtbar, wenn der Regler vorher angefasst worden war: Der angefangene Wert verdrängte den neuen im Regler. Jetzt gilt die Schnellwahl sofort und der Regler folgt.
- **Gespeicherte Avatare:** Ein im Feld „Paketname“ stehen gebliebener Name überstimmte die Liste, sodass „In Vorschau laden“ und „Paket löschen“ nichts fanden. Beide nehmen jetzt das in der Liste gewählte Paket; der Name im Feld zählt nur, wenn nichts gewählt ist.

### Wichtig

- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Noch nicht an der Konsole benutzt: die Warteschlange der Pakete, Profile ausführen / als Startprofil / Import, die Backport-Erkennung, das Löschen von Spielständen und das Überstehen des Ruhemodus. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- Die veröffentlichte ELF ist nicht byte-gleich mit der an der Konsole getesteten (Versionsnummer).
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Nur Oberfläche: `web/app.js`, `web/style.css`, `web/index.html`, Wörterbücher; Handbuch beschreibt den Tacho ohne die Auswahl der Animation. Tests: Browser-Suite 56 Szenarien ohne Befund, Wörterbücher aller fünf Sprachen ohne Fehler.
