# PS5 Cooling & System Center – Pro v1.86.5

## Highlights
- **Sanftere Lüfterregelung:** Der Lüfter folgt der Temperatur schon ab 10 K unter dem Ziel langsam, springt beim Aufholen nicht mehr und schwankt weniger. Neu: die **Lüfterkurve als Bild** (Browser-App und Vollbild-App), eine **Regelaufzeichnung** der letzten 30 Minuten und eine einstellbare **Obergrenze der Lüfterdrehzahl**.
- **Kraken Mount startet mit der App:** Es ist ab jetzt standardmäßig eingeschaltet (etwa 5 Sekunden nach dem Start der App) und lässt sich in der Seite ausschalten; das merkt sich die App. Eingebettet ist Kraken Mount v0.2.1.
- **Disc-Dump bequemer und sicherer:** Der Knopf heißt „Dumpen starten“, der Fortschritt erscheint als Meldung oben links, ein eigener Ordner lässt sich auch in der Vollbild-App eintippen, und ein Dump überschreibt oder löscht nie einen fremden Ordner.

## Neu
### Lüfter
- **Lüfterkurve:** Karte „Lüfterkurve“ im Reiter Kühlung (Browser) und Reiter „Lüfterkurve“ in der Vollbild-App: welche Drehzahl die Automatik bei welcher Temperatur verlangt, mit der Temperatur von jetzt.
- **Regelaufzeichnung:** Die letzten 30 Minuten der Regelung, eine Zeile je Sekunde, als Tabelle (CSV) herunterladen oder auf der Konsole speichern.
- **Obergrenze der Lüfterdrehzahl** (30–100 %, ab Werk 100 = keine Grenze): im Normalbetrieb geht der Lüfter nicht darüber. Die Notfallgrenze bleibt unberührt.
- **Warnschwelle folgt dem Ziel** (ab Werk an, wie bisher): lässt sich in den erweiterten Einstellungen ausschalten, dann gilt genau der Wert „Warnung ab“.
- Profil- und Zielwechsel stehen jetzt im Protokoll.

### Kraken Mount
- Startet von selbst mit der App, solange man es nicht in der Seite ausgeschaltet hat. Läuft es schon oder ist es nur gerade beschäftigt, sendet die App nichts ein zweites Mal.
- Läuft noch eine ältere Fassung als die eingebettete, steht ein Hinweis unter dem Schalter: Zum Wechsel aus- und wieder einschalten.
- **Kraken Mount v0.2.1:** Trophäen, Spielstand-Symbole und Hintergründe kommen auch bei FPKG-Spielen an; Zwischenspeicher und `.pkg`-Dateien werden für die Dienste der Konsole lesbar gemacht.

### Disc und Dump
- **„Dumpen starten“** statt „Starten“, wenn der Knopf das Spiel startet und gleich dumpt. Er bleibt grau, bis der Dump fertig ist, und ein Hinweis nennt die Wartezeit.
- **Fortschritt oben links** auf dem Bildschirm (Dump eines Spiels und Sichern eines Disc-Abbilds), alle 10 %.
- **Dump-Ordner** (Browser-App und Vollbild-App mit Bildschirmtastatur): Umlaute, Klammern, ein Schrägstrich am Ende oder ein voller Pfad werden aufgeräumt statt abgelehnt; ein ungültiger Name bleibt mit Grund im Feld stehen.
- **Dump Forge v0.5.6:** Trophäen melden sich auch bei FPKG, deren Dateien nur verschlüsselt im Container lagen; Packs mit mehr als 32 Teilen lassen sich schreiben.

