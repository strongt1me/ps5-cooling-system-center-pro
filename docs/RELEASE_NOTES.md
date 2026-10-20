# PS5 Cooling & System Center – Pro v1.71.3

## Highlights
- Sammelversion aus der Gesamtprüfung: viele kleine Fehler behoben, bessere Kompatibilität mit dem PS5-Browser, Handbuch und FAQ in allen sechs Sprachen auf dem neuesten Stand

## Verbesserungen
- Stilregeln für ältere Browser getrennt, damit ein unbekanntes Selektorstück nicht die ganze Regel verwirft
- Benutzerhandbuch und FAQ (HTML, PDF, in der App) vollständig aktualisiert; neue FAQ „Die Oberfläche ruckelt beim Scrollen“
- Temperaturprotokoll schreibt im Hintergrund

## Fehler
- Gerade gelöschte Titel kommen nicht mehr aus der Updates-Warteschlange zurück
- Payload-Profile akzeptieren `.bin`; Startmarke wird nur bei älterem Stand entfernt
- Payloads herunterladen: doppeltes „Aktualisieren“ abgefangen, Löschen prüft die Prüfsumme
- Doppelte `Content-Length`-Kopfzeilen werden abgelehnt; zweite App-Instanz wird zuverlässiger erkannt

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.71.3.elf` | die App – nach jedem Neustart der Konsole an Port 9021 senden |
| `PPSA86921.zip` | Starter-Kachel für `/data/homebrew/` (startet die App per Tipp) |
| `LIESMICH.txt` | Kurzanleitung |

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.71.2...v1.71.3
