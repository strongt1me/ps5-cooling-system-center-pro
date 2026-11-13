# PS5 Cooling & System Center – Pro v1.82.0

## Highlights
- **Konvertieren mit Prüfung vor dem Start:** Das Fenster zeigt alle Befunde auf einmal als Liste (Quelle lesbar, Platz auf dem Ziel, FAT32-Grenze, Name am Ziel schon vergeben, Spiel läuft, anderer Auftrag läuft, Namen und Pfade nah an der Grenze). Rot sperrt den Start, Gelb warnt nur.
- **Warteschlange für Konvertierungen:** Bis zu 24 Spiele nacheinander. Läuft schon ein anderer Auftrag (auch ein Verschieben oder Entpacken durch ShadowMountPlus), wartet der Eintrag und beginnt danach von selbst. Ein Fehler hält sie nicht an, ein gescheiterter Eintrag lässt sich erneut versuchen, am Ende kommt eine Meldung der Konsole.
- **Name der Ausgabe nach Vorlage:** `{titel}`, `{id}`, `{version}` und `{quelle}`, zum Beispiel `{titel} [{id}] v{version}`. Leer bleibt es wie bisher der Name des Ordners.
- **Sicherer:** Ändert sich die Quelle, während die App konvertiert (Datei ersetzt, gelöscht oder neu), wird das Ergebnis nicht übernommen, und die Meldung nennt die Datei.

## Neu und geändert

### Konvertieren (Browser-App und Vollbild-App)
- **Prüfung vor dem Start** (`POST /api/v1/library/convert/check`): die Liste aus den Highlights; ein laufender anderer Auftrag sperrt nur den Start, nicht das Einreihen.
- **Stufenzeile:** Vermessen, Schreiben (bei ffpfsc Komprimieren), Prüfen und Umbenennen, jeweils mit der Zeit des Schritts.
- **Warteschlange** (`/api/v1/library/convert/queue`): liegt im Speicher der App (nach einem Neustart leer) und löscht nichts; „Abbrechen“ des laufenden Auftrags lässt sie weiterlaufen, „Warteschlange stoppen“ hält nur weitere Starts an.
- **Vollbild-App:** Namenszeile mit drei Stufen (Name des Ordners, Vorlage, eigener Name über die Bildschirmtastatur), Prüfliste mit farbigen Punkten, Stufenleiste auf der Karte „Konvertieren“ und ein Warteschlangen-Fenster (△ auf der Karte).
- Neue Texte in den sechs Wörterbüchern der Browser-App und der Vollbild-App; das Handbuch beschreibt es in allen sieben Sprachen.

### Durchsicht
- Nach einer Gegenprüfung durch mehrere Fachprüfer (14 von 22 Befunden bestätigt, alle klein) sind fünf Dinge verbessert: die Prüfliste vermisst höchstens ein Spiel zugleich und hält ihren Plan frisch, „Abbrechen“ trifft nur den Auftrag, den man gesehen hat, zu lange Namensvorlagen werden abgelehnt statt gekürzt (dazu werden ungültige und unsichtbare Zeichen im Namen ersetzt), ein Abbruch in der letzten Quellprüfung wird beachtet, und das Handbuch beschreibt Reste abgebrochener Aufträge richtig.
- Acht Funde der Durchsicht der neuen Teile sind behoben: wartende Einträge stehen auf „wartet“ und lassen sich entfernen, das Ergebnis eines Auftrags geht nicht mehr verloren, wenn gleich danach ein anderer startet, bei einem Blocker durch ShadowMountPlus wird nur alle 30 Sekunden neu versucht, der Plan-Cache hält vier Titel.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- **An der PS5 Pro geprüft** (die Spiele nur gelesen, die Ergebnisse danach gelöscht): zwei echte Spiele nacheinander in der Warteschlange (5,6 GB als ffpfsc in 176 s und 0,9 GB in 29 s, beide geprüft, kein Fehlalarm der Quellprüfung), Warten bei laufendem eigenem Auftrag und bei einem Entpacken durch ShadowMountPlus, Entfernen, Stoppen und Abbrechen, Prüfliste mit Vorlage und Standardname. Die App sah nach dem Senden gut aus. An einer Testkopie eines echten Spiels (12 800 Dateien) wurde außerdem bestätigt: eine unveränderte Quelle gibt keinen Fehlalarm, eine während des Konvertierens geänderte Quelle bricht den Auftrag mit dem Namen der Datei ab, und ein Abbruch im letzten Schritt wirkt.
- **Nicht einzeln bestätigt:** die Bedienung der Vollbild-App am Fernseher im Einzelnen (Namenszeile mit Tastatur, Warteschlangen-Fenster).
- **Bekannt:** Ein Auftrag von ShadowMountPlus sperrt „Konvertieren starten“ in der Prüfliste nicht. Der Start meldet dann, dass Aufträge nicht gleichzeitig laufen; das Einreihen in die Warteschlange funktioniert.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.82.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG und **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.81.0...v1.82.0
