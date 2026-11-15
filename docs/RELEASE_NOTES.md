# PS5 Cooling & System Center – Pro v1.85.0

## Highlights
- **Drei Durchsichten der ganzen App** (Browser-App, Vollbild-App und Programm): mehr als 100 Befunde geprüft, gegengeprüft am Code und behoben – Absturz- und Datenrisiken, Fristen bei hängenden Hilfsprogrammen, atomare Dateien, Sperren zwischen Aufträgen.
- **Konvertieren nach `.ffpkg` plant jetzt selbst** (eigener UFS2-Plan): Spiele mit Dateinamen, die sich nur in der Groß-/Kleinschreibung unterscheiden, gehen jetzt; Fortschritt und Restzeit stimmen; die Abbilddatei hat keine Löcher mehr.
- **Dump:** Der PS4-Dump senkt jetzt auch die `param.sfo` (der Backport übersah sie bisher), `param.json`/`param.sfo` kommen erst am Ende an ihren Platz, ein altes Ziel bei „Ersetzen“ wird erst nach den Prüfungen gelöscht.
- **Geordnetes Beenden:** Sendet man eine neue ELF oder beendet die App, werden laufende Aufträge zuerst abgebrochen und räumen auf (an der PS5 mit einem laufenden Debug-FPKG-Auftrag geprüft).
- **Dump Forge bei der reinen ELF-Version:** Die App legt `/data/PS5-Cooling-Center/dumpforge/` bei jedem Start an; Anleitung im Handbuch und in der FAQ (7 Sprachen).

## Neu und geändert

### Konvertieren und Dump
- **`.ffpkg`:** Eigener UFS2-Plan statt des exFAT-Plans; Meldung der still weggelassenen Einträge (Verknüpfungen, Sonderdateien, `desktop.ini` …); Fortschritt in Abbild-Bytes. Das Format des Abbilds ist unverändert.
- **Aufräumen:** Beim ersten Konvertieren werden unfertige Teildateien und Signier-Ordner früherer Sitzungen (älter als eine Stunde) entfernt.
- **Dump:** entschlüsselte Dateien bekommen 0777, die `.sha256`-Liste hat jede Datei nur einmal, die Info-Datei wird atomar geschrieben, „Dump löschen“ zählt als Auftrag, die Warteschlange joint nicht mehr doppelt.
- **Namensvorlage, Quellstempel, Warteschlange:** Ein Eintrag, der zwischen Auswahl und Start entfernt wird, startet nichts mehr; ändert sich die Quelle, merkt es die App schon vor dem Zurücklesen.

### Kraken Mount und Dump Forge
- Verbindungen zu den Hilfsprogrammen haben beim Aufbau eine Frist; ein Auftrag endet nach 10 Minuten, wenn die Auftragsliste zu groß zum Lesen ist.
- „Antwortet nicht“ gilt nicht mehr als „läuft nicht“: kein zweites Einspielen, kein Beenden eines selbst gestarteten Programms; „Abbrechen“ wirkt auch während des Starts.
- `config.toml` und `version.txt` von Kraken Mount werden über eine Zwischendatei geschrieben.
- Eine geladene Fassung von Kraken Mount, die nach dem Start nicht lauscht, wird markiert: Der nächste Start nimmt die eingebettete Fassung, dieselbe Fassung wird nicht erneut geladen.
- Alte Fassungen von Dump Forge werden nach dem Laden einer neueren gelöscht.

### Wächter für eingehängte Spiele
- Das Aushängen vor einem Spielstart dauert höchstens 20 s; ein Titel, den man währenddessen neu einhängt, bleibt in der Haltliste; ein Fehler beim Schreiben der Haltdatei steht im Protokoll. Der Streifen nennt die stehende Uhr, solange ein Spiel läuft oder ein Auftrag der App arbeitet, und fragt dann nur noch einmal pro Minute.

### PS4Mount
- Ein Content-Name aus dem Kernel-Log darf nur noch die üblichen Zeichen enthalten (er gelangte ungeprüft in einen Pfad, der gelöscht wird). Ausschalten oder das Lösch-Fenster beendet eine laufende Gruppen-Registrierung; ein währenddessen entferntes Spiel wird nicht wieder eingetragen; „Update wählen“ und die Symbol-Route prüfen die Titel-ID streng.

### Browser-App
- FPKGMount: Die Knöpfe „Kraken Mount starten“/„Beenden“ und „Aktualisieren“ sind weg (der Schalter oben macht dasselbe, die Seite frischt sich selbst auf); das Ergebnis von „Neue Fassung von GitHub laden“ bleibt mit Uhrzeit auf der Karte stehen; neuer Knopf „Alle neu aufbauen“.
- PS4Mount: „Aktualisieren“ entfällt (das Protokoll behält „Neu laden“); die Einstellungen lassen sich erst nach dem Laden speichern.
- Datei-Editor mit Frist, „Liste laden“ im Payload-Store bleibt nicht gesperrt, weitere Meldungen in 6 Sprachen.

### Vollbild-App
- Cover-Bilder verdrängen das am längsten nicht gezeichnete, wenn der Speicher voll ist (Profil- und Danksagungsbilder bleiben); „Laden“ von FPKGMount und Dump Forge blockiert die übrigen Abfragen nicht mehr; das Ergebnis des Ladens bleibt auf der Seite stehen.

## Fehlerbehebungen
- **Dateimanager:** Platzprüfung ohne Überlauf, riesige Content-Length füllt `/data` nicht mehr; Editor-Speichern und „Neuer Ordner“ warten auf laufende Aufträge der App; eine vorhandene `.fm-tmp` wird nur als normale Datei überschrieben.
- **Abbilder:** Ein UFS-Inode, der größer ist als das Abbild, gilt als beschädigt; fremde `.ffpfsc`-Dateien mit Größen nahe 2^64 bestehen die Prüfung nicht mehr; AMPR-Index und Paketteile überschreiben nie ein inzwischen neu angelegtes Ziel.
- **Pakete:** Eine Kachel-Deinstallation und eine Paket-Installation schließen sich gegenseitig aus.
- **Downloads in den Speicher:** Der Puffer verdoppelt sich statt bei jedem Stück zu kopieren.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- **An zwei PS5 geprüft** (gleicher Code, nur die Versionsnummer ist neu): Abbruch während des Dump-Forge-Starts, Debug-FPKG-Lauf mit Abbruch beim Schreiben, Beenden mit laufendem Auftrag, `.ffpkg`-Konvertierung mit vollem Zurücklesen, Wächter mit stehender Uhr, FPKGMount- und PS4Mount-Seite.
- **Bekannt:** Ob die Konsole ein neu geschriebenes `.ffpkg` einhängt, ist weiter nicht bestätigt. Kraken Mount und Dump Forge sind fremde Programme und lauschen auf allen Schnittstellen (Hinweis in `SECURITY.md`). Spiel löschen und das Verschieben über ShadowMountPlus lassen sich beim Beenden nicht abbrechen.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.85.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.84.0...v1.85.0
