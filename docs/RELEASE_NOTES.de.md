# PS5 Cooling & System Center – Pro v1.95.0

## Highlights
- **Kraken Mount 0.3.2 (quer3q):** FPKGMount bringt die neue Fassung mit: Das Schließen eines Spiels hängt nicht mehr, Apps, die beim Beenden eine zweite ELF starten, laufen aus einer `.pkg`, Homebrew sieht die Schriften der Konsole, und ampr-emu-ng 0.5.1 ist dabei. Seine eigene Update-Abfrage ist in der Fassung der App abgeschaltet – ein Update über seine Seite käme unangepasst und würde auf ShadowMountPlus warten; neue Fassungen kommen weiter über den Knopf der App.
- **NTFS-Laufwerke sind sicherer:** Ein Stick, der abgezogen wird, während die App in einem großen Ordner Dateien anlegt oder löscht, verliert keine vorhandenen Namen mehr. Präparierte Sticks bringen den Treiber nicht mehr dazu, über seine Puffer hinaus zu schreiben oder Hunderte MB zu belegen, und die Dateien von Windows unter `$Extend` bleiben unberührt.
- **Eine komplette Prüfung der App:** Korrekturen an Lüfterregelung, Aufträgen, Spielständen, Weboberfläche und Vollbild-App – siehe unten.
- **„Ersetzen“ verliert die alte Datei nicht mehr:** Scheiterte eine Kopie, die eine Datei auf der Konsole ersetzen sollte, oder wurde sie abgebrochen, war auch die alte Datei weg.

## Neu
### Lüfterregelung
- **Notfallgrenze:** Es gilt der gespeicherte Wert oder Ziel + 4 °C, je nachdem, was höher ist; die Einstellungen heben den gespeicherten Wert nicht mehr für immer an (nach einem einmaligen Ziel von 91 °C blieb der Lüfter bis 95 °C an der Komfortgrenze gedeckelt).
- **Einzelne Ausreißer:** Ein Messwert, der mehr als 12 °C vom vorigen abweicht, wird einen Durchlauf lang zurückgehalten; bestätigt ihn der nächste, gilt er. Ein +40-K-Ausreißer alle 150 s lässt den Lüfter nicht mehr minutenlang hochlaufen.
- **Beobachten:** Eine von Hand gesetzte Schwelle bleibt gesetzt (auch nach einem Neustart); ohne sie behält die Konsole ihre eigene Einstellung – jetzt auch beim Start.
- **Wächter und Beenden:** „Lüfter voll“ entschuldigt eine Übertemperatur höchstens 60 s; beim Beenden der App geht der Lüfter in jedem Fall an die Konsole zurück.

### Spiele und Aufträge
- **LZ4/zstd-Pakete entpacken:** Scheitert das Entpacken, wird es abgebrochen oder weicht das Ergebnis von den Paketen ab, nimmt der Auftrag die Dateien und Ordner, die er hingelegt hat, wieder weg – das Spiel liegt wie vorher in Paketen. An der Konsole mit einer Testkopie erprobt (Abbruch bei einem Drittel, LZ4 und zstd: nichts blieb zurück).
- **„Als neuer Ordner daneben“:** Scheitert, statt „fertig“ zu melden, wenn eine Datei fehlt; `param.json`/`param.sfo` kommen zuletzt; der AMPR-Index des Originals bleibt genau, wie er war.
- **Dump Forge:** Das Ergebnis prüft die App selbst, bevor ein Auftrag als fertig gilt und bevor „Original danach“ das Original löschen darf.
- **Kraken Mount 0.3.2:** Siehe oben. „Neu aufbauen“ bleibt gesperrt, bis ein angeforderter Neuaufbau fertig ist.

### Spielstände
- **Sicherungen enthalten die Datenbank der PS5-Spielstände** (`savedata_prospero`), nicht nur die der PS4 – an den Firmwares 1.00 bis 12.70 geprüft.
- Reste eines unterbrochenen Einspielens werden beim Start aufgeräumt; setuid-/setgid-Bits aus einer ZIP fallen weg; ein Zurückspielen sagt, wenn überzählige Dateien sich nicht entfernen ließen.

### Dateimanager: NTFS
- **Unterbrochene Index-Änderungen:** Die Schreibreihenfolge des Upstream-Fixes, dazu Ordnungspunkte für den Schreib-Cache der App. Ein Test bricht jedes Anlegen und Löschen nach jedem Schreibvorgang ab: Bei 0 von 901 Abbruchpunkten geht ein Name verloren (vorher bei 11 von 1.094).
- **Präparierte Sticks:** Datensätze werden wie bei ntfs-3g geprüft; eine Attributliste, die sich wiederholt, eine unsinnige Indexgröße oder ein fremder Datensatz in der Liste einer gelöschten Datei werden abgewiesen.
- **Verschieben:** Das Ziel ist auf dem Stick, bevor das Original gelöscht wird; Systemeinträge von Windows werden nicht verschoben.
- **Namen:** Zu lange Namen werden abgewiesen statt gekürzt; COM0, LPT0 und COM¹–LPT³ bekommen einen Namen, den Windows öffnen kann.

### Weboberfläche
- Zusammengesetzte Zeilen werden übersetzt, die Schnellwahl bricht in schmalen Karten keine Wörter mehr, Knöpfe und rote Schrift sind in jedem Design lesbar, russische Mehrzahlformen (116 Meldungen), 82 nicht mehr gebrauchte Wörterbucheinträge entfernt.

### Vollbild-App
- Die Laufwerkstelemetrie unter System → Experte geht wieder; lange Sprachen passen (Protokoll, Karten, Fenster, Seitenleiste 390 px, Spiele-Unterreiter, Details-Blatt); zwölf fehlende Texte ergänzt.

## Fehlerbehebungen
- **„Ersetzen“ beim Kopieren auf die Konsole:** Die alte Datei bleibt, wenn die Kopie scheitert oder abgebrochen wird (Quellen von der Konsole und von NTFS).
- **Server:** Ein NUL im Anfragekopf oder `\u0000` im Anfragekörper wird sofort abgewiesen; Downloads müssen wie jede andere Antwort mindestens 64 KB in 30 s schaffen (ein tröpfelnder Leser hielt eine Verbindung und auf NTFS das Laufwerk unbegrenzt).
- **Start der App:** Eine ältere Ausführung, die über HTTP gefunden wird, wird nur beendet, wenn sie wirklich ein Payload ist.

## Hinweise
- **An der Konsole erprobt:** Pro (Firmware 12.00): Rücknahme beim Entpacken mit LZ4 und zstd, Kraken Mount 0.3.2 mit einem Spiel (gestartet, gelaufen, beendet; Update-Abfrage aus, keine eigene Kachel). Slim: die NTFS-Fixes auf einem Test-Stick (300 Dateien angelegt, alle zurückgelesen, 150 gelöscht, der Rest zurückgelesen, die neuen Namensregeln).
- **Kraken Mount:** Ein laufendes Kraken Mount bleibt die alte Fassung, bis es im Reiter FPKGMount aus- und wieder eingeschaltet wird; der erste Start von 0.3.2 baut die Zwischenspeicher der `.pkg`-Spiele einmal neu auf.
- 46 Host-Tests mit der Lüfter-Simulation, 392 Tests der Vollbild-App und die Wörterbuchprüfung bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.95.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.94.0...v1.95.0