### Spiele und Kacheln
- **Prüfrunde (War-Room-Audit):** Das Pausieren des Hintergrund-Spiels fasst nur noch echte Spiele an (nie eine Medien-App wie YouTube) und prüft kurz vorher, dass man nicht schon wieder im Spiel ist; Kraken Mount lässt sich in den ersten Sekunden nach dem Start mit der App sicher ausschalten, und ein Schutz verhindert eine Startschleife, falls es die Konsole je zum Absturz bringt; das Ausweich-Entpacken verliert „Abbrechen“ nicht mehr und zeigt die richtige Größe; PS4Mount speichert „aus“ sicher; viele kleinere Fehler (Anzeigen, Meldungen, Werkzeuge) behoben.
- **Packs auf den Spielkarten:** „zstd gepackt“ / „LZ4 gepackt“ steht jetzt bei **jedem** Format unter dem Titel, auch bei Abbildern (exFAT, ffpkg, ffpfs, ffpfsc), die nicht eingehängt sind; die App liest es beim Lesen der Abbild-Datei mit, ohne etwas einzuhängen. Im Browser mit der Zeile „Gepackt“ unter „Infos & Metadaten“, in der Vollbild-App in der Zeile des Spiels und im Infofeld.
- **FPKG bei gepackten Spielen:** Ist ein Spiel mit AMPR-Packs (zstd oder LZ4) gepackt, packt Dump Forge die Packs beim Umwandeln selbst aus: Kraken Mount nimmt kein Paket mit Packs an. Das Paket enthält dann das ungepackte Spiel (das Fenster nennt die Größe und prüft den Platz danach), die Quelle bleibt unverändert. Gesperrt bleibt „FPKG“ nur, wenn sich die Packs nicht prüfen lassen oder das Spiel zstd- und LZ4-Packs zugleich hat.
- **Spiele-Ansicht:** Die Vorgabe der Spielkarten ist wieder „groß“ (wie bisher); wer „kompakt“ gewählt hat, behält es (Browser der Konsole und Browser am PC, je Gerät gemerkt).
- **PS4Mount ist ab Werk eingeschaltet** (bei einer neuen Installation, ohne gespeicherte Einstellung), wie Kraken Mount. Wer es schon ausgeschaltet hat, behält das; der Schalter in Browser-Seite und Vollbild-App zeigt den echten Zustand.
- **Marke „FPKGMount“:** Spiele, die Kraken Mount (FPKGMount) eingehängt hat, tragen jetzt die Marke „FPKGMount“, wie die PS4Mount-Spiele die Marke „PS4Mount“ (Browser und Vollbild-App). Sie erscheint, solange Kraken Mount läuft.
- **Kachel nach einem Spiel ohne Fehlermeldung:** Die App pausiert ein Spiel im Hintergrund selbst, sobald der Startbildschirm vorn ist (Einstellung „Spiel im Hintergrund pausieren“, ab Werk an, in Browser und Vollbild-App). Tippst du danach die Kachel an (Browser oder Vollbild), startet sie ohne den Fehlstart und ohne die Meldung („Device busy“), die ShadowMountPlus sonst auslösen konnte. Die Konsole pausiert das Spiel beim Start einer Kachel ohnehin; die App tut es nur etwas früher. Fortsetzen geht wie sonst. An der Konsole gesehen mit einem PS4-Spiel aus PS4Mount (.94, mehrfach von Hand getestet) und einem FPKG-Spiel aus Kraken Mount (.52). Bleibt die Pause aus (Schalter aus, oder du tippst schon in der ersten Sekunde nach der PS-Taste), gilt weiter dieser Ausweg: Lehnt die Konsole den Start der Kachel bei laufendem Spiel ab, zeigt die App sofort „Bitte einen Moment Geduld: Die App wird gleich gestartet.“ und öffnet sie nach etwa 3 Sekunden von selbst (in den ersten 60 Sekunden, nachdem ein Spiel in den Hintergrund ging); ein zweites Drücken ist nicht mehr nötig, und sie öffnet sich nicht noch einmal, wenn man sie schließt. 
- Die Dump-Warteschlange versucht den Start eines Titels bis zu fünfmal (vorher dreimal).
- Browser-App, Spiele: Das Band zeigt nur noch, was läuft. Ergebnisse („fertig“, „abgebrochen“, „gescheitert“) stehen im Verlauf.

## Verbesserungen
- Der Spielstart schießt in der Drehzahl weniger über (Vorsteuerung um 3 Punkte gedämpft).
- Ein abgelehnter Kachelstart steht mit dem Wortlaut von ShadowMountPlus im Protokoll; ein gescheiterter Spielwechsel nennt den Grund in Klartext.
- Die Systemfunktionen der App wurden gegen die Bibliotheken von 43 Firmwares (1.00 bis 12.70) geprüft: Alle 174 fest gebundenen Funktionen sind überall vorhanden. Zwei Funktionsnamen, die es in keiner Firmware gab, sind korrigiert. Die Konsole liefert an dieser Stelle keinen Fernsehernamen; das Feld bleibt leer, Hersteller, Produktcode und Baujahr stehen einmal im Protokoll.
- Handbuch und FAQ in sieben Sprachen, Übersetzungen der neuen Texte in sechs Sprachen.

