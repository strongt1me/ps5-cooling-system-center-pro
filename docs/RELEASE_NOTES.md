## Release Notes

Version: v1.54.0
Datum: 2026-10-09

Neu seit 1.53.1: aufgeräumter Bereich „Profile“, feste Power-Optionen und der kleine Lüfter unter den Schaltern.

### Neu

- **Profile aufgeräumt.** Die Kopfzeile hat nur noch **„Neues Profil“** als hervorgehobenen Knopf und daneben einen runden Menüknopf (drei Punkte). Dahinter liegen
  „Profile sichern“, „Profile laden“ und der Hilfetext; das Menü schließt sich nach der Wahl. In der Profilzeile sind „Bearbeiten“ und „Als Startprofil“ runde Symbolknöpfe
  (Stift, Stern; der Name steht als Hinweistext und für Screenreader dabei), „Ausführen“ bleibt der blaue Knopf. Das Ergebnis des letzten Laufs steht in einer schmalen Zeile statt
  im großen Kasten mit Liste.
- **Power-Optionen bleiben auf jeder Seite an derselben Stelle.** Sie sitzen überall dort, wo sie auf der Seite Kühlung sitzen. Der kleine Lüfter nimmt dafür oben keinen Platz mehr
  weg: Er hängt als schmale Zeile unter den Schaltern am rechten Rand (auf Kühlung bleibt er ausgeblendet).

### Wichtig

- **Getestet ist Firmware 12.00** auf einer PS5 Pro (CFI-7021). Ein Nutzer meldet, dass die App auch auf **13.60** läuft (vom Autor nicht geprüft).
- Die neue Profil-Ansicht und die Lage des kleinen Lüfters liefen als Testfassung an der Konsole; eine Rückmeldung, wie beides dort aussieht, steht noch aus. Die veröffentlichte ELF ist nicht
  byte-gleich mit der Testfassung (Versionsnummer).
- Noch nicht an der Konsole benutzt: Profile ausführen und als Startprofil, der Import vom PC bei den Profilen, die Backport-Erkennung mit `fakelib2`/`backports`, das **Löschen von Spielständen**
  und das Überstehen des Ruhemodus. Probiere das Löschen zuerst mit einem unwichtigen Titel. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Nur Oberfläche (`web/index.html`, `web/app.js`, `web/style.css`) und die Versionsnummer; kein Eingriff in den C-Teil. Browser-Suite 54 Szenarien ohne Befund, dazu ein Test, dass die Power-Optionen beim Seitenwechsel nicht wandern.
