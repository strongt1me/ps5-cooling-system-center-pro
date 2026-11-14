# PS5 Cooling & System Center – Pro v1.84.0

## Highlights
- **Neues Konvertierformat „FPKG (Debug-.pkg)“** in der Browser-App und der Vollbild-App, geschrieben von **Dump Forge** (quer3q): mit Prüfliste, Schritten, Warteschlange und vier Kompressionsstufen. Dump Forge liegt in den Ordnern beider Kacheln oder wird mit einem Knopf geladen; es legt keine eigene Kachel an.
- **FPKGMount** (neu seit v1.83.0, das nicht einzeln veröffentlicht wurde): Ein neuer Reiter unter Spiele (neben „PS4 PKG von USB“, in der Browser-App und der Vollbild-App) spielt (F)PKG-Spiele und Spielordner ohne Installation, mit **Kraken Mount** von quer3q. Die App bringt das Programm mit, startet und beendet es, zeigt seine Spiele und kann sie neu aufbauen oder (nach einer Rückfrage) löschen.
- **Vollbild-App: Dateimanager wie in der Browser-App.** Auswahlmodus mit Haken, „Alle Größen messen“ und ein Zeileneditor für Textdateien (bis 64 KiB, dort, wo Ändern erlaubt ist).
- **AMPR EMU NG 0.4.0** von quer3q ist eingebaut. Es stehen immer die **zwei neuesten Fassungen** zur Auswahl: 0.4.0 und 0.3.5.
- **Spielstände: nichts ist von selbst gewählt.** Man hakt selbst an, was gesichert werden soll (Browser-App und Vollbild-App).
- **Durchsicht nach v1.82.0:** Fehler und Randfälle bei Aufträgen, Warteschlange, Löschen und Editor behoben.

## Neu und geändert

### Konvertieren: Debug-FPKG (Browser-App und Vollbild-App)
- **Das Format:** Neben ffpfsc, ffpkg und exFAT baut das Fenster „Konvertieren“ ein **Debug-FPKG** (`.pkg`) aus einem PS5-Spielordner oder einem Abbild; Kraken Mount (Reiter FPKGMount) hängt es ein. Es läuft wie die anderen Formate: Prüfung vor dem Start, Namensvorlage, Schritte mit Zeiten, Warteschlange, Abbrechen, Eintrag im Verlauf.
- **Motor ist Dump Forge** (quer3q, GPL-3.0, v0.5.5), ein eigenes PS5-Programm. Die App startet es für den Auftrag, gibt ihm die Quelle und das Ziel, zeigt seine Schritte und beendet es danach wieder. Die ELF (18 MB) steckt nicht in der App: Die beiden Kacheln bringen sie in ihrem Ordner `dumpforge/` mit, oder „Dump Forge laden“ im Fenster holt sie von GitHub (nur auf Knopfdruck, Prüfsumme aus `SHA256SUMS`) nach `/data/PS5-Cooling-Center/dumpforge/`; man kann sie auch selbst dorthin legen. Die neueste Version zählt.
- **Vier Stufen:** Schnell (empfohlen), Ausgewogen und Kleinste (laut Dump Forge rund 2,6 % kleiner, aber etwa 6- und 9-mal so lange), Unkomprimiert. „Gründlich“ lässt Dump Forge jedes Byte des Pakets noch einmal lesen, „Schnell“ prüft Aufbau und Stichprobe; es entsteht keine `.sha256`.
- **Ziel:** „Spielbar“ legt das Paket in den Ordner `FPKG-Games` des Laufwerks (den Kraken Mount durchsucht), „Sicherung“ nach `PS5-Sicherung/Spiele`. Die Prüfliste nennt vorab einen Namen über 63 Bytes (mehr nimmt Dump Forge nicht) und warnt, wenn Original und Paket dieselbe Titel-ID haben und das Original bei ShadowMountPlus eingehängt ist.
- **Keine Kachel von Dump Forge:** Dump Forge legt bei jedem Start eine Kachel an. Die App verhindert das wie bei Kraken Mount und „PS4 PKG von USB“ und entfernt eine früher angelegte.
- **Größe:** Jede Kachel ist durch die Datei etwa 18 MB größer (FAKE86921 rund 86 MB, FAKE86922 rund 51 MB entpackt). Wer nur die ELF der App benutzt, lädt Dump Forge mit dem Knopf.

