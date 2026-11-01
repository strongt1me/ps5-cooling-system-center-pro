# PS5 Cooling & System Center – Pro v1.75.1

## Highlights
- **Handbuch und FAQ auf dem Stand von 1.75** – in allen sechs Sprachen, direkt in der App

## Neu
- Handbuch-Kapitel **„Die Vollbild-App“**: alle Tasten (Touchpad-Taste für die Seitenleiste, L1/R1, L2/R2, Aktionen, Options-Menü), die Bereiche und was im Browser bleibt
- Abschnitt **„Lüfterwächter“** im Kapitel Kühlung
- FAQ: Seitenleiste in der Vollbild-App, Ablageort eigener Profilbilder, warum der USB-Stick „usb0“ heißt

## Verbesserungen
- Installation: neuer Abschnitt „Die Kacheln“ – FAKE86921 und FAKE86922 starten die App selbst, nach einem Update die Ordner neu kopieren
- Profil: eigene Bilder für die Vollbild-App (`/data/PS5-Cooling-Center/Profilbilder` oder USB-Stick)
- Dateimanager: Laufwerke heißen `usb0`, `usb1` …
- Begleitdokumente (HANDBUCH.md, API.md) nachgezogen

## Fehlerbehebungen
- Veraltete Antworten berichtigt: „Nach jedem Neustart neu senden?“ (nicht mit Kachel) und „Kachel öffnet nur eine leere Seite“ (betraf nur die frühere Medien-Kachel)

## Hinweise
- Am Programm selbst hat sich gegenüber v1.75.0 nichts geändert; die neue ELF bringt das erneuerte Handbuch mit. Wer die Kacheln nutzt, kopiert die Ordner FAKE86921 und FAKE86922 neu.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.0...v1.75.1
