## Release Notes

Version: v1.68.1
Datum: 2026-10-15

### Design „Standard“
- Das Design **„Aktuell“** heißt jetzt **„Standard“**; wer es gewählt hatte, behält es automatisch
- Die Kacheln sind darin wieder **deckend** (ohne Durchscheinen)

### Nie zwei PS4Mount gleichzeitig
- Läuft PS4Mount schon als eigenes Payload (`ps4mount.elf`), lädt die App ihr eingebautes PS4Mount **nicht ein zweites Mal**: Der Reiter „PS4 von USB“ zeigt „wartet“ samt Grund, und sobald das eigenständige beendet ist, übernimmt die App von selbst
- Erkannt wird es an seinem Web-Panel (Port 8770), an der Sperre auf Port 9032 oder am Prozessnamen; taucht es später doch auf (etwa nach dem Ruhemodus), hält die App ihres an, statt mit ihm um dieselben Spiele zu streiten
- Umgekehrt startet die Seite **Payloads** (auch ein Startprofil) keine `ps4mount.elf`, solange schon ein PS4Mount läuft – erkannt am Inhalt der Datei, nicht am Namen

### PS4-Spiele von USB (PS4Mount 0.5.1)
- Jede Zeile im **Protokoll von PS4Mount** beginnt jetzt mit Datum und Uhrzeit auf die Millisekunde, wie in PS4Mount 0.5.1 (z. B. `2026-10-15 18:08:35.284`)
- Eine Zeile, die in mehreren Teilen geschrieben wird, bekommt einen einzigen Stempel
- Die Korrekturen aus dem Pull Request #18 bleiben erhalten (in 0.5.1 sind sie noch nicht enthalten)

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.68.1; den Ordner in `/data/homebrew/` ersetzen

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Alles Weitere (vier Designs, neue Kachel-Optik, LZ4, DLC-Module, Stabilität) steht bei v1.68.0.
- Der Schutz vor einem zweiten PS4Mount ist am Rechner geprüft (Erkennung, Sperre auf der Seite Payloads, Anzeige „wartet“); an der Konsole lief noch kein eigenständiges PS4Mount gleichzeitig mit der App.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