### FPKGMount (Browser-App und Vollbild-App)
- **Kraken Mount** (quer3q, GPL-3.0, v0.0.1-beta) meldet Debug-FPKGs (`.pkg`) und Spielordner (roh, zstd, LZ4) aus `/data/homebrew`, `/data/etaHEN/games` und dem Ordner `homebrew` der USB-Laufwerke auf dem Startbildschirm an und startet sie mit ampr-emu-ng. Es ist ein eigenes Programm (in Rust); die App trägt die veröffentlichte ELF unverändert (SHA-256 geprüft) und bedient sie.
- **Der Reiter:** „Einschalten“ startet oder beendet Kraken Mount (Beenden wird abgelehnt, solange ein Spiel läuft), „Jetzt suchen“, die Liste mit Symbol, Titel-ID, Version, Größe, Laufwerk, Art und Zustand, je Spiel „Neu aufbauen“ und „Spiel löschen“ (fragt zuerst; löscht PKG-Datei bzw. Spielordner, Kachel und Zwischenspeicher, Spielstände bleiben), dazu sein Protokoll und „Neue Fassung von GitHub laden“ (nur auf Knopfdruck, mit Prüfsumme). Die Einstellungen von Kraken Mount bleiben in seiner eigenen Seite (Port 8097, Link im Reiter). Die Vollbild-App hat dieselbe Seite.
- **Keine Kachel für Kraken Mount selbst:** Die App unterdrückt die Kachel, die Kraken Mount sonst bei jedem Start anlegt; eine früher angelegte entfernt sie beim nächsten Start.
- **Eigener Ordner „FPKG-Games“:** Kraken Mount sucht nur in `/data/FPKG-Games` und in dem Ordner `FPKG-Games` der Laufwerke (die App trägt das beim ersten Start in seine Konfiguration ein, eine vorhandene bleibt unberührt). ShadowMountPlus sucht dort nicht, beide laufen nebeneinander; Ordner-Spiele und Abbilder bleiben bei ShadowMountPlus.
- **Hinweis:** Kraken Mount ist eine Beta, als `.pkg` kann ein Spiel schlechter laufen als als zstd-Ordner. Stellt man Kraken Mount auf Ordner um, die auch ShadowMountPlus durchsucht (zum Beispiel `/data/homebrew`), würde jedes Ordner-Spiel dort doppelt angemeldet.
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
- **FPKGMount an der PS5 geprüft** (mit einem kleinen Testordner statt eines echten Spiels, dessen Reste danach gelöscht wurden): Starten und Beenden, Liste mit Symbol, Neu aufbauen, Löschen mit Rückfrage, die Abfrage nach einer neuen Fassung bei GitHub, keine eigene Kachel von Kraken Mount (eine vorhandene wird beim nächsten Start entfernt). Ein echtes Debug-FPKG (Alex Kidd, mit Dump Forge am PC gebaut) wurde angemeldet und startet; die Kachel zeigt ein Vorhängeschloss, weil Kraken Mount die `param.json` unverändert anmeldet (`upgradable`), ShadowMountPlus schreibt dort `standard`. Kraken Mount meldet sich selbst als Version 0.1.0, die Veröffentlichung heißt v0.0.1-beta.
- **Debug-FPKG an der PS5 geprüft** (zwei Test-PS5): ein 1,5-GB-Spiel wurde in etwa 35 Sekunden zu einem 403-MB-Paket, von Dump Forge geprüft; Abbrechen räumt auf; zwei Aufträge in der Warteschlange (Schnell mit schneller Prüfung, Unkomprimiert mit gründlicher Prüfung) liefen durch; Dump Forge wird in den Ordnern beider Kacheln gefunden; beide Kacheln starten mit der größeren Datei; die Kachel von Dump Forge ist weg. **Auch das Anmelden eines von der App gebauten Pakets in FPKGMount ist geprüft** (Alex Kidd; das Spiel war vorher bei ShadowMountPlus ausgehängt, weil beide dieselbe Titel-ID haben): Kraken Mount meldete das Paket an, das Spiel startete und lief, die Kachel zeigte kein Vorhängeschloss. Danach wurden Paket und Anmeldung wieder entfernt und das Spiel bei ShadowMountPlus wieder eingehängt. Warum das früher mit Dump Forge am PC gebaute Paket ein Schloss zeigte und dieses nicht, ist nicht geklärt.
- **Bekannt:** Ein Auftrag von ShadowMountPlus sperrt „Konvertieren starten“ in der Prüfliste nicht (der Start meldet es dann); ein automatischer Disc-Dump wird bei einem laufenden anderen Auftrag nicht nachgeholt, sondern lässt sich von Hand starten.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.84.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG und **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.82.0...v1.84.0
