## Release Notes

Version: v1.69.1
Datum: 2026-10-18

### Oberfläche
- **Flüssiges Scrollen** (Stick am Fernseher) in „Standard“, „Hell“ und „Dunkel“, nicht nur in „Optimal“. Gefunden mit fünf Testfassungen von „Standard“ an der Konsole: Bremsen waren der weiche Leuchtrand der Karten (Schatten mit 20 Pixel Unschärfe) und die Textschatten. Der Leuchtrand besteht jetzt aus drei feinen Ringen ohne Unschärfe (in „Hell“ der Schatten unter den Karten aus feinen Kanten), der allgemeine Textschatten entfällt (auf den dunklen Karten war er nicht zu sehen), in der Seitenleiste bleibt ein kleiner statt drei großer. „Hell“ hat deckende Karten und einen flachen Hintergrund im selben Farbton, „Dunkel“ ebenfalls einen flachen Hintergrund. Außerdem liegt die klebende Seitenleiste auf einer eigenen Ebene. Das Aussehen ändert sich kaum; an der Konsole in allen vier Designs bestätigt
- **Flackern der Spiele-Seite alle paar Sekunden behoben:** Die Lüfterfarbe (`--fan-rgb`) stand am Wurzelelement der ganzen Seite. Im Spiel ändert sich die Drehzahl alle paar Sekunden um einen Prozentpunkt, und jede Änderung ließ die Stilberechnung des gesamten Dokuments von vorn beginnen, auf jeder Seite. Die Farbe steht jetzt nur noch auf der Kühlungsseite, wo allein sie gebraucht wird; das Aussehen ist unverändert
- Die Suche in der Spieleliste zeichnet das Raster erst nach einer kurzen Eingabepause neu statt bei jedem Buchstaben
- **Listen werden nur noch bei einer Änderung neu gebaut:** Die Spieleliste wurde jede Minute komplett neu gezeichnet (alle Karten samt Bildern), die Spielregeln alle 15 Sekunden, das Protokoll alle 5 Sekunden und das Live-Protokoll alle 2 Sekunden, auch wenn sich nichts geändert hatte. Das ruckelte beim Scrollen und nahm einem halb bestätigten Knopf („Wirklich? Nochmal klicken“) die Bestätigung
- **Profilbilder:** Die Auswahl zeigt Vorschaubilder mit 132 statt 440 Pixel (rund 0,2 statt 1,3 MB, 25-mal weniger Bildpunkte zum Dekodieren); ein Klick lädt weiter die volle Datei

### Starter-Kachel
- Die Starter-Kachel (`PPSA86921`) läuft nach dem Tipp einige Sekunden als eigener Titel, bis sie die App übergeben hat. Die Spielerkennung hielt sie dabei für ein Spiel: Sie stand unter „Läuft gerade“, begann und beendete eine Sitzung der Spielzeit (ein gleichzeitig laufendes Spiel wurde dabei in zwei Sitzungen zerteilt), und ihre Sekunden bei Leerlauf-Drehzahl hätten als Mindestdrehzahl der Konsole im Spiel gelernt werden können. Sie zählt jetzt nicht mehr als Spiel; ein Spiel, das daneben läuft, bleibt in dieser Zeit das Spiel
- `PPSA86921.zip` enthält die App von v1.69.1; den Ordner in `/data/homebrew/` ersetzen (die App steckt in der `eboot.bin` der Kachel, sonst startet die Kachel die alte Fassung)

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- **Ruckeln und Flackern:** Beides ist am Browser der Konsole geprüft und weg. Gemessen wurde nicht mit Zahlen, sondern an der Konsole selbst, im Vergleich der Designs; im Testbrowser am PC ruckelt nichts.
- **Starter-Kachel:** Wer die App über die Kachel startet, braucht den neuen Ordner `PPSA86921`; läuft die App schon, öffnet ein Tipp auf die Kachel nur die Oberfläche und ersetzt die laufende nicht.
- Alles Weitere zur Kühlung (Nachtmodus, Selbsttest, Mindestdrehzahl, Vorhalt) steht bei v1.69.0.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
