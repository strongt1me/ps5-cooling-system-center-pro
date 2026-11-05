# PS5 Cooling & System Center – Pro v1.76.8

## Highlights
- **Browser-App:** Vollbild startet nur noch im Browser der PS5, am PC und am Handy bleibt das normale Fenster mit Taskleiste
- **Dateimanager:** **Kopieren nach** und **Verschieben nach** für mehrere ausgewählte Einträge auf einmal

## Neu

### Dateimanager
- In der Leiste über der Auswahl stehen neben „Kopieren“, „Ausschneiden“, „Löschen“ und „Auswahl aufheben“ jetzt auch **Kopieren nach** und **Verschieben nach**. Sie öffnen dieselbe Zielwahl wie „⋯“ bei einem einzelnen Eintrag (Laufwerke oben, Ordner zum Hineinwechseln, „Hierhin kopieren/verschieben“), aber für alle ausgewählten Einträge auf einmal. Danach ist die Auswahl aufgehoben.
- „Verschieben nach“ gibt es wie „Ausschneiden“ nur, wenn alle Einträge änderbar sind, und es ist im selben Ordner gesperrt.
- Die Vollbild-App nimmt mehrere Einträge weiter über die Ablage („Kopieren/Ausschneiden“ und „Hier einfügen“).

## Verbesserungen

### Browser-App: Vollbild
- Der erste Klick irgendwo auf der Seite hat bisher in jedem Browser auf Vollbild geschaltet; am PC musste man F11 drücken, um die Taskleiste wiederzusehen. Jetzt geschieht das nur noch im Browser der PS5 selbst (dort gibt es keine Taskleiste). Am PC und am Handy bleibt die Seite im normalen Fenster, bis jemand **Vollbild** drückt; der Knopf merkt sich das für die nächsten Starts, **Vollbild beenden** schaltet es wieder ab. Wer es schon einmal bewusst ein- oder ausgeschaltet hatte, behält seine Wahl.

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren, damit sie dieselbe Fassung tragen. Die Änderungen dieses Release betreffen die Browser-Seite, die in der ELF steckt.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.8.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.7...v1.76.8