## Fehlerbehebungen
- **Entpacken ohne „Permission denied“:** Scheitert das Entpacken über ShadowMountPlus mit „Permission denied“ (zum Beispiel bei einem Abbild, dessen Ordner keine Schreibrechte haben), entpackt die App dasselbe Abbild selbst in denselben Zielordner und zeigt den Fortschritt im selben Fenster. Das Abbild wird dabei nicht gelöscht, auch wenn „Abbild danach löschen“ gewählt war. An der Konsole noch nicht bestätigt.
- **Komprimiertes Abbild (ffpfsc):** Das Fenster, das „Konvertieren“ bei einem solchen Spiel öffnet, heißt jetzt „… entpacken“ und zeigt wie das andere Konvertieren-Fenster, was entstehen kann: der Dump-Ordner ist wählbar, die übrigen Formate stehen ausgegraut mit dem Grund da (aus einem komprimierten Abbild nicht möglich: erst entpacken, dann den Ordner konvertieren).
- **Lüfter laut oder zu leise:** Im Profil „Kühl“ stieg der Lüfter bei Ziel 68 °C plötzlich auf 54 %; bei Ziel 78 °C blieb er auf 23 %, obwohl die CPU auf 80 °C stieg. Beides war dieselbe Ursache (Gerade statt Kurve) und ist behoben.
- **Eigener Dump-Ordner:** Ein selbst eingetippter Ordner wurde nicht benutzt, und der Dump startete danach nicht.
- **„Vorhandenen Dump ersetzen“** konnte einen Ordner löschen, der nur so hieß wie ein Dump (etwa einen Spielordner für ShadowMountPlus). Als Dump gilt jetzt nur ein Ordner mit seiner Info-Datei; alles andere bleibt unberührt, und ein Dump bricht ab, statt in einen fremden Ordner zu schreiben.
- **Spiel beenden während eines Dumps:** Beim Test stürzte die Konsole dabei ab (Kernel-Panic). Die App lehnt „Spiel beenden“ und den Spielwechsel ab, solange ein Dump läuft.
- **Browser-App, Bereich Disc:** Der Pfeil der Aufklapp-Zeilen erschien am Konsolen-Browser als Fragezeichen; er ist jetzt gezeichnet statt ein Textzeichen.
- **Vollbild-App, Lüfterkurve:** Das Bild beginnt bei der ersten Temperatur, für die die Automatik etwas verlangt, wie im Browser.
- **Kraken Mount ausschalten:** Der Schalter springt nicht mehr sofort auf „an“ zurück; er bleibt aus, und die Seite zeigt „wird beendet …“, bis Kraken Mount weg ist (Browser-App und Vollbild-App).
- **Kachel nach einem Spiel:** Nach dem Schließen der Meldung öffnete sich die Kachel ein zweites Mal, sobald man sie mit der O-Taste schloss. Das tut sie nicht mehr.

## Hinweise
- Beim ersten Start nach dem Wechsel auf Kraken Mount v0.2.1 baut es den Zwischenspeicher **jedes** Spiels einmal neu auf; das dauert und braucht freien Platz.
- Kraken Mount läuft als eigenes Programm weiter, wenn die App ersetzt wird; die App beendet es nie von selbst (ein Spiel kann davon laufen). Eine ältere laufende Fassung wird erst nach Aus- und Einschalten ersetzt.
- An der Konsole gesehen: die sanftere Lüfterkurve, der Disc-Dump beim Start des Spiels, der Pfeil im Disc-Bereich, Kraken Mount v0.2.1 mit Autostart (nach dem Start der App). An der Konsole gesehen außerdem: das Pausieren des Spiels im Hintergrund vor dem Kachelstart (PS4-Spiel aus PS4Mount, FPKG-Spiel aus Kraken Mount). Nur im Host-Test geprüft oder noch nicht an der Konsole bestätigt: der Neustart der Kachel nach einem Spiel (ohne zweites Öffnen; Ausweg, wenn die Pause ausbleibt), das Verhalten der Pause hinter dem Kontrollzentrum, das Ausschalten von Kraken Mount ohne Zurückspringen, die Fortschrittsmeldung oben links, der Autostart nach einem Neustart der Konsole, Dump Forge v0.5.6.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.5.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.86.4...v1.86.5
