# PS5 Cooling & System Center – Pro v1.85.1

## Highlights
- **FPKGMount zeigt den richtigen Suchordner:** Bei ausgeschaltetem Kraken Mount nennt die Seite jetzt den Ordner `FPKG-Games`, den die App beim ersten Start einträgt, und nicht mehr die Grundeinstellung von Kraken Mount. Das war irreführend, weil ShadowMountPlus die Ordner der Grundeinstellung ebenfalls durchsucht.

## Verbesserungen
- **Suchordner-Zeile (Browser-App und Vollbild-App):** Kraken Mount aus → „Die App trägt beim ersten Start den Ordner FPKG-Games ein: /data/FPKG-Games und der Ordner FPKG-Games auf jedem USB- und M.2-Laufwerk.“ Kraken Mount läuft mit leerer Einstellung → seine Grundeinstellung, dazu der Hinweis, FPKG-Spiele besser in `FPKG-Games` zu legen und den Ordner auf der Seite von Kraken Mount einzutragen. Mit Übersetzungen in 6 Sprachen.
- **Vollbild-App:** Ein neuer Test fährt die Dateien-Seite selbst: Eine Antwort für eine Datei, die niemand geöffnet hat, öffnet weder Editor noch Betrachter.

## Hinweise
- Alles andere ist unverändert gegenüber v1.85.0 (Durchsichten, `.ffpkg` über UFS2-Plan, geordnetes Beenden, Dump-Forge-Anleitung in Handbuch und FAQ). Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.85.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.85.0...v1.85.1
