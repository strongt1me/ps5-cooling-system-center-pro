## Release Notes

Version: v1.68.0
Datum: 2026-10-15

### Vier Designs
- Neuer Wähler **„Design“** oben neben der Sprache: **Aktuell**, **Hell**, **Dunkel** und **Optimal**. Umschalten ohne Neuladen; jedes Gerät merkt sich seine Wahl (z. B. „Optimal“ am Fernseher, „Hell“ am Handy)
- **Hell**: gedämpftes Grau-Blau statt Weiß, blendet nicht; Kacheln mit Bild bleiben dunkel, damit ihre Schrift lesbar bleibt
- **Dunkel**: fast schwarz, ohne Leuchten und ohne Durchscheinen
- **Optimal**: für Fernseher und PS5 – kräftiger Kontrast, größere Schrift, dickere Linien, ohne Durchscheinen und Schatten (am schnellsten gezeichnet)

### Neue Kachel-Optik
- Alle Kacheln mit leuchtendem Rand und einer Symbol-Kachel neben dem Titel (eigenes Symbol je Kachel); die Legende steht als Pille daneben
- Diagramme (Temperaturverlauf, Langzeitverlauf, Lüfterdrehzahl) in einem eigenen Innenfeld, Flächen mit Farbverlauf
- Im Design „Aktuell“ sind die Kacheln durchscheinender (72 % statt 86 %)

### LZ4-Asset-Packs (nach PS5 LZ4 Compression 1.5.0)
- Alle Dateien eines gepackten oder kopierten Spiels bekommen die Rechte 0777 (mit 0666 stürzten manche Spiele ab)
- Marker-Dateien (PlayGo-Platzhalter, `playgo.pgm`, Startdateien von Ghost of Yōtei) bleiben immer lose auf der Platte
- **„Pakete prüfen“ repariert** jetzt auch: das passende APR-EMU, fehlende Dateien aus den Paketen zurück auf die Platte, Rechte im ganzen Ordner
- Vier getestete Profile eingebaut (Astro Bot, UFC 6, Spiele von Insomniac, Unity-Spiele); das passende wird vorgeschlagen, ein eigenes Profil geht vor

### PS4-Spiele von USB (PS4Mount 0.5.0)
- **„Zurückverschieben“**: PKGs zurück in ihren Ursprungsordner, einzeln oder als Auswahl in Gruppen; ein laufendes Spiel wird vorher geschlossen
- Korrekturen aus dem Pull Request #18 (EzioRamesha): eine beschädigte PKG kann die App nicht mehr zum Absturz bringen, bei Speichermangel wird nichts halb geschrieben, ein beiseitegelegtes Update wird nicht überschrieben
- Fest auf der Konsole installierte Spiele werden nie entfernt; während PS4Mount arbeitet, bleibt die Seite bedienbar („arbeitet gerade …“)

### DLC-Module
- Mitgeliefert ist jetzt die Fassung **„log“** von dlc_emu 0.3: Die bisherige Fassung ohne Protokoll stürzt laut Autor bei jedem Spielstart ab
- Liegt die alte Fassung in einem Spiel, zeigt „Spiel-Plugins“ sie als **fehlerhaft**; „Aktualisieren“ ersetzt sie (die alte Datei bleibt als `.old`)

### Stabilität und Sicherheit
- Weboberfläche: zu tief verschachtelte Anfragen, falsche Größenangaben und überlange Adressen werden abgewiesen; Fehler der Konsole stehen im Protokoll
- **Spielstände löschen** nur noch, wenn vorher ein Laufwerk für die Sicherung gewählt ist
- Hochladen: ein stockender Upload wird beendet; eine Datei gilt erst als gespeichert, wenn sie auf dem Laufwerk ist
- Verschieben zwischen Laufwerken löscht im Original nur, was nachweislich kopiert ist
- Eine unlesbare `config.json` wird beiseitegelegt statt überschrieben
- Fehlt auf einer Firmware eine Systemfunktion, startet die App trotzdem; die Sammelinstallation ist unter Firmware 6.00 gesperrt
- ShadowMountPlus ab 1.7-beta4: installierte Pakete werden erkannt (Verschieben und Entpacken nur für Ordner und Abbilder)

### Fehlersuche und Bedienung
- Fehlermeldungen auf Deutsch (z. B. „Auf dem Laufwerk ist kein Platz mehr frei“), die Worte des Systems stehen in Klammern dahinter
- Fehlermeldungen bleiben länger stehen; „Keine Verbindung zur Konsole“ statt „Failed to fetch“
- Skriptfehler im Browser landen im Protokoll der App
- Ein Seitenwechsel beginnt oben; L2 / R2 rollen in einem offenen Fenster

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.68.0; den Ordner in `/data/homebrew/` ersetzen

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Derselbe Stand (nur mit der Nummer 1.67.0) lief an der Konsole und startete sauber. Die Designs und die Kachel-Optik sind am PC und in Handybreite geprüft, am Fernseher noch nicht.
- An der Konsole noch nicht benutzt: „Zurückverschieben“ von PS4Mount, die Reparaturen von „Pakete prüfen“ und die DLC-Module.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
