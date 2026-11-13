# PS5 Cooling & System Center – Pro v1.83.0

## Highlights
- **FPKGMount:** Ein neuer Reiter unter Spiele (neben „PS4 PKG von USB“, in der Browser-App und der Vollbild-App) spielt (F)PKG-Spiele und Spielordner ohne Installation, mit **Kraken Mount** von quer3q. Die App bringt das Programm mit, startet und beendet es, zeigt seine Spiele und kann sie neu aufbauen oder (nach einer Rückfrage) löschen.
- **Vollbild-App: Dateimanager wie in der Browser-App.** Auswahlmodus mit Haken, „Alle Größen messen“ und ein Zeileneditor für Textdateien (bis 64 KiB, dort, wo Ändern erlaubt ist).
- **AMPR EMU NG 0.4.0** von quer3q ist eingebaut. Es stehen immer die **zwei neuesten Fassungen** zur Auswahl: 0.4.0 und 0.3.5.
- **Spielstände: nichts ist von selbst gewählt.** Man hakt selbst an, was gesichert werden soll (Browser-App und Vollbild-App).
- **Durchsicht nach v1.82.0:** Fehler und Randfälle bei Aufträgen, Warteschlange, Löschen und Editor behoben.

## Neu und geändert

### FPKGMount (Browser-App und Vollbild-App)
- **Kraken Mount** (quer3q, GPL-3.0, v0.0.1-beta) meldet Debug-FPKGs (`.pkg`) und Spielordner (roh, zstd, LZ4) aus `/data/homebrew`, `/data/etaHEN/games` und dem Ordner `homebrew` der USB-Laufwerke auf dem Startbildschirm an und startet sie mit ampr-emu-ng. Es ist ein eigenes Programm (in Rust); die App trägt die veröffentlichte ELF unverändert (SHA-256 geprüft) und bedient sie.
- **Der Reiter:** „Einschalten“ startet oder beendet Kraken Mount (Beenden wird abgelehnt, solange ein Spiel läuft), „Jetzt suchen“, die Liste mit Symbol, Titel-ID, Version, Größe, Laufwerk, Art und Zustand, je Spiel „Neu aufbauen“ und „Spiel löschen“ (fragt zuerst; löscht PKG-Datei bzw. Spielordner, Kachel und Zwischenspeicher, Spielstände bleiben), dazu sein Protokoll und „Neue Fassung von GitHub laden“ (nur auf Knopfdruck, mit Prüfsumme). Die Einstellungen von Kraken Mount bleiben in seiner eigenen Seite (Port 8097, Link im Reiter). Die Vollbild-App hat dieselbe Seite.
- **Keine Kachel für Kraken Mount selbst:** Die App unterdrückt die Kachel, die Kraken Mount sonst bei jedem Start anlegt; eine früher angelegte entfernt sie beim nächsten Start.
- **Hinweis:** ShadowMountPlus sucht in denselben Ordnern; für Ordner-Spiele läuft am besten nur eines von beiden. Kraken Mount ist eine Beta, als `.pkg` kann ein Spiel schlechter laufen als als zstd-Ordner.
- **Umbenannt:** „PS4 von USB“ heißt jetzt „PS4 PKG von USB“.

### Vollbild-App
- **Dateimanager mit Haken:** „Auswählen“ im Quadrat-Menü eines Eintrags (oder △ → „Mehrere auswählen“). X setzt oder nimmt den Haken und springt eine Zeile weiter, O beendet den Modus. Kopieren, Ausschneiden und Löschen wirken auf die Haken.
- **„Alle Größen messen“** (△ → Mehr) misst die Ordner der Ansicht nacheinander, mit Fortschritt; „Sortieren nach: Größe“ zählt gemessene Ordner mit.
- **Textdateien bearbeiten** (Quadrat-Menü → „Bearbeiten“): zeilenweise mit der Bildschirmtastatur, Zeilenmenü (davor einfügen, danach einfügen, löschen), △ speichert, O schließt. Zeilenenden (LF oder CRLF) und ein fehlender letzter Zeilenumbruch bleiben, wie die Datei sie hatte.
- **Spielstände:** Nichts ist beim Öffnen vorgewählt.

### Browser-App
- **Spielstände:** Nichts ist beim Öffnen vorgewählt; „Alle auswählen“ gibt es weiter.
- **Texteditor im Dateimanager:** Dateien mit CRLF behalten ihre Zeilenenden. Dateien, die nicht sauber UTF-8 sind oder gemischte Zeilenenden haben, werden nur angezeigt, weil Speichern sie verändert hätte; der Hinweis sagt, was zu tun ist.

