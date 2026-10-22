# PS5 Cooling & System Center – Pro v1.74.0

## Highlights
- **Mehr Kontrolle bei Spiel-Updates** – Standardregel und Schalter je Spiel, „Nur laden“, Download-Ordner (auch USB), Prüfsummen-Schalter und ein Diagnosebericht
- **Andere Versionen** – Versionsliste von prosperopatches.com und orbispatches.com, dazu der letzte Patch, der auf deiner Firmware läuft
- **Fake-PKG-Spiele** werden jetzt geprüft und aktualisiert wie alle anderen

## Neu
### Spiel-Updates
- Standardregel „Updates erlauben/verweigern“ und ein Schalter „Updates“ in jeder Spielzeile
- „Nach dem Download installieren“ für alle und je Spiel; ohne Installation bleibt das Paket bereit
- Download-Ordner: interner Speicher oder ein angeschlossenes USB-Laufwerk
- Schalter für die Prüfsummen (SHA-256) der geladenen Stücke
- Diagnose je Spiel mit Bericht zum Kopieren (ohne IP-Adresse und Konsolenname)
- „Andere Versionen“: Liste mit Größe und nötiger Firmware, Anleitung mit QR-Code und Feld zum Einfügen des Links; geladen wird nur von den offiziellen Update-Servern
- „Älterer Patch passt“: braucht der neueste Patch eine neuere Firmware, schlägt die App die neueste Version vor, die hier läuft
- Fake-PKG- und fpkg-Spiele nehmen an Prüfung und direktem Download teil; der Knopf „Link“ kopiert die Download-Adresse

## Verbesserungen
- PS4 von USB: Die Suche schaut ab Werk auch in den Ordner `pkg` jedes Laufwerks
- Fehlermeldungen der Weboberfläche und zu lange Adressen werden sauber behandelt

## Fehlerbehebungen
- „Alle aktualisieren“ ließ Fake-PKG-Spiele aus, obwohl die Liste ein Update zeigte
- Mehrere Überläufe beim Lesen fremder Abbild- und Paketdateien behoben (`lz4pack`, `amprlib`, Abbild-Leser)
- Die Spielstand-Sicherung arbeitet mit einem größeren Stapel und ist dadurch stabiler

## Hinweise
- **Nicht an der Konsole bestätigt:** die Versionslisten (die Antwortformate der beiden Seiten konnten nicht live geprüft werden), die Zertifikatsprüfung der Verlaufsseiten und das Installieren eines Updates aus einem USB-Ordner. Ändert eine Seite ihr Format, meldet die App einen Fehler statt einer Liste
- Die neuen Texte gibt es zunächst nur auf Deutsch
- Die Starter-Kachel `PPSA86921.zip` enthält diese App; ein Aktualisieren der Kachel ist optional

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.74.0.elf` | die App – nach jedem Neustart der Konsole an Port 9021 senden |
| `PPSA86921.zip` | Starter-Kachel für `/data/homebrew/` (startet die App per Tipp) |
| `LIESMICH.txt` | Kurzanleitung |

## Danke
- **Knutwurst** – PatchDL, Vorbild und Quelle der Update-Funktionen
- **prosperopatches.com und orbispatches.com** – die Versionsverläufe

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.73.1...v1.74.0
