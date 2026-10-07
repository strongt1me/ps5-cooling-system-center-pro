## Release Notes

Version: v1.50.0
Datum: 2026-10-07

Neu seit 1.49.1: die Zieltemperatur bis 91 °C, die Oberfläche in sechs Sprachen, mehr Komfort im Dateimanager und kleinere Seitendateien.

### Neu

- **Zieltemperatur bis 91 °C** (bisher 78 °C), wie bei ShadowMountPlus (`fan_target_temperature`, 50–91). 91 °C ist der Wert,
  den die Konsole selbst einstellt: so leise wie ohne die App. Die **Schnellwahl** heißt jetzt Kühl 62 °C, Ausgewogen 77 °C,
  Leise 91 °C (bisher 62 / 70 / 78). Damit das Ziel erreichbar ist, parkt die Regelung den Lüfter bei hohen Zielen höher:
  die Ruhelage der Schwelle liegt bei Ziel + 10 °C (mindestens 80, höchstens 91; bei Zielen bis 70 °C ändert sich nichts).
  Die Notfallgrenze folgt dem Ziel (mindestens +4 °C, bis 95 °C), die feste Schwelle der Betriebsart „Beobachten“ geht ebenfalls
  bis 91 °C. **Die Warnschwelle geht mit dem Ziel mit** (nie niedriger als Ziel + 2 °C), sonst würde die App bei hohen Zielen dauernd warnen; der eingestellte Wert bleibt gespeichert.
  Der Vergleich mit ShadowMountPlus steht im [Handbuch](HANDBUCH.md#vergleich-mit-shadowmountplus-fan_target_temperature).
- **Sechs Sprachen.** Oben rechts in der Kopfzeile wählst du **Deutsch, English, Italiano, Español, Français** oder **Русский**.
  Beim ersten Besuch nimmt die Seite die Sprache deines Browsers (gibt es sie nicht: Englisch); die Wahl merkt sich der Browser.
  Zahlen, Datum und Uhrzeit folgen der Sprache. Das Wörterbuch einer Sprache wird erst beim Wählen geladen.
- **Handbuch und FAQ in allen sechs Sprachen**, in der App (Seitenleiste, unter „Credits“) und als **PDF und HTML mit Bildschirmfotos** zum
  Download (`PS5_Cooling_System_Center_Handbuch_FAQ_v1.50.0.zip`). Die README gibt es ebenfalls in allen sechs Sprachen.
- **Dateimanager: ordnen, auswählen, ansehen.** Sortieren nach Name, Größe oder Datum (mit Richtung; der Browser merkt sich die Wahl),
  „Alle auswählen“, „Ordnergrößen“ (zählt Ordner nach, höchstens etwa 8 Sekunden je Ordner, dann „mindestens“) und **Ansehen**: Bilder
  (PNG, JPG, GIF, WebP, BMP, ICO) und der Anfang von Textdateien (bis 256 KB) gleich im Browser.
- **Kleinere Seitendateien.** Die Dateien der Oberfläche liegen komprimiert (gzip) in der App und gehen so über das Netz: die Seite
  (HTML, Skript, Stil) schrumpft von 611 KB auf 160 KB. Browser ohne gzip bekommen den Text wie bisher.
- **GitHub:** Vorlagen für Fehler, Wünsche und Übersetzungen.

### Verbessert

- Die eingebauten Fassungen von Handbuch und FAQ haben ein kompaktes Deckblatt, eine Sprachleiste und tragen das Logo nicht mehr
  als eingebettetes Bild in jeder Seite.
- Handbuch und FAQ nennen die neuen Funktionen (Sortieren, Ansehen, Sprache).

### Wichtig

- **Getestet ist nur Firmware 12.00** auf einer PS5 Pro (CFI-7021).
- **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
- **„Spiel beenden“ fragt nicht nach.**
- **Was auf Deutsch bleibt:** die Meldungen, die die Konsole selbst auf dem Fernseher einblendet (beim Start, bei einer Warnung, bei der
  Mikrofon-Taste), die Entwicklerdokumente und die Schnittstellenbeschreibung. Ein seltener Text kann in einer Sprache noch fehlen und
  erscheint dann deutsch; bitte als Issue („Translation“) melden.

### Technisches

- Neue Dateien: `src/assets.c` (liefert die eingebetteten Dateien aus, gzip wie gespeichert oder mit libdeflate entpackt),
  `web/i18n-boot.js` und `web/i18n.js` (Sprachwahl und Übersetzung zur Laufzeit), Wörterbücher `web/lang/<xx>.json`,
  `web/handbuch.<xx>.html` und `web/faq.<xx>.html`.
- Neue Endpunkte `GET /api/v1/files/view` (Ansehen) und `GET /api/v1/files/size` (Größe von Ordnern), siehe [API.md](API.md).
- `tools/i18n/` mit den Hilfen für Übersetzer (Texte aus dem Quelltext ziehen, Wörterbuch prüfen), siehe
  [ENTWICKLUNG.md](ENTWICKLUNG.md#übersetzungen).