### AMPR EMU NG 0.4.0
- Neu laut Veröffentlichung von quer3q: FPKG-Spiele (`.pkg`, experimentell), schnellere Paketlesung, LZ4-Pakete in parallelen Aufträgen, große Paketindizes nach Bedarf gelesen, Korrekturen für den Hänger beim Laden mit zstd-Paketen und den endlosen Ladebildschirm auf der internen SSD.
- Eingebettet sind 0.4.0 und 0.3.5 (je Normal- und Debug-Bau). Kommt eine neuere, wird sie ergänzt und die älteste entfernt. Der Knopf „AMPR EMU NG von GitHub laden“ und die Reparatur für zstd-Pakete nehmen die neueste.
- Die Paketformate (zstd, LZ4) sind unverändert. FPKG-Spiele baut oder startet die App nicht.

## Fehlerbehebungen
- **Vollbild-Dateimanager:** Eine verspätete Antwort für eine andere Datei kann nicht mehr in den Editor oder Betrachter der offenen Datei geraten; „Größe“-Sortierung baut die Zeilen sofort neu und der Fokus bleibt auf dem Eintrag; der Auswahlmodus endet nach Kopieren, Ausschneiden und Löschen.
- **Aufträge unter sich:** Ein Verschieben, das gestartet wird oder auf die Kachel wartet, zählt auch dort als laufend, wo ShadowMountPlus nicht gefragt wird. „Dump löschen“ und „Dump starten“ sowie die Dump-Warteschlange schließen sich gegenseitig aus. Der Disc-Dump prüft alle Aufträge der App, nicht nur drei.
- **Warteschlange:** Sie hält die Konsole auch zwischen zwei Aufträgen wach und gibt das Wachhalten frei, solange sie nur auf ShadowMountPlus wartet.
- **Spiel löschen:** Die Marker (`param.json`, `param.sfo`) fallen nur, wenn `sce_sys` ein echter Ordner auf dem Laufwerk des Spiels ist, damit ein Suchlauf den Rest nicht wieder anmeldet und eine Verknüpfung nie die Marker eines fremden Spiels trifft.
- **Konvertieren und Kopieren:** Der Quellstempel gibt seine Hash-Liste frei, wenn es zu viele Dateien zum Einzelvergleich sind; die Protokollzeile beim Start liest ihre eigene Kopie des Ziels.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- **An der PS5 Pro geprüft:** Der gleiche Code (nur die Versionsnummer ist neu) lief an der Konsole: Konvertier-Warteschlange mit zwei echten Spielen, Löschen eines Spiels über die App, Editor mit CRLF, gemischten Zeilenenden und anderer Kodierung, Antworten aller Seiten der Browser-App, die Auswahl der AMPR-EMU-NG-Fassungen und die Spielstände-Seite der Browser-App ohne Vorauswahl.
- **Auch an einer zweiten PS5 geprüft:** „Dump löschen“ und „Dump starten“ werden bei laufender Dump-Warteschlange abgelehnt (409 mit klarer Meldung), die Warteschlange läuft danach normal zu Ende; die Spielstände-Seite der Vollbild-App startet ohne Vorauswahl. Die Übersetzungen der neuen Texte wurden von einem unabhängigen Durchgang gegengelesen (Bedeutung, Grammatik, Begriffe) und an 14 Stellen verbessert; ein Muttersprachler hat sie nicht gelesen.
- **FPKGMount an der PS5 geprüft** (mit einem kleinen Testordner statt eines echten Spiels, dessen Reste danach gelöscht wurden): Starten und Beenden, Liste mit Symbol, Neu aufbauen, Löschen mit Rückfrage, die Abfrage nach einer neuen Fassung bei GitHub, keine eigene Kachel von Kraken Mount (eine vorhandene wird beim nächsten Start entfernt). **Nicht geprüft:** ein echtes FPKG (`.pkg`); ob ein solches Spiel startet, zeigt erst der Versuch. Kraken Mount meldet sich selbst als Version 0.1.0, die Veröffentlichung heißt v0.0.1-beta.
- **Bekannt:** Ein Auftrag von ShadowMountPlus sperrt „Konvertieren starten“ in der Prüfliste nicht (der Start meldet es dann); ein automatischer Disc-Dump wird bei einem laufenden anderen Auftrag nicht nachgeholt, sondern lässt sich von Hand starten.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.83.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG und **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.82.0...v1.83.0
