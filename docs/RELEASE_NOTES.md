## Release Notes

Version: v1.53.1
Datum: 2026-10-08

Neu seit 1.53.0: runde Auswahlfelder und Knöpfe auch am Browser der Konsole.

### Behoben

- **Eckige Auswahlfelder, Knöpfe und Textfelder auf der Konsole.** Der alte Browser der Konsole zeichnete sie mit seinem eigenen eckigen Aussehen und ignorierte Rundung und Farbe, die der PC-Browser
  übernahm (zum Beispiel die Sprachwahl). Das Eigenaussehen ist jetzt abgeschaltet, den Pfeil der Auswahlfelder zeichnet die Seite selbst.

### Wichtig

- **Getestet ist Firmware 12.00** auf einer PS5 Pro (CFI-7021). Ein Nutzer meldet, dass die App auch auf einer PS5 mit **13.60** läuft (vom Autor nicht geprüft).
- Der Fix für die runden Felder lief als Testfassung an der Konsole; wie die Felder dort aussehen, ist noch nicht zurückgemeldet. Die veröffentlichte ELF ist nicht byte-gleich mit der Testfassung (Versionsnummer).
- Noch nicht an der Konsole benutzt: Profile ausführen und als Startprofil, der Import vom PC bei den Profilen, die Backport-Erkennung mit `fakelib2`/`backports`, das **Löschen von Spielständen**
  und das Überstehen des Ruhemodus. Probiere das Löschen zuerst mit einem unwichtigen Titel. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Nur `web/style.css` (Regeln für `select`, `button` und Textfelder) und die Versionsnummer; kein Eingriff in den C-Teil. Browser-Suite 54 Szenarien ohne Befund.
