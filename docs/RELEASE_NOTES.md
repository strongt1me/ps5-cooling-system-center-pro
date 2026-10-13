## Release Notes

Version: v1.65.0
Datum: 2026-10-13

### Starter-Kachel
- Neuer Dump-Ordner **`PPSA86921`** für `/data/homebrew` (Datei `PPSA86921.zip`, entpackt in den Homebrew-Ordner kopieren): ShadowMountPlus meldet ihn an, auf dem Startbildschirm erscheint eine Kachel
- Ein Tipp auf die Kachel startet die App (die ELF steckt in der `eboot.bin`, es muss nichts mehr gesendet werden) und öffnet danach die Web UI im Browser der Konsole; läuft die App schon, öffnet sie nur die Web UI
- Gebaut als nativer PS5-Titel nach dem Vorbild von ps5-native-app-boilerplate; die ELF darin ist die von v1.65.0 (bei jeder neuen Version den Ordner neu laden)
- Neue Schnittstelle `POST /api/v1/open-ui` (nur von der Konsole selbst), öffnet den Browser auf der Web UI

### Oberfläche
- Seitenleiste: Kühlung oben, Profil unter System
- Alle Kacheln sind leicht durchscheinend
- Beim Konvertieren steht das Format des Ergebnisses

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Die Starter-Kachel ist an einer PS5 Pro mit 12.00 geprüft (Tipp startet die App und öffnet die Web UI); auf anderen Firmwares ist sie ungetestet.
- Die LZ4-Pakete sind an der Konsole nur mit Arkanoid ausprobiert (kein Spielstart mit Paketen); der Mitschnitt und die DLC-Module sind dort noch nicht benutzt. Erst ungepackt starten, dann packen.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
