## Release Notes

Version: v1.59.0
Datum: 2026-10-10

Neu seit 1.58.0: ein leuchtender Luftstrom in der Lüfter-Kachel, ein neues Bild und eine neue Anordnung für „Läuft gerade“, eine Zahl, die mit der Tachonadel mitläuft, und ein aufgeräumter Dateimanager.

### Neu

- **Lüfter-Kachel mit leuchtendem Luftstrom.** Neues Hintergrundbild des Users; der Luftstrom leuchtet in der Farbe der Drehzahl: stufenlos Blau (bis 25 %) → Grün (55 %) → **Gelb (75 %, neu)** → Rot (ab 95 %). Nadel, Zahl und Leuchtrand folgen demselben Verlauf.
  Der Tacho ist entlang seiner Form freigestellt (glatte Kante) und hat einen weichen Lichtschein in derselben Farbe, damit er zum Bild gehört. „LÜFTER“, „%“ und „gemessene Drehzahl“ sind heller und haben einen leichten Schatten; dahinter liegt ein dunklerer Verlauf.
  Technik ohne `filter` (der Browser der Konsole ist alt): eine Farbfläche, die nur durch eine Maske mit dem Luftstrom scheint; die Farbe ändert sich nur mit dem Messwert.
- **Die Zahl läuft mit der Nadel mit.** Steigt oder fällt die Drehzahl, zählt die Zahl Schritt für Schritt mit, statt sofort zum Endwert zu springen. Ist die Kachel nicht zu sehen (Tab im Hintergrund, weggescrollt), stehen Nadel und Zahl sofort auf dem Endwert.
- **Kachel „Läuft gerade“:** neues Hintergrundbild des Users (Luftstrom und Skala, sein eingezeichneter Rahmen ist abgeschnitten); Text oben links, Controller unten rechts direkt auf dem Bild, beide mit gleichem Abstand zum Rand.
- **Dateimanager:** Der Knopf in der Seitenleiste heißt „Dateimanager“ (alle Sprachen); der Titel „Dateien“ und die Zeile darunter sind größer; „Aktualisieren“ steht in der Werkzeugzeile rechts neben „Hochladen“.

### Behoben

- Verließ die Lüfter-Kachel mitten in einer Bewegung den Bildschirm, blieben Nadel und Zahl auf dem alten Wert stehen.

### Wichtig

- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Noch nicht an der Konsole benutzt: die Warteschlange der Pakete, Profile ausführen / als Startprofil / Import, die Backport-Erkennung, das Löschen von Spielständen und das Überstehen des Ruhemodus. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- Die veröffentlichte ELF ist nicht byte-gleich mit der an der Konsole getesteten (Versionsnummer).
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- Nur Oberfläche: `web/ps5-fan.js` (Farbverlauf mit Gelb, Ereignis `fan-display` für die Zahl, Endstand ohne Animation, wenn unsichtbar), `web/app.js`, `web/style.css`, `web/index.html`, neue Bilder `web/img/fan-flow-mask.png` und `web/img/gauge-halo.png`, geänderte `banner-fan.jpg`, `banner-game.jpg`, `gauge-dial.png`; Handbuch in allen Sprachen.
  Tests: Browser-Suite 56 Szenarien ohne Befund, Wörterbücher aller fünf Sprachen ohne Fehler.
