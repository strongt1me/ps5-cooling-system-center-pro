# Changelog

## v1.83.0 – 13.11.2026

### FPKGMount: (F)PKG-Spiele mit Kraken Mount, und „PS4 von USB“ heißt jetzt „PS4 PKG von USB“ (13.11.2026, auf Wunsch)
- **Neuer Reiter „FPKGMount“ (Spiele)** in der Browser-App und der Vollbild-App, neben „PS4 PKG von USB“. **Kraken Mount** von quer3q (GPL-3.0, v0.0.1-beta) meldet (F)PKG-Spiele (Debug-FPKG, `.pkg`) und Spielordner (roh, zstd, LZ4) ohne Installation auf dem Startbildschirm an und startet sie mit ampr-emu-ng; dafür war in AMPR EMU NG 0.4.0 die FPKG-Unterstützung angelegt. Kraken Mount ist ein eigenes Programm in Rust mit eigenem PKG-Leser und lässt sich nicht in C umschreiben wie PS4Mount: Die App trägt die veröffentlichte `kraken-mount.elf` (4,3 MB, unverändert, SHA-256 geprüft, gepackt 2,6 MB) und bedient sie.
- **Was der Reiter kann:** Kraken Mount starten und beenden (der Schalter „Einschalten“; Beenden wird abgelehnt, solange ein Spiel läuft), „Jetzt suchen“, die Liste seiner Spiele (Symbol, Name, Titel-ID, Version, Größe, Laufwerk, FPKG oder Ordner, Zustand: wartet, wird geprüft, wird aufgebaut, bereit, läuft, fehlgeschlagen, doppelt), je Spiel **Neu aufbauen** und **Spiel löschen** (fragt zuerst und nennt Namen und Quelle; löscht PKG-Datei bzw. Spielordner, Kachel und Zwischenspeicher, Spielstände bleiben), „Alle neu aufbauen“, sein Protokoll und „Neue Fassung von GitHub laden“ (holt die neueste Veröffentlichung nur auf Knopfdruck, per HTTPS, prüft sie gegen `SHA256SUMS`; der nächste Start nimmt sie). Die Einstellungen von Kraken Mount (Suchordner, Fakelib, Backports) bleiben in seiner eigenen Seite auf Port 8097 (Link im Reiter). Die Vollbild-App hat dieselbe Seite (Quadrat: Neu aufbauen, Löschen; Dreieck: Start/Beenden, Suchen, alle neu aufbauen, neue Fassung, Protokoll).
- **Aufbau:** `src/kraken.c` (Starten über den ELF-Lader 127.0.0.1:9021, Fragen auf 127.0.0.1:8097, Beenden, Löschen nur für ein Spiel der aktuellen Liste, Laden einer neuen Fassung nach `/data/PS5-Cooling-Center/kraken/`), `GET`/`POST /api/v1/kraken`, `GET /api/v1/kraken/icon`; `third_party/kraken_mount/` mit `ORIGIN.md`; die ELF wächst durch den Baustein auf etwa 13,6 MB (keine harte Grenze, an der Konsole geprüft). Neue Texte in den sechs Wörterbüchern der Browser-App und der Vollbild-App, Handbuch (7 Sprachen), README (7 Sprachen), `docs/API.md`, `THIRD_PARTY_NOTICES.md`, Credits (quer3q), `tools/upstream.json` (Baustein `kraken-mount`).
- **Keine Kachel für Kraken Mount selbst (auf Wunsch):** Kraken Mount legt bei jedem Start eine Kachel (KRKN00001) für seine Seite an. Die App verhindert das wie bei „PS4 PKG von USB“, wo es auch keine Kachel gibt: Vor dem Start legt sie an der Stelle `/user/app/KRKN00001` eine leere Datei an (Kraken Mount lässt den Platz dann laut seinem Quelltext in Ruhe), und eine früher angelegte Kachel entfernt sie mit der Deinstallation der Konsole samt ihrer Markierung `tile.ok`. Das Programm selbst bleibt unverändert.
- **Vorhängeschloss an der Kachel (an der PS5 geklärt):** Kraken Mount meldet die `param.json` eines Spiels unverändert an. Steht darin `applicationDrmType: "upgradable"` (viele Abbilder), zeigt die Konsole die Kachel mit Schloss, das Spiel startet trotzdem. ShadowMountPlus schreibt beim Anmelden `"standard"` hinein, leert `versionFileUri` und entfernt `originContentVersion` und `targetContentVersion`; die Konsolendatenbank zeigt den Unterschied (`appDrmType` 5 gegen 2). Mit der von Hand so geänderten `param.json` einer Testkopie verschwand das Schloss. Das Verhalten liegt in Kraken Mount (unverändert eingebettet); ein Hinweis steht im Handbuch, eine Meldung an den Autor ist vorbereitet.
- **Hinweis zu ShadowMountPlus:** Kraken Mount sucht standardmäßig in denselben Ordnern (`/data/homebrew`, `/data/etaHEN/games`, USB `homebrew`); für Ordner-Spiele läuft am besten nur eines von beiden. Die Schnittstelle von ShadowMountPlus (Port 10101) und die Einstellungen von Kraken Mount fasst die App nicht an.
- **Umbenannt:** Der Reiter „PS4 von USB“ heißt jetzt „PS4 PKG von USB“ (Oberfläche, Vollbild-App, Handbuch, README, Wörterbücher).

### Spielstände: nichts ist von selbst gewählt (13.11.2026, auf Wunsch)
- **Übersetzungen der neuen Texte** (34 je Sprache) von einem unabhängigen Durchgang gegengelesen und an 14 Stellen verbessert (zum Beispiel „Verwerfen“ im Schwedischen, „neuere Fassungen“ im Französischen, „Haken“-Texte im Englischen).
- Auf der Seite „Spielstände“ der Browser-App und der Vollbild-App ist beim Öffnen **kein Titel mehr vorgewählt**, auch wenn alle gesichert werden könnten; man hakt selbst an, was gesichert werden soll. „Alle auswählen“ gibt es weiter, der Hinweis sagt „Wähle mindestens einen Titel.“

### Vollbild-App: Dateimanager mit Haken-Auswahl, „Alle Größen messen“ und Zeileneditor (13.11.2026)
Anlass: Wunsch des Nutzers, dass die Vollbild-App im Dateimanager dasselbe kann wie die Browser-App.
- **Auswahlmodus mit Haken:** „Auswählen“ im Quadrat-Menü eines Eintrags (oder △ → „Mehrere auswählen“) zeigt vor jedem Eintrag ein Kästchen. X setzt oder nimmt den Haken und springt eine Zeile weiter, so lassen sich viele Einträge schnell nacheinander abhaken; O beendet den Modus (die Haken bleiben), die untere Leiste und ein Hinweis auf der rechten Karte sagen es. Kopieren, Ausschneiden und Löschen wirken auf die Haken. Ein Ordnerwechsel beendet den Modus.
- **„Alle Größen messen“** (△ → Mehr): misst die Ordner der Ansicht nacheinander (die App zählt je Ordner höchstens 8 Sekunden), mit Fortschritt auf der rechten Karte und „Messen beenden“; bereits gemessene Ordner werden übersprungen. „Sortieren nach: Größe“ zählt gemessene Ordner jetzt mit ihrer Größe.
- **Textdateien bearbeiten** (Quadrat-Menü → „Bearbeiten“, bis 64 KiB und nur, wo Ändern erlaubt ist, wie in der Browser-App): Die Datei wird zeilenweise bearbeitet. X öffnet die Bildschirmtastatur für die Zeile, Quadrat das Zeilenmenü (davor einfügen, danach einfügen, löschen), △ speichert, O schließt (mit Rückfrage bei ungespeicherten Änderungen). Geänderte Zeilen sind markiert; Zeilenenden (LF oder CRLF) und ein fehlender letzter Zeilenumbruch bleiben, wie die Datei sie hatte. Gespeichert wird über `POST /api/v1/files/write` (Temp-Datei, `fsync`, Umbenennen); der Editor bleibt offen, bis die App antwortet. Neu: `src/ps5cc/files_edit.*` (Test `ps5cc_files_edit_test`, 8 Tests).
- Neue Texte in den sechs Wörterbüchern der Vollbild-App; der Hinweis auf die Tasten steht in der unteren Leiste.
- **Dateimanager: Orte und Hinweise wieder wie vorher (13.11.2026, auf Wunsch):** Die Schnellzugriffe auf `/user`, `/system_data`, `/system` und `/system_ex` und die geänderten Hinweistexte („gesperrt, ändern geht darunter“, „Hier gesperrt“ …) sind zurückgenommen (Commit 966cb25 rückgängig gemacht). Browser- und Vollbild-App zeigen dieselben Orte wie vorher: usbN, Interne SSD (`/data`), Ganzes System. Eine Sperre der Systemordner gegen Löschen, Verschieben und Umbenennen wurde probiert und wieder verworfen.

### Fremdbausteine: AMPR EMU NG 0.4.0 (13.11.2026)
- **AMPR EMU NG (quer3q) v0.3.5 → v0.4.0**, Wunsch des Users: Es stehen immer die **zwei neuesten Fassungen** zur Auswahl. Eingebettet sind jetzt 0.4.0 und 0.3.5 (je Normal- und Debug-Bau, vier Dateien); kommt eine neuere, wird sie ergänzt und die älteste entfernt (Regel in `tools/upstream.json`). Der Knopf „AMPR EMU NG von GitHub laden“ und die Reparatur für zstd-Pakete nehmen die neueste (0.4.0). Dateien geprüft: SHA-256 gleich `SHA256SUMS`, SELF-Kopf, Kennung `ampr-emu-ng 0.4.0`.
- Neu in 0.4.0 laut Veröffentlichung: FPKG-Spiele (`.pkg`, experimentell; gestartet mit kraken-mount statt ShadowMountPlus), schnellere Paketlesung, LZ4-Pakete in parallelen Aufträgen, große Paketindizes nach Bedarf gelesen, Korrekturen für den Hänger beim Laden mit zstd-Paketen und den endlosen Ladebildschirm auf der internen SSD. Die Paketformate (zstd, LZ4) sind unverändert, an `src/zstdpack.c` und `src/lz4pack.c` ändert sich nichts. FPKG-Spiele baut oder startet die App nicht.
- Nachgezogen: Handbuch (7 Sprachen), README (7 Sprachen), Credits-Text und Hinweistext in Browser- und Vollbild-App, `THIRD_PARTY_NOTICES.md`, `third_party/ampr_emu/ORIGIN.md`, `docs/API.md`, Vorschaudaten der Vollbild-App.

### Durchsicht nach v1.82.0: Fehler und Randfälle (13.11.2026)
- **Vollbild-Dateimanager:** Die Antwort auf „Datei lesen“ füllt den Editor nur noch, wenn sie zur offenen Datei gehört (eine verspätete Antwort für eine andere Datei konnte sonst deren Text in die falsche Datei speichern); „Größe“-Sortierung baut die Zeilen sofort neu; der Auswahlmodus endet nach Kopieren, Ausschneiden und Löschen; ein Neustart des Messens räumt den alten Stand weg. Neuer Test `OnlyTheAnswerForTheOpenFileIsTaken` (346 Tests der Seite grün).
- **Konvertieren:** Der Quellstempel gibt seine Hash-Liste frei, sobald es zu viele Dateien zum Einzelvergleich sind; die Protokollzeile beim Start liest ihre eigene Kopie des Ziels, nicht den Auftragssatz, den der Faden schon ändert (gleich bei Kopieren).
- **Aufträge unter sich:** Was die App selbst von einem Verschieben hält (wird gestartet, wartet auf die Kachel), zählt auch dort, wo ShadowMountPlus nicht gefragt wird (`ps5tm_gamemove_busy_local`, Test in `jobreg`, 201 Prüfungen). Der Disc-Dump prüft alle Aufträge der App, nicht nur drei. „Dump löschen“ und „Dump starten“ schließen sich gegenseitig aus. Die Warteschlange hält die Konsole auch zwischen zwei Aufträgen wach. Beim Löschen eines abgemeldeten Spiels fallen zuerst `param.json` und `param.sfo`, damit ein Suchlauf den halben Rest nicht wieder anmeldet. `gamedelete` liest `waited_tile` unter der Sperre.
- **Zweites Audit (4 Prüfer) – behoben:** Die Marker (`param.json`, `param.sfo`) fallen nur, wenn `sce_sys` ein echter Ordner auf dem Laufwerk des Spiels ist (sonst hätte eine Verknüpfung die Marker eines fremden Spiels getroffen). „Dump löschen“ wird abgelehnt, solange die Dump-Warteschlange läuft (sonst scheiterte oder „übersprang“ deren nächster Eintrag). Die Konvertier-Warteschlange gibt das Wachhalten der Konsole frei, solange sie nur auf ShadowMountPlus wartet. `ps5tm_gamemove_busy_local` zählt auch ein von ShadowMountPlus schon angenommenes, von der App beobachtetes Verschieben. Browser-Editor: Dateien mit CRLF behalten ihre Zeilenenden; Dateien, die nicht sauber UTF-8 sind oder gemischte Zeilenenden haben, werden nur angezeigt (Speichern hätte sie verändert). Vollbild-Dateimanager: der Betrachter nimmt nur die Antwort für seine Datei; nach dem Sortieren nach Größe bleibt der Fokus auf dem Eintrag.
- **Bewusst nicht geändert:** Der automatische Disc-Dump wird bei einem laufenden anderen Auftrag nicht nachgeholt (Ereignis nennt den Grund, von Hand startbar). `files/write` prüft keine anderen Aufträge und keine zwischenzeitliche Änderung der Datei (kleine, atomare Textänderung; kein Laufwerksplatz-Wettbewerb). Ein nach Absturz übrig gebliebenes Temp-Stück des Editors wird nicht aufgeräumt. Ob `/system` schreibgeschützt eingehängt ist, wird nicht vorab geprüft (Fehler kommt erst beim Schreiben).

---

## v1.82.0 – 13.11.2026

### Konvertieren: Prüfung vor dem Start, Name, Schritte, Warteschlange (13.11.2026)
Anlass: ein Vergleich mit dem Werkzeug „Dump Forge“. Was dort schon der Hauptweg ist (ein Strom ohne Zwischenordner, Prüfsumme beim Schreiben, Stichprobenprüfung, Datei erst unter unbekanntem Namen), kann diese App seit 1.46.0 bis 1.79.0 schon; hier kommt, was fehlte. Browser-App und Vollbild-App bekommen dasselbe.
- **Prüfung vor dem Start (`ps5tm_gameconvert_check`, `POST /api/v1/library/convert/check`):** Das Fenster zeigt alle Befunde auf einmal als Liste (Quelle lesbar, Platz, FAT32-Grenze, Name am Ziel schon vergeben, Spiel läuft, PS4-Ziel, Namen/Pfade nah an der Grenze, anderer Auftrag läuft). Rot sperrt „Konvertieren starten“ und „Zur Warteschlange“, Gelb warnt nur; ein laufender anderer Auftrag sperrt nur den Start. Die Abfrage nutzt den Plan, den das Fenster gerade geholt hat (kein zweiter Gang durch das Spiel) und fragt ShadowMountPlus nicht.
- **Name der Ausgabe nach Vorlage (`src/cvname.c`, Host-Test `test/cvname`):** `{titel}`, `{id}`, `{version}`, `{quelle}`; leer = Name des Ordners wie bisher. Zeichen, die ein Laufwerk nicht nimmt, werden zu `_`, Endungen aus Leerzeichen und Punkten entfallen; mit Endung und der Prüfsummen-Datei höchstens 255 Bytes. Das Fenster zeigt den fertigen Namen und seine Länge; der Start nimmt das Feld `name`.
- **Stufenzeile (`stages` im Status):** Vermessen, Schreiben (bei ffpfsc Komprimieren), Prüfen, Umbenennen mit der Zeit je Schritt, in Browser-App und Vollbild-App.
- **Warteschlange (`src/convqueue.c`, `/api/v1/library/convert/queue`):** Bis zu 24 Aufträge nacheinander; läuft schon ein anderer Auftrag, wartet der Eintrag und beginnt danach von selbst (ohne ShadowMountPlus dabei zu fragen). Ein Fehler hält sie nicht an („Erneut versuchen“), „Abbrechen“ des laufenden Auftrags lässt sie weiterlaufen, „Warteschlange stoppen“ hält nur weitere Starts an. Nichts wird gelöscht; sie liegt im Speicher und ist nach einem Neustart der App leer. Eine Meldung der Konsole am Ende.
- **Quelle während des Laufs geändert (`gameconvert.c`, `stamp_*`):** Vor dem Schreiben nimmt die App einen Fingerabdruck der Quelle (Zahl und Größe der Dateien und ein Hash aus Pfad, Größe und Zeit je Datei, bis 400 000 Dateien mit Namen); vor dem Umbenennen vergleicht sie ihn. Weicht er ab, wird das Ergebnis nicht übernommen und die Meldung nennt die erste abweichende Datei.
- Neue Texte in den sechs Wörterbüchern der Browser-App, Handbuch in sieben Sprachen, `docs/API.md`.

### Durchsicht und Konsolentest der Warteschlange (13.11.2026)
- **Durchsicht seit v1.81.0** (acht Blickwinkel, acht Funde, alle behoben):
  - Ein Eintrag, der auf einen anderen Auftrag wartet, steht auf „wartet“ (nicht „läuft“) und lässt sich entfernen; Stoppen beim Warten lässt ihn „wartet“ und löst keine Meldung „0 von 1 fertig“ aus.
  - `gameconvert.c` merkt sich, wie der letzte Auftrag eines Eintrags endete (`ps5tm_gameconvert_queue_result`): ein Auftrag, auf den innerhalb der Abfragesekunde ein anderer Start folgt, gilt nicht mehr als gescheitert.
  - Blockiert einer der eigenen Aufträge der App, startet die Warteschlange nichts und fragt ShadowMountPlus nicht; blockiert ShadowMountPlus selbst (Verschieben, Entpacken), wird der Start nur alle 30 Sekunden versucht.
  - Der Plan-Cache der Prüfung hält die letzten vier Titel statt einen; der zweite Blick des Quellstempels speichert keine Hashes mehr; die Zeile „Quelle lesbar“ nennt „als exFAT-Abbild“ nur beim Format exFAT (neuer Wörterbuch-Eintrag in zwölf Dateien); toter Code entfernt.
- **An der PS5 Pro geprüft** (nur lesend, Ergebnisse danach gelöscht): zwei echte Spiele nacheinander in der Warteschlange (Matchbox 5,6 GB als ffpfsc in 176 s, BUBBLE BOBBLE 0,9 GB in 29 s, beide „fertig und geprüft“, kein Fehlalarm des Quellstempels); Prüfliste mit Vorlage und Standardname; Warten bei laufendem eigenem Auftrag und bei einem Entpacken durch ShadowMountPlus (Eintrag „wartet“, startet danach von selbst); Entfernen und Stoppen beim Warten; Abbrechen eines Auftrags aus der Warteschlange; Ergebnis eines Auftrags, auf den sofort ein fremder Start folgt; Plan-Cache mit zwei Titeln (zweite Runde 0,01 s statt 1,08 s).
- **Von Hand angesehen:** Nach dem Senden an die Konsole sah die App für den Nutzer „soweit gut“ aus. Nicht einzeln durchgespielt ist die Bedienung der Vollbild-App am Fernseher (Namenszeile mit Tastatur, Prüfliste, Stufenleiste, Warteschlangen-Fenster; geprüft sind die Daten, 337 Unit-Tests und die Vorschaubilder).
- **An einer Testkopie geprüft** (ein echtes PS5-Spiel, 0,97 GB, mit eigener Titel-ID und 12 800 Dateien; deine Spiele blieben unberührt): Eine unveränderte Quelle gibt keinen Fehlalarm (der letzte Schritt mit Quellprüfung und Umbenennen dauerte 156 ms). Wird während des Schreibens eine Datei hinzugefügt oder ersetzt, scheitert der Auftrag mit „Die Quelle hat sich während des Konvertierens geändert: <Datei>“, und kein Ergebnis bleibt liegen; eine gelöschte Datei lässt ihn mit einem Lesefehler scheitern. Ein Abbruch im letzten Schritt (Quellprüfung vor dem Umbenennen) wirkt: der Auftrag endet als „abgebrochen“ ohne Ergebnis.
- **Verbesserungen nach der Gegenprüfung** (Audit mit vier Fachprüfern und adversarialer Prüfung, 14 von 22 Befunden bestätigt, alle klein):
  - **Prüfliste (F-31):** Ein Treffer im Plan-Cache hält ihn frisch (vorher verfiel er nach 30 Minuten trotz Gebrauch); die Prüfung vermisst höchstens ein Spiel zugleich (sonst „Das Spiel wird gerade vermessen“); die Browser-App fragt nie zwei Prüfungen gleichzeitig.
  - **Abbrechen (F-37):** `POST /api/v1/library/convert/cancel` nimmt `{"queue_id":n}` und bricht nur den Auftrag ab, den die Oberfläche sah (409 `other_convert`, wenn inzwischen der nächste der Warteschlange läuft); Browser-App und Vollbild-App senden es. Ein Eintrag, dessen Start gerade angefragt wird, lässt sich nicht entfernen (409).
  - **Namensvorlage (F-46):** Zu lange Vorlagen (ab 200 Bytes) werden überall abgelehnt statt gekürzt, Prüfung und Start nutzen denselben Puffer; ungültiges UTF-8, C1-Steuerzeichen, Null-Breite- und Richtungszeichen (U+200B-200F, U+2028-202E, U+2060-2069, U+FEFF) werden zu `_` (Test `test/cvname`, 28 Prüfungen).
  - **Abbruch in der letzten Quellprüfung (F-43):** Der Lauf des Quellstempels beachtet den Abbruch und vor dem Umbenennen wird er noch einmal geprüft.
  - **Handbuch (F-36):** Der Satz zu `….ps5cc-konv-teil` sagt jetzt, dass Reste nach einem Absturz nur bei gleichem Ziel ersetzt werden.
- **Bekannt:** Ein Auftrag von ShadowMountPlus sperrt „Konvertieren starten“ in der Prüfliste nicht (die Prüfliste fragt ShadowMountPlus bewusst nicht bei jedem Tastendruck); der Start meldet dann, dass Aufträge nicht gleichzeitig laufen, und das Einreihen funktioniert.

---

## v1.81.0 – 13.11.2026

### Durchsicht seit v1.79.0 und Fehlersuche (13.11.2026)
- **Geprüft:** Durchsicht aller Änderungen seit v1.79.0 (acht Blickwinkel, sechs Funde), alle zehn Host-Tests, ein Fehlerlauf über alle Seiten und Reiter der Browser-App in drei Sprachen (keine Skriptfehler), das Protokoll der Pro (`app.log`) seit dem Release.
- **Abbild-Lesen (`library.c`):** Eine übrig gebliebene Marke `probe-read.txt` setzt einen Titel erst beim zweiten Mal auf die Liste `probe-skip.txt` (das erste Mal nur auf `probe-suspect.txt`); ein gutes Lesen streicht ihn wieder. Vorher genügte ein gewöhnliches Beenden der App mitten im Lesen (neue ELF gesendet, Konsole ausgeschaltet), um ein Spiel dauerhaft vom direkten Lesen auszunehmen. Die Marke wird nicht mehr mit `fsync` geschrieben.
- **Energie-Befehle (`api.c`, `http.c`):** „Aus“, „Neustart“, „Ruhemodus“ und „Abgesicherter Modus“ schreiben jetzt die Zeile `power_request` mit Adresse, Origin und Gerät des Absenders ins Protokoll. Anlass: Im Protokoll vom 12.11. steht `safemode_requested` fünf Sekunden nach dem App-Start um 04:50, ohne dass sich der Absender feststellen ließe; die App löst das nicht von selbst aus.
- **Abgelehnter Upload (`filemgr.c`):** Wird ein Upload mit 409 abgelehnt (anderer Auftrag oder Upload läuft), liest der Server den Rumpf zuerst leer (höchstens 15 s und 64 MiB), bevor die Antwort hinausgeht. Vorher riss die Verbindung bei gleichzeitigen Anfragen manchmal ab, und die Seite zeigte einen Netzwerkfehler statt des Grundes.
- **Verlauf zurücksetzen (`history.c`):** Die Antwort kommt, wenn die leere Datei geschrieben ist (höchstens 3 s), damit ein Neustart gleich danach den alten Verlauf nicht zurückbringt.
- **Karte der Hintergründe bleibt:** Die Marke `?vollbild=1` bleibt für den Besuch erhalten (`sessionStorage`), wenn man über Handbuch oder FAQ zurückkommt; ein frischer Start (Kachel, kein Verweis) löscht sie und die Karte bleibt versteckt. Am PC mit fünf Abläufen geprüft.
- **Ein gemeinsames Register „läuft ein anderer Auftrag?“ (`src/jobreg.c`):** Die Listen der Module (Dateimanager, Kopieren, Konvertieren, Verschieben, Löschen, Abbild-Aufträge, AMPR-Index, Paket installieren, Paket teilen, Spielstände, Auswerfen und die Abbild-Prüfung der Bibliothek) sind durch einen Aufruf `ps5tm_jobs_busy_except(eigene Kennung, …)` ersetzt; ein neuer Auftrag wird nur noch dort eingetragen. Die Meldungen nennen jetzt den laufenden Auftrag aus der Tabelle. Neu dabei, was einzelnen Listen fehlte (Disc-Abbild, Auswerfen, Spielstände …). Der Disc-Dump behält seine eigene Prüfung (er fragt unter seiner Sperre). Host-Test `test/jobreg` (199 Prüfungen) und CI.

### Systemsprache in der Browser-App (12.11.2026)
- **Ab Werk „Auto (System)“:** Ohne eigene Wahl übernimmt die Browser-App die Systemsprache und zeigt Englisch, wenn sie die Sprache nicht hat. Im Browser der PS5 gilt die Sprache der **Konsole** (die Seite erfährt sie von der App, merkt sie als `lang_sys` und lädt einmal neu, wenn sie von der gezeigten abweicht); am PC und Handy gilt die Sprache des Browsers, alle seine Sprachen in ihrer Reihenfolge (vorher nur die erste). Die Sprachwahl oben hat dafür den neuen Eintrag **Auto (System)**; eine eigene Wahl bleibt gemerkt, „Auto“ nimmt sie zurück.
- **Vollbild-App:** unverändert (folgt schon der Systemsprache der Konsole, sonst Englisch; `tv_language` leer = Systemsprache).
- Geprüft mit neun Fällen am PC (deutsch, französisch, portugiesisch und japanisch → Englisch, Liste „pt, it, en“ → Italienisch, gespeicherte Wahl, PS5-Kennung mit Konsole schwedisch / japanisch / englisch). Handbuch (`docs/HANDBUCH.md`) angepasst. **An der Konsole noch nicht erprobt.**


---

## v1.80.0 – 11.11.2026

### Audit 11.11.2026: die übrigen Befunde F-11, F-17 bis F-21, F-24, F-27
- **F-11:** Beim Packen im Spielordner (zstd/LZ4) wird vor dem Entfernen der Originale und vor dem Signieren noch einmal gefragt, ob das Spiel inzwischen läuft; wenn ja, bleiben die Originale liegen bzw. wird das Signieren übersprungen, mit Hinweis.
- **F-17:** Der Bau nutzt `-fstack-protector-strong` (ELF +82 KB auf 10,10 MB). Neu: `.github/workflows/host-tests.yml` führt die Host-Tests (account, cvsample, filemgr, fsign, instlock, smpcfg, sha256, convpfs, zstdpack, ps4m) bei jedem Push und Pull-Request auf `src/` und `test/` aus; auf GitHub noch nicht gelaufen.
- **F-18:** Das Paketverzeichnis eines LZ4-Pakets (`lz4pack.c`) wird zuerst nur im Kopf gelesen: Anzahlen innerhalb der Grenzen und Größe passt zum Kopf, erst dann wird bis zu 2000 MiB reserviert. Die zstd-Dateiliste (`zstdpack.c`) bricht beim Durchlaufen bei der Grenze ab statt erst nach dem Aufbau der ganzen Liste.
- **F-19:** Die Leerlaufprüfung der Abbild-Prüfung der Bibliothek kennt jetzt alle Aufträge (Spielstände, Paket teilen und installieren, AMPR, Abbild-Aufträge, Auswerfen, Disc), vorher fünf.
- **F-20:** Der Payload-Store ändert seinen Index nur noch unter einer Sperre (Löschen und fertiger Download verlieren sich nicht mehr gegenseitig), jede Schreibung hat eine eigene Temp-Datei, neu angelegt. Die AMPR-Bibliothek (`amprlib.c`) schreibt ihre Dateien einzeln nacheinander.
- **F-21:** Vor dem direkten Lesen eines Abbilds steht dessen Titel-ID in `probe-read.txt`; findet der nächste Start die Datei, ist die App dort beendet worden: Der Titel kommt in `probe-skip.txt` und wird nicht mehr direkt gelesen (Datei löschen erlaubt ihn wieder), mit Protokollzeile `image_probe_crash`.
- **F-24:** Der Payload-Katalog wird nach einem Fehlschlag nicht bei jedem Besuch der Seite neu geholt: erster neuer Versuch nach 10 Minuten, dann bis 3 Stunden; der Knopf fragt immer.
- **F-27:** Das Überspringen in der Dump-Warteschlange wird in einem Schritt mit der Prüfung des aktuellen Titels (unter der Sperre) gesetzt und beim Wechsel des Titels unter der Sperre gelöscht.
- Bau ohne Warnungen; alle Host-Tests grün. **Nicht an der Konsole erprobt**, vor allem der Stack-Schutz.

### Audit 11.11.2026: die offenen Befunde F-06, F-07, F-08, F-09, F-10, F-15
- **F-06, USB auswerfen:** `ps5tm_usbeject_busy()` meldet ein laufendes Auswerfen; Kopieren, Konvertieren, Verschieben, Dateimanager (samt Upload), Abbild-Aufträge, Paket-Installation, Spielstände und AMPR-Index starten in dem Fenster nicht (Meldung „Ein Laufwerk wird gerade ausgeworfen …“).
- **F-07, Verschieben:** Ein Start des Verschiebens oder Entpackens hält von der ersten Prüfung bis zur Antwort von ShadowMountPlus einen lokalen Merker (`g_starting`); andere Aufträge sehen ihn über `ps5tm_gamemove_busy()`, und ein zweiter Start läuft nicht in die Lücke. Das bewusste „SMP-Fehler gilt als frei“ bleibt.
- **F-08, Vollbild-Kachel und Verbindungen:** Die Loopback-Verbindungen (Kachel, Browser der Konsole, App selbst) haben eine eigene Grenze von 12 statt keiner. Die Kachel macht bei jedem Fehler der Statusabfrage eine wachsende Pause (bis 2 s) und stellt in der Zeit keine weiteren Fragen (Bibliothek, Aufträge, Verlauf …), damit sie eine langsame App nicht noch langsamer macht.
- **F-09:** Annahmewarteschlange des Webservers von 16 auf 128.
- **F-10, AMPR-Index beim Signieren:** Der Index wird nach je 20 signierten Dateien und am Ende korrigiert statt nur am Ende; ein Stromausfall mittendrin lässt nicht mehr eine lange Liste signierter Dateien mit alten Größen im Index zurück. fsign-Test (3071 + 18 Prüfungen) grün.
- **F-15, Upload-Reste:** Eine abgelehnte Anfrage verwirft den Rest ihres Körpers höchstens 15 s und höchstens 64 MiB lang; danach geht die Antwort hinaus und die Verbindung schließt (vorher unbegrenzt unter der Upload-Sperre).
- Bau ohne Warnungen; Tests: Dateimanager 56, Sperrdatei 6 + SIGKILL, fsign 3071 + 18, Vollbild-App 329. **Nicht an der Konsole erprobt.**

### README (11.11.2026)
- Neue Bilder aus der aktuellen Oberfläche mit Beispieldaten (Namen, Spiele, Adressen und Seriennummer ersetzt): Kühlung (Kopfbild), Spiele in der **kompakten Ansicht** (jetzt Standard), dazu neu **Spiele in der großen Ansicht**, System (mit dem Expertenmodus im Banner) und Profil. Danach auch Payloads, Dateimanager (mit laufender Kopie), Spielzeit, Protokoll, PS4 von USB, Spielstände, Pakete, Kühlung (Diagramme) und Handy mit erfundenen Daten (Spiele, Dateien, Payloads, Protokollzeilen); alle Bilder zeigen die Oberfläche ohne den früheren Schalter in der Kopfzeile. Die Beschriftung nennt die kompakte Ansicht als Standard; die Funktionsliste „Spiele“ nennt die beiden Ansichten (sieben Sprachen).

### Audit 11.11.2026: F-12 und F-14; Zwischenspeicher immer an; Spielkarten springen nicht mehr
- **F-12, eine Ausführung der App, ohne HTTP erkannt (`src/instlock.c`):** Jede Ausführung hält eine Schreibsperre auf `/data/PS5-Cooling-Center/app.lock`, bis sie endet (das System löst sie auch bei SIGKILL). Eine neu gesendete App findet damit ihre ältere, auch wenn die so überlastet ist, dass sie auf die Statusabfrage mit 503 oder gar nicht antwortet; früher galt sie dann als fremdes Programm, und die neue regelte den Lüfter neben ihr. Die ältere bekommt `SIGTERM` (der Lüfter geht zurück an die Konsole), nach drei Sekunden `SIGKILL`; erst dann starten Lüfter und Port der neuen. Die Suche über den Port bleibt für Ausführungen ohne Sperre (alle bis 1.79.x) und falls die Sperre nicht geht. Host-Test `test/instlock/run.sh` (6 Prüfungen plus SIGKILL-Fall).
- **F-14, Texteditor und Upload:** Der Texteditor schreibt mit `fsync` vor dem Umbenennen und prüft `close` (nach einem Stromausfall steht die alte oder die neue Datei da, nie eine leere). Eine Teil-Datei `….ps5cc-hochladen` von einem abgebrochenen Upload wird beim nächsten Upload gleichen Namens entfernt (nur eine reguläre Datei; es läuft immer nur ein Upload) statt ihn zu blockieren.
- **Zwischenspeicher „Covers & Metadaten“ immer an, ohne Anzeige (Wunsch des Users):** Weg sind die Zeile „53 Spiele gespeichert · 13 MB / Ordner leeren / Covers & Metadaten speichern“ der Seite Spiele (Browser) und der Abschnitt „Covers & Metadaten“ unter System → Speicher (Vollbild-App). Der Speicher arbeitet immer (`ps5tm_libcache_enabled()` gibt 1); `library_cache` bleibt im Konfigurationsfeld und in der API, ab Werk 1, und entscheidet nichts mehr. `GET /api/v1/library/cache` und `…/clear` bleiben für Skripte.
- **Expertenmodus nur noch auf der Seite System, ab Werk aus (Wunsch des Users):** Der Schalter oben in der Kopfzeile ist weg; er steht jetzt nur noch auf der Seite System, im Banner „Deine Konsole“ rechts neben „Läuft seit“ (die Zeile bei den Einstellungen ist entfallen). Wer ihn eingeschaltet hatte, bekommt ihn einmal ausgeschaltet (Merker `ps5tm.expertReset`), danach gilt wieder die eigene Wahl. Handbuch (sieben Sprachen) angepasst.
- **„Hintergründe der Vollbild-App“ nur noch, wenn die Browser-App aus der Vollbild-App geöffnet wird (Wunsch des Users):** `POST /api/v1/open-ui` kennt `{"from":"fullscreen"}`; die Vollbild-App sendet das, und die Seite trägt dann `?vollbild=1`. Nur mit dieser Adresse zeigt die Browser-App die Karte (System) und lädt ihre Daten; wer die Browser-App allein öffnet (Kachel, PC, Handy), sieht sie nicht. Nichts wird gemerkt. Die Texte der Vollbild-App sagen, wie man dorthin kommt (Options → Weboberfläche im Browser öffnen).
- **Vorgabe der Spielkarten ist „kompakt“ (Wunsch des Users):** Wer die Darstellung noch nie gewählt hat, sieht die kompakte Ansicht; wer „groß“ gewählt hat, behält es (der Browser merkt die Wahl je Gerät, `web/theme-boot.js`). In der Vollbild-App gibt es diese Wahl nicht.
- **Kompakte Spielkarten haben immer dieselbe Höhe:** Auf der PS5 waren die Karten erst etwa 18 px höher (158 statt 140) und schrumpften dann von selbst (Screenshots 16:50:11 und 16:50:12). Ursache: Das Titelbild war ein Rasterelement über zwei Zeilen, von denen eine flexibel (`1fr`) ist; so ein Element gibt seine ganze Höhe an die flexible Zeile weiter, und der Browser rechnete das mal so, mal so. Jetzt steht das Bild frei links oben (`position: absolute`, 78 px), die Zeile für die Marken ist `minmax(52px, 1fr)`: Höhe immer 140 px (am PC gemessen; auf der PS5 nach dem Senden zu prüfen).

### Kein Fokusrahmen des Browsers um Knöpfe (11.11.2026)
- **Anlass:** Mit dem Controller (Steuerkreuz, L1/R1 für den Bereichswechsel) zeichnete der Browser der PS5 einen hellbraunen Rahmen um den gewählten Knopf oder Reiter. L1/R1 setzen den Fokus auf den neuen Reiter, deshalb fiel er dort am meisten auf.
- **Änderung (`web/style.css`, nur Aussehen):** Knöpfe, `summary` und `role="button"` bekommen bei `:focus-visible` keinen Browser-Rahmen mehr; ihre eigene Markierung (Reiter und `.btn` etwas heller) bleibt, Knöpfe ohne eigene werden leicht aufgehellt. Auswahllisten, Text- und Mehrzeilenfelder bekommen statt des Rahmens einen helleren eigenen Rand (nach dem Screenshot vom 11.11. nachgezogen); Haken, Schalter, Optionsknöpfe, Schieberegler, Links und die Bildwahl behalten ihre Markierung.

### Durchsicht 11.11.2026: die vier wichtigsten Befunde (F-01 bis F-04)
- **F-01, Zugang bleibt offen:** Meldet `accept()` zu wenig Dateien, Puffer oder Speicher (`EMFILE`, `ENFILE`, `ENOBUFS`, `ENOMEM`), schließt die Weboberfläche ihren Zugang nicht mehr (das gab allen Wartenden einen Abbruch, und das Neuöffnen scheiterte oft am selben Mangel). Sie gibt stattdessen einen Reserve-Deskriptor frei, nimmt eine wartende Verbindung an und beendet sie sauber, wartet 250 ms und macht weiter; die Meldung `http_accept_resource` steht höchstens einmal je Minute im Protokoll. Alle anderen Fehler wie bisher (der Fehler 163 vom 03.11. schloss den Zugang und die Neueröffnung klappte). **Befund zum Vorfall vom 11.11.:** Im Protokoll steht an dem Tag kein `http_accept_failed`; dieser Fehler war also nicht die Ursache, die Ursache bleibt offen.
- **F-02, die Vollbild-Kachel ersetzt die App nicht mehr mitten im Auftrag:** Neue Route `GET /api/v1/jobs/active` (dieselbe Liste wie beim USB-Auswerfen, jetzt mit laufenden Uploads). Die Kachel fragt nur diese eine Route; kommt keine Antwort oder keine verständliche, gilt das als „beschäftigt“ und die alte App bleibt. Nur eine App ohne die Route (404) wird noch wie bisher Auftrag für Auftrag gefragt, auch dort zählt eine fehlende Antwort als beschäftigt. Beim Ablösen bekommt die alte App zuerst `SIGTERM` (Lüfter geht zurück an die Konsole), erst nach drei Sekunden `SIGKILL`.
- **F-03, die Spieleliste blockiert nicht mehr alle:** Wer nur die Seite anzeigt, bekommt auch nach einer Änderung (Kopieren, Löschen, Verschieben …) sofort die bisherige Liste; die nächste Anfrage liest neu. Wer einen Auftrag gegen die Liste plant und die allererste Lesung warten höchstens 20 Sekunden (`library_wait_timeout` im Protokoll), danach geht es mit der vorhandenen Liste weiter. Die Ordnergrößen einer Lesung haben zusammen höchstens 20 Sekunden; was fertig ist, bleibt im Zwischenspeicher, der Rest zeigt die zuletzt bekannte Größe.
- **F-04, Uploads sind für andere Aufträge sichtbar:** Ein laufender Upload zählt als Auftrag des Dateimanagers: `ps5tm_filemgr_busy()` meldet ihn allen anderen (Kopieren, Konvertieren, Verschieben, Löschen, USB auswerfen …), und der Dateimanager startet keinen Auftrag, solange ein Upload läuft. Umgekehrt startet ein Upload nicht, solange ein anderer Vorgang der App läuft (neu in der Liste: Disc-Abbild). Beanspruchen und Prüfen sind ein Schritt.
- **Geprüft:** Bau ohne Warnungen (`-Werror`); der Dateimanager-Test (56 Prüfungen) läuft durch. **Nicht an der Konsole erprobt.**

### Fremdbausteine: AMPR EMU NG 0.3.5 (11.11.2026)
- **AMPR EMU NG v0.3.4 → v0.3.5** (quer3q, Veröffentlichung vom 11.11.2026): Laufzeit-Korrekturen ohne Änderung der Paketformate. Laut Notizen: Spielstände (ein neues Spiel über einen vorhandenen Stand blieb auf dem Ladebildschirm hängen; die Stände selbst wurden nicht zerstört) und ein „schlankerer“ Lauf, bei dem Spiele flüssiger laufen (Leerlauf beim Lesen). Die Bauten tragen die Kennung `ampr-emu-ng 0.3.5`, der SELF-Kopf stimmt, die SHA-256 der beiden Dateien (751 504 und 869 168 Byte) stimmt mit der `SHA256SUMS` der Veröffentlichung und den Prüfsummen von GitHub überein.
- **In der App:** die eingebetteten Dateien `libSceAmpr.sprx-0.3.5-ng` und `-ng-debug` ersetzen die von 0.3.4 (wie bei den Fassungen davor; ELF-Größe siehe Bau), `src/amprng.c` (`NG_VERSION`, Größen, SHA-256 für den Abruf per Knopf), Namen in Handbuch (sieben Sprachen), READMEs, `docs/API.md`, `THIRD_PARTY_NOTICES.md`, `third_party/ampr_emu/ORIGIN.md`, Credits-Satz (Browser und Vollbild-App) und `tools/upstream.json`. Auf der Konsole eingebaute Spiele behalten ihre Datei, bis man im Fenster „AMPR & APR-EMU“ die neue Fassung wählt; die Fassung 0.3.4 im Ordner `AMPR_EMU` der Konsole wurde nicht angefasst.
- **Keine harte 10-MB-Grenze für die ELF (Test 11.11.2026):** An der Pro liefen aufgefüllte Kopien der ELF mit 12, 16 und 25 MB (gesendet über Port 9021, jeweils neue Prozess-Nummer, Port 8086 antwortete); auch elfldr hat im Quelltext keine Obergrenze, und die offiziellen Programme von ps5-payload-dev sind bis 62 MB groß. Der frühere Fehler bei 10,5 MB hatte eine andere, nie eingegrenzte Ursache. Die Regel „Nutzlast unter ~10 MB“ steht deshalb nur noch als „sparsam“ in `.claude/skills/upstream-update/SKILL.md`, `third_party/ampr_emu/ORIGIN.md` und `tools/upstream.json` (ältere CHANGELOG-Einträge bleiben als Geschichte stehen).

### Große Spielekarten auf der PS5: leichter zu zeichnen (11.11.2026)
- **Anlass:** Der Scroll-Test an der Pro (19,4 ms mit allem an, Ziel 16,7 ms) zeigte: Kartenecken (17,4), Schein (18,0), Knopfrundung (18,1) und das Beschneiden der Karte (18,6) bremsen, alle vier zusammen bringen 16,7 ms. Ein Vergleich mit den Spielkarten von ShadowMountPlus (Quelltext, nicht an der Konsole gemessen) zeigte, wo sie sparen: kein Schatten auf der Karte, die Karte beschneidet nicht, kleineres Titelbild, weniger gerundete Kästchen je Karte.
- **Was sich ändert (nur Browser der PS5, nur die große Ansicht; die kompakte Ansicht und andere Browser bleiben):** Die Karte ist 14 statt 18 px rund und beschneidet ihren Inhalt nicht mehr (auch `contain: paint` entfällt); kein Schein-Ring (das laufende Spiel behält sein grünes Leuchten); Knöpfe 7 px, Marken 6 px statt Pille. Das Titelbild trägt die runden oberen Ecken selbst: `app.js` (`gmRoundTop`) zeichnet es einmal je Bild in der angezeigten Größe (statt 512 px für etwa 283) mit durchsichtigen oberen Ecken und tauscht es aus (`data-mode="round"`, Klasse `pre` am Kästchen); nur quadratische Bilder, alle anderen und Bilder ohne Vorrundung beschneidet das Kästchen per CSS wie bisher. Beim Wechsel der Ansicht kommt das Original zurück.
- **Vorgerundet wird auf der Seite, nicht in der Nutzlast:** Die Bilder, die die App speichert, bleiben unverändert (die Vollbild-App benutzt dieselben und zeichnet ihre Rundung selbst; die ELF liegt bei 9,99 MB, ein PNG-Schreiber wäre zu viel).
- **Messung nach dem Einbau (Pro, große Ansicht, Seite „Spiele“, Karte 717 px, Seite 5696 px):** alle 13 Schritte 16,2–16,4 ms, p95 17 ms, 0 langsame Bilder (60 Bilder/s), auch „Alles an“. **Korrektur:** Die Seitenhöhe 2179 px und die Messungen mit 19,4 ms davor stammen aus der **kompakten** Ansicht, die bei dem Nutzer Standard ist; für die alte große Ansicht gibt es keine Vergleichsmessung. In der kompakten Ansicht (Karte 158 px) lag „Alles an“ bei 19,5 ms, mit vier unruhigen Schritten (36 bis 72 ms, viele langsame Bilder, auch im Kontrolllauf).
- **Kompakte Ansicht, gleiche Leichtigkeit (11.11. später):** Karte 12 px und ohne Beschneiden und Schein, Symbolknöpfe 7 px, Marken 6 px; das kleine Titelbild wird rundum vorgerundet (10 px, höchstens 256 px) statt von seinem Kästchen beschnitten.
- **Titelbilder nacheinander umwandeln (11.11. später):** Die Umwandlung (Canvas, PNG) läuft in einer Schlange, ein Bild je 50 ms und nur nach 400 ms ohne Scrollen, nicht mehr alle 25 auf einmal (Messung 5: erster Schritt 47 ms, „Verläufe aus“ 43 ms, danach ruhig). „Automatisch messen“ wartet vor dem Vorlauf, bis alle Bilder geladen und umgewandelt sind (Schlange leer, eine Sekunde Ruhe, höchstens 40 s). Außerdem verlangt der Test nur noch, dass die Seite höher als das 1,5-fache des Fensters ist (kompakt ist sie nur etwa dreimal so hoch), und schreibt bei einem Abbruch `scrollauto skip …` mit Zahlen ins Protokoll.
- **Dreieck-Taste entfernt:** Die Controller-Abfrage der kompakten Ansicht (`navigator.getGamepads()` alle 120 ms) ist ausgebaut: Der Browser der PS5 belegt die Dreieck-Taste selbst (Seite neu laden), und die Infoleiste erscheint auch per Maus, Steuerkreuz-Fokus und Taste „i“. Handbuch (sieben Sprachen) angepasst. Verdacht: Die Abfrage könnte die unruhigen Schritte der kompakten Messung verursacht haben (nicht belegt).
- **Prüfen:** Nach dem Einbau mit „Automatisch messen“ an der Pro messen; Protokollzeile `scrollauto info` nennt jetzt Ansicht (`view`), Fenster (`win`), Pixeldichte (`dpr`) und Kartenhöhe (`card`).

### Durchsicht der Änderungen seit v1.79.0 (11.11.2026)
Alle Host-Tests (account, convpfs, filemgr, fsign, ps4m, sha256, sim, smpcfg, zstdpack, neu cvsample) und die 329 Tests der Vollbild-App laufen durch, die Seite hat keine Skriptfehler auf allen acht Seiten, die HTML-Dateien sind ausgeglichen. Behoben:
- **Dateimanager, Text speichern:** Jeder Speichervorgang bekommt einen eigenen Temp-Namen (`.ps5cc-tmp-<pid>-<zähler>`). Zwei gleichzeitige Speichervorgänge derselben Datei (Seite und Vollbild-App, Doppelklick) löschten sich sonst gegenseitig die Temp-Datei, und die halb geschriebene Datei konnte über die echte kommen.
- **Dump abbrechen (`dump_job.c`):** Das Abbruch-Flag wird wie in den anderen Auftragsmodulen unter der Sperre gesetzt, die den Dump gesehen hat, und nur, wenn einer läuft. Ein Abbruch für den eben beendeten Dump konnte sonst den nächsten Titel der Dump-Warteschlange treffen.
- **Schnellprüfung exFAT, letzte Stücke:** Die Stichprobe lag in `cv_run`; ein Test (`test/cvsample`, 646 Prüfungen, Bilder von 1 Byte bis 2 TiB) fand einen echten Fehler: Hatte die regelmäßige Auswahl zufällig das vorletzte Stück gewählt, fehlte das drittletzte. Die Auswahl steht jetzt in `src/cvsample.h`, der Abschluss ersetzt alles ab dem ältesten der letzten drei Stücke.
- **Seite, kompakte Ansicht:** Die Titelbilder werden erst nach einer Scroll-Pause verkleinert (nicht mehr beim Scrollen auf der Hauptlinie).
- **Scroll-Test:** Knopf und Messung beim Handscrollen kommen auch nach einem Fehler wieder (`try/finally`); die Änderung der Zeilen gilt gegenüber „Alles an“ und fehlt, wenn diese Messung fehlt; die Protokollzeilen werden nacheinander gesendet und kommen in der Reihenfolge an.
- **`.ini` bleibt lose:** Die Regel steht an einer Stelle (`lzc_is_ini()` in `lz4cfg.c`) und gilt für LZ4, zstd und das automatische Profil; „.INI“ in Großbuchstaben zählt jetzt auch.
- **Test `account`:** Der Stub kannte die gepackte Fake-Anmeldung nicht mehr (`ps5tm_blob_inflate`), der Test ließ sich nicht linken; behoben.

### Scroll-Test: Knopf „Automatisch messen“ (11.11.2026)
- **Was:** Auf der Karte „Scroll-Test (PS5)“ (Expertenansicht, Seite System) scrollt der neue Knopf **Automatisch messen** die Seite „Spiele“ selbst durch (hin und her, 1800 Pixel je Sekunde, je Einstellung 7 Sekunden) und misst je Bild den Abstand zum vorigen (`requestAnimationFrame`). Reihenfolge: ein Vorlauf (nicht gewertet, die Titelbilder laden sich dabei), alles an, dann je ein Schalter aus (Rundungen, Schatten, Verläufe, Bilder, Übergänge, „Karten nicht einzeln zeichnen“), am Ende noch einmal alles an als Kontrolllauf. Dauer etwa eine Minute; „Abbrechen“ stoppt. Danach stehen die Schalter wie vorher, die Seite ist wieder „System“, und die Karte zeigt je Einstellung: mittleren Abstand in ms, Bilder pro Sekunde, Zahl der Bilder über 50 ms („langsam“) und die Änderung des mittleren Abstands gegenüber „Alles an“.
- **Protokoll:** eine Zeile `scrollauto info …` (Knoten, Karten, Seitenhöhe) und vier Zeilen `scrollauto v2 …` mit je bis zu vier Messungen im Format „Schlüssel Mittel p95 größter langsam“ (Schlüssel `none`, `round`, `shadow`, `grad`, `img`, `anim`, `contain`, `cardround`, `cardglow`, `btnround`, `coverclip`, `cards`, `none2`), weit unter der Grenze von 10 Zeilen je Minute der Route `/api/v1/log/client`. Die bisherige Messung beim Scrollen von Hand (`scroll …`) schweigt während des Durchlaufs.
- **Feinere Schalter für die Spielkarten (11.11. nachmittags):** Der erste Durchlauf an der Pro (Seite „Spiele“, 19,8 ms mit allem an) zeigte: „Rundungen aus“ (17,0 ms) und „Schatten aus“ (17,2 ms) bringen das Scrollen an die 60-Bilder-Grenze, Verläufe (18,4), Bilder (19,2), Zeichenbereich je Karte (20,0) und Übergänge (20,7) kaum oder nichts; der Kontrolllauf wich nur 0,2 ms ab. Diese beiden Schalter wirken aber auf die ganze Seite. Neu sind vier Schalter, die nur die Spielkarten betreffen: **Kartenrundung aus** (Ecken der Karten), **Kartenschein aus** (der feine Schein-Ring, `--card-glow`), **Knopfrundung aus** (Knöpfe und Marken auf den Karten) und **Beschneiden der Karte aus** (`overflow`, dazu `contain: paint`, das auf der PS5 ebenfalls an der runden Kante beschneidet). „Automatisch messen“ prüft sie einzeln und zusammen („Karten: alles aus“), 13 Schritte statt 8, etwa zwei Minuten. Danach ändert sich die Gestaltung nur dort, wo es etwas bringt. Zehn neue Texte in sechs Sprachen (ein Satz geändert: „etwa zwei Minuten“).
- **Grenzen:** Ein Skript scrollt nicht ganz wie der Controller; verglichen werden aber nur die Einstellungen untereinander, und das Kontrolllauf-Ergebnis zeigt, wie stark gleiche Durchläufe streuen. Nur Browser-Oberfläche (die Vollbild-App hat keine scrollenden Browserseiten).
- **Stand:** Am PC in der Vorschau geprüft (Ablauf, Tabelle, Schalter zurückgestellt, Protokollzeilen, Englisch und Russisch); die Zahlen dort sind wertlos, weil der Vorschau-Tab im Hintergrund auf 30 Bilder/s gedrosselt ist. Messung an der Konsole steht aus. Zwölf neue Texte in allen sechs Sprachen.

### SHA-256 mit den SHA-Befehlen der CPU (11.11.2026)
- **Was:** `src/sha256.c` rechnet die SHA-256 jetzt mit den SHA-Befehlen des Prozessors (SHA-NI, `sha256rnds2`/`sha256msg1`/`sha256msg2`), wenn die CPU sie hat; sonst bleibt das bisherige reine C. Das gilt für alles, was die App mit SHA-256 prüft: die Prüfsummen-Datei nach dem Konvertieren (`.sha256`), das Zurücklesen beim Aufteilen von Paketen, die Spielstand-Sicherungen, die Prüfung heruntergeladener Payloads, Plug-ins und APR-EMU-Dateien und das Signieren (`fsign`). Keine Einstellung, keine neue Oberfläche.
- **Sicherheit:** Der Pfad wird zur Laufzeit gewählt (CPUID, Blatt 7, Bit 29, dazu SSSE3/SSE4.1; die Funktion trägt `target("sha,sse4.1,ssse3")`, es braucht also keine Übersetzer-Schalter). Vor dem ersten Gebrauch läuft ein Selbsttest („abc“ gegen die bekannte Summe); besteht er nicht, nimmt die App das reine C. Das Ergebnis steht beim Start im Protokoll („sha256_path“). Mit `-DPS5TM_NO_SHANI` lässt sich der Pfad beim Übersetzen abschalten.
- **Host-Test** (`test/sha256/run.sh`, 18 Prüfungen, mit Sanitizern): bekannte Antworten (leer, „abc“, 56 und 112 Byte, eine Million „a“), 3000 Zufallsnachrichten der Länge 0 bis 1500 (um jede Block- und Auffüllgrenze) in einem Stück und in zufällige Stücke geschnitten, reines C gegen SHA-Befehle, und 8 MiB gegen das unabhängige `sha256sum`. Geschwindigkeit auf dem PC: **380 → 2625 MB/s (6,9-fach)**.
- **An der Pro gemessen** (Pro .94, Protokoll bestätigt „SHA-Befehle der CPU (Selbsttest bestanden)“; Quelle PPSA99130 „Vulkan Template“, 260,3 MB, je drei Läufe, gründliche Prüfung, Ziel interne SSD): Prüfen **ffpfsc 1,07 → 0,50 s** (Gesamtzeit 2,75 → 2,28 s), **exFAT 1,4–1,5 → 0,43 s** (Gesamtzeit 1,87 s), ffpkg 0,73 s. Auf usb0 (ffpkg, 335 MB) 3,49 → 2,40 s: Das ist jetzt die Lesegeschwindigkeit des Sticks, die SHA-256 ist nicht mehr der Engpass. Das Schreiben ändert sich nicht (usb0 mit etwa 30 MB/s bleibt der Engpass beim Schreiben).
- **Richtigkeit an der Konsole:** PPSA99210 (82 MB) als ffpfsc (30,5 MB) und als exFAT (88,6 MB) auf die interne SSD konvertiert, gründlich geprüft; die von der App geschriebene `.sha256` und die SHA-256 der vom PC aus per FTP geladenen Datei (Python `hashlib`) sind in beiden Fällen **gleich**. Ergebnisdateien und `.sha256` danach wieder gelöscht.

### Vorfilter für nicht komprimierbare Blöcke: ausprobiert und wieder entfernt (11.11.2026)
- **Was entfernt wurde:** Der am selben Tag eingebaute Vorfilter von ffpfsc (drei Stücke zu 4 KiB eines Blocks durch einen Kompressor der Stufe 1; schrumpft keines um 3 %, wird der Block roh gespeichert, ohne ihn zu versuchen) ist wieder ausgebaut: `conv_pfs.c`/`conv_pfs.h` (die Felder `no_prefilter` und `prefiltered`), `gameconvert.c` (Protokollzeile „game_convert_prefilter“), die sechs Übersetzungen dieser Zeile und der Absatz im Handbuch. Die Abbilder entstehen wieder so wie vor dem 11.11.: Ein Block wird gepackt, wenn er dabei mindestens 5 % spart, sonst roh.
- **Warum, A/B-Test an der Pro** (ffpfsc auf die interne SSD, Quelle ebenfalls auf der SSD, die ELF mit und ohne Vorfilter im Wechsel, Prüfung der Schreibphase): **Marsupilami 2** (17,5 GB, Ordner, gründliche Prüfung, Reihenfolge mit, ohne, ohne, mit): Schreiben 87,1 s und 86,0 s mit, 81,7 s und 81,0 s ohne, also **ohne Vorfilter 6 % schneller** (Streuung etwa 1 s); Ergebnis 5 650 448 384 gegenüber 5 647 433 728 Byte. **ASTRO's PLAYROOM** (30,6 GB, exFAT-Abbild, schnelle Prüfung, ein Paar): 160,5 s mit, 158,0 s ohne (1,6 %); 13 313 835 008 gegenüber 13 280 870 400 Byte. Der Vorfilter übersprang nur 4,7 % und 6,6 % der Blöcke: Beide Spiele sind überwiegend komprimierbar (17,5 → 5,65 GB und 30,6 → 13,3 GB).
- **Eine Aussage des ersten Eintrags war falsch:** „Das Abbild ist byte-gleich zu dem ohne Vorfilter“ stimmte nur für selbst gebaute Blöcke im Host-Test. Bei echten Daten hielt der Vorfilter Blöcke für nicht komprimierbar, die die Kompression noch um mindestens 5 % schrumpft; die Abbilder waren 0,05 % und 0,25 % größer (gültig, die Prüfung bestand). Zwei Läufe lassen sich nicht über die SHA-256 vergleichen, weil das Abbild Zeitstempel enthält (`conv_pfs.c`, Inodes); verglichen wurde über die Größe.
- **Nicht gemessen:** der Gewinn bei überwiegend nicht komprimierbaren Daten (auf dem PC 86 → 285 MB/s je Faden); keines der installierten Spiele hat solche Daten, und bei USB-Quellen (etwa 140 MB/s Lesen) bremst ohnehin das Lesen. Ein neuer Anlauf sollte adaptiv sein (nur solange viele der letzten Blöcke roh landen) und an einem Spiel aus solchen Daten gemessen werden.
- **Danach an der Pro (ELF ohne Vorfilter):** 3D Billard (PPSA02800, 2,28 GB, exFAT-Abbild auf der SSD) als ffpfsc auf die SSD konvertiert, gründlich geprüft: 810 MB, 17,3 s (Schreiben 12,1, Prüfen 5,2), ohne Fehler; das Protokoll hat keine Vorfilter-Zeile mehr. Ergebnisdatei und `.sha256` danach gelöscht.
- **Test:** `test/convpfs` hat jetzt 23 statt 27 Prüfungen; neu ist ein Block mit zufälligen Stücken bei 0, 28 und 56 KiB und Nullen dazwischen, der gepackt werden muss (das hätte der Vorfilter falsch eingeordnet).

### Konvertieren: Prüfstufe „Schnell“ und größere Lesestücke (11.11.2026)
- **Prüfstufe (Browser und Vollbild-App):** Im Konvertieren-Fenster gibt es unter „4. Prüfung danach“ die Wahl **Gründlich** (Vorgabe, wie bisher: das ganze Ergebnis zurücklesen, jeden Block prüfen, Prüfsummen-Datei) oder **Schnell**. API: `verify` `"full"`/`"quick"` in `POST /api/v1/library/convert`, der Status nennt es. Schnell liest das Ergebnis nicht noch einmal ganz und legt **keine** Prüfsummen-Datei an (die Meldung am Ende sagt es): **ffpfsc** prüft Köpfe, Inodes, die ganze Blocktabelle, die Bereiche, die leer sein müssen, und höchstens 2048 gleichmäßig verteilte Blöcke, jeden entpackt (ein komprimierter Block trägt seine Adler-32); **exFAT** liest rund 400 beim Schreiben mit einer CRC-32 versehene Stücke (die ersten und letzten drei und verteilte dazwischen) zurück; **ffpkg (PS4)** prüft Aufbau und jede Datei, aber ohne das zweite Durchlesen für die SHA-256. Ein beschädigter Block außerhalb der Stichprobe bleibt dabei unentdeckt (ein Test zeigt es), „Gründlich“ findet ihn immer.
- **Lesestücke 4 MiB statt 1 MiB:** Die Quelle (Ordner, Abbild oder Datei) und das Zurücklesen für die Prüfsumme (`checkfile.c`) werden in 4-MiB-Stücken gelesen; der Lesevorlauf zwischen zwei Laufwerken hält drei solche Stücke bereit (12 MB statt 4). Geschrieben wird weiter in 1-MiB-Stücken. **An der Konsole gemessen** (siehe unten): bei einem 260-MB-Spiel kein messbarer Unterschied; 8 MiB lohnen erst bei großen Spielen und ließen sich nur mit einem solchen messen.
- **Messung an der Pro (11.11.2026, Pro .94):** Quelle PPSA99130 „Vulkan Template“ (260,3 MB, 269 Dateien, interne SSD), je drei Läufe, Streuung etwa 0,1 s; Vorher = ELF vom 10.11. (1 MiB, immer volle Prüfung), Nachher = diese ELF. Ziel usb0 (Gesamtzeit, davon Prüfen): **ffpfsc** gründlich 5,85 s (1,09) → 6,0 s (1,1–1,3), **schnell 4,99 s (0,22)**; **exFAT** gründlich 9,21 s (1,29) → 8,89 s (1,14), **schnell 7,67 s (0,22)**; **ffpkg** gründlich 11,91 s (3,49) → 11,96 s (3,61), **schnell 8,54 s (0,22)**. Gründlich bleibt also gleich schnell wie vorher; schnell spart 15 % (ffpfsc), 17 % (exFAT) und 28 % (ffpkg) der Gesamtzeit dieses kleinen Spiels. **usb0 schreibt nur mit etwa 30 MB/s und ist hier der Engpass**, nicht die Kompression (der Vorfilter, der bei diesem Spiel 1126 von 3972 Blöcken übersprang, änderte die Zeit deshalb nicht; er ist wieder ausgebaut, siehe oben). Auf die interne SSD als Ziel (ffpfsc, nach der Änderung) sind es 2,75 s (Schreiben 1,68, Prüfen 1,07): Das Prüfen ist dort gut ein Drittel der Zeit und rechnet mit rund 125 MB/s, also begrenzt die SHA-256 (reines C), nicht das Laufwerk.
- **Test:** `test/convpfs` (neu, 23 Prüfungen mit Sanitizern): welche Blöcke roh und welche gepackt gespeichert werden, volle und schnelle Prüfung, beschädigter Block in der Stichprobe (beide finden ihn), außerhalb (nur die volle), roh gespeicherter Block, Bereich, der leer sein muss, Blocktabelle. Vollbild-Tests 329.

### LZ4 1.11.1 (10.11.2026)
- Die LZ4-Bibliothek der LZ4-Pakete (`src/third_party/lz4`) ist auf **v1.11.1** (Yann Collet, BSD-2-Clause) getauscht (Wunsch des Users). Die Veröffentlichung hat keine Anhänge; die vier Dateien kamen vom Tag und stimmen mit den Git-Blob-Prüfsummen überein, die GitHub nennt (`README.PS5TM`). Behoben in 1.11.1: `LZ4_resetStreamHC()` konnte auf einem Zustand mit Müll bei Stufe 10 und höher außerhalb schreiben (seit 1.11.0); die App ruft nur `LZ4_compress_HC_extStateHC` auf, das den Zustand selbst anlegt, war also nicht betroffen. Das Blockformat ist unverändert: `test/lz4cross` (884 Blöcke, 457 MB, schnell und HC 3 bis 12) liest die Blöcke von 1.11.1 und 1.11.0 in beide Richtungen ohne einen Fehler, beide Fassungen packen gleich viel (147.862.829 Bytes).
- Aufgeräumt an der Konsole (auf Wunsch des Users): die beiden Dateien `libSceAmpr.sprx-0.3.3-ng` und `-ng-debug` aus `/data/PS5-Cooling-Center/AMPR_EMU` gelöscht (durch die eingebettete NG 0.3.4 überholt).

### APR-EMU-Version: Liste in drei Gruppen (10.11.2026)
- **Browser-Fenster „APR-EMU-Version“ und Vollbild-App:** Die Liste hat drei Gruppen mit Titel: **„AMPR EMU NG @quer3q“** (alle NG-Fassungen, auch die später dazukommenden: Regel `ng`), **„AMPR EMU @Drakmor“** (die übrigen, etwa die Fassung ohne Paketunterstützung) und **„LZ4 Asset Pack Version“** (`0.4.2.1 test-pack`, `0.4.2.1 test-debug-pack`: Regel „liest LZ4, ist aber nicht NG“; die Fassung ohne Protokoll zuerst). Vollbild: dieselben drei Überschriften (die App schreibt Überschriften groß). Die PlayGo-Liste bleibt eine Liste ohne Gruppentitel. Nur Anzeige: Auswahl, Einbau und Sperren (zstd/LZ4-Spiel) wie bisher.
- **Marke ZSTD:** Die NG-Fassungen tragen neben „LZ4“ jetzt auch die Marke **„ZSTD“** (sie lesen beide Formate); in der Vollbild-App steht „ZSTD“ in der Beschreibung („AMPR EMU NG · LZ4 · ZSTD · …“).
- **Name der Fassung ohne Paketunterstützung:** `libSceAmpr.sprx-0.4.2.1-test-nopack` heißt in der Liste und beim Eingebauten nur noch **„0.4.2.1“** (statt „0.4.2.1 test-nopack“); die Zeile darunter sagt „ohne Paketunterstützung“. Geändert im Server (`label_of` in `src/amprlib.c`), so gilt es für beide Oberflächen.
- **Übersetzungen:** Titel in allen sechs Sprachen (Browser und Vollbild); die Vollbild-Überschrift „ZSTD-ASSET-PACKS“ hatte noch keine Übersetzung und bekam sie dabei.
- **Aufgeräumt an der Konsole (auf Wunsch des Users, einmalig):** die fünf älteren Dateien `libSceAmpr.sprx-0.3.2-ng`, `-0.2.0-ng`, `-0.2.0-ng-debug`, `-0.3.6.6-test` und `-0.3.6.6-test-debug` aus `/data/PS5-Cooling-Center/AMPR_EMU` gelöscht (die App zeigt, was im Ordner liegt).

### Fremdbausteine (10.11.2026)
- **AMPR EMU NG v0.3.4** (quer3q, 09.11.2026) löst die eingebettete 0.3.3 ab (beide Dateien, `libSceAmpr.sprx-0.3.4-ng` und `-ng-debug`; die alten fliegen heraus, Wunsch des Users bei jeder NG-Fassung). Die Dateien kamen vom User und stimmen mit der `SHA256SUMS` der Veröffentlichung überein (712.144 und 819.776 Bytes, SELF-Kopf und Kennung `ampr-emu-ng 0.3.4` geprüft). Neu in der Laufzeit: Sie passt ihre Fäden dem Spiel an (startet klein, wächst beim Lesen, gibt Fäden sofort zurück, wenn das Spiel sie braucht), die Paket-Zwischenspeicher sind doppelt so groß; behoben sind Schwarzbild und Hänger beim Start, wenn das Spiel keinen eigenen Faden mehr bekam, und das Laden von Spielständen (Crimson Desert). Die Paketformate (zstd, LZ4) sind unverändert, `zstdpack.c` und `lz4pack.c` brauchen dafür nichts. Nachgezogen: `NG_VERSION` und Tabelle in `src/amprng.c` (der Knopf „AMPR EMU NG von GitHub laden“ gilt jetzt für 0.3.4 als mitgelieferte Fassung), `ORIGIN.md`, `THIRD_PARTY_NOTICES.md`, Handbuch ×7, README ×7, Credits (Browser und Vollbild), `docs/API.md`.
- **PS5 Dump Forge v0.5.5** (quer3q, 09.11.2026): Übernommen ist die Regel, dass **jede `.ini` lose bleibt**, damit `dlc_emu.ini` und andere Einstellungen nach dem Packen von Hand änderbar bleiben. Bei **zstd** immer (`zsp_stays_loose`); bei **LZ4** beim Packen immer, auch mit eigenem Profil (`lz4pack_write.inc`), und im Profil „Automatisch“ (`auto_loose`). Das **Lesen** bestehender Pakete bleibt, wie es war: Ein früher gepackte `.ini` lässt sich weiter prüfen und entpacken (`lzc_protected` wurde bewusst nicht erweitert). Test um drei Fälle ergänzt (73 Prüfungen), Handbuch ×7 nachgezogen. Nicht übernommen, weil die Windows-Oberfläche oder ein anderes Verfahren betreffend: Namensschema der Ausgaben („Output names“, englische Titel), Lesefenster beim Prüfen über Netz, Löschen auf Windows-FAT/exFAT. Unverändert gegenüber v0.5.4 sind `fself.rs`, die Liste der Programmdateien und die Formatbeschreibung; `fsign.c` stimmt weiter byteweise mit `make_fself.py` aus NG 0.3.4 überein (Test: 3071 + 18 Prüfungen).
- **Nicht angefasst:** LZ4 v1.11.1 und ps5upload v12.4.6 meldet die Prüfung ebenfalls als neu; beides wurde nicht verlangt (LZ4 nur nach Absprache, die APR-EMU liest dessen Format).

### Durchsicht der letzten Änderungen (10.11.2026, Abend)
Drei Prüfungen (Sicherheit, Parallelität, stille Fehler) über den neuen C-Code, die Vollbild-App und die Weboberfläche; jeder Fund wurde im Code nachgelesen, bevor er behoben wurde.
- **ShadowMountPlus-config.ini (`smpcfg.c`):** Endete die Datei mit der Zeile `[emulators]` ohne Zeilenumbruch, wurde `update_enabled=0` an die Kopfzeile geklebt und die Datei damit beschädigt; jetzt steht die Zeile auf einer eigenen Zeile. Die Kontrolle „steht danach das Gewollte drin?“ lief erst nach dem Ersetzen und prüfte nur den Speicherpuffer; jetzt läuft sie vor dem Schreiben, und nach dem Ersetzen wird die Datei selbst zurückgelesen. Zwei gleichzeitige Aufrufe teilten sich den Namen der Zwischendatei und konnten die Datei leeren: jetzt nacheinander. Ein Lesefehler oder ein `stat`-Fehler anderer Art als „fehlt“ führt nicht mehr zu einer neuen Datei über der echten; Zwischendatei und Sicherung folgen keinen Verknüpfungen (`O_EXCL|O_NOFOLLOW`), eine halbe Sicherung bleibt nicht liegen, `fclose` wird geprüft. Host-Test um 4 Fälle erweitert (150 Prüfungen).
- **zstd-Pakete:** Beim Entfernen der Originale werden die Pfade des Index wie überall sonst geprüft (kein `..`, kein führender `/`); ein Schreibfehler am Kopf einer neuen Paketdatei lässt keine Teil-Datei im Spielordner zurück.
- **Verlauf zurücksetzen:** Die leere Datei schreibt jetzt der Schreib-Faden, nicht mehr die Anfrage unter der Sperre, auf die der Lüfterfaden wartet; dazu kein gemeinsamer Zugriff auf `history.csv.tmp` mehr.
- **Abbrechen:** Das Abbruch-Flag wird unter der Sperre gesetzt, die den Auftrag gesehen hat (Kopieren, Abbild-Aufträge, Dateimanager, Spielstände; Konvertieren, Pakete und Teilen taten es schon): Ein Abbruch, der gerade beim Ende eines Auftrags ankommt, trifft nicht mehr den nächsten.
- **PS4-Ordner konvertieren:** Zwei gleichzeitige Starts desselben Titels (Doppelklick, Browser und Vollbild-App) schrieben beide das Overlay, und der abgewiesene löschte es unter dem laufenden; die Starts laufen jetzt nacheinander.
- **Dateimanager, Textdatei speichern:** Die Zwischendatei wird neu angelegt und folgt keiner Verknüpfung.
- **AMPR / APR-EMU:** Lässt sich die vorhandene Datei des Spiels nicht lesen, wird sie nicht mehr ohne Sicherung ersetzt (vorher ging sie dann ohne `.orig` verloren).
- **Weboberfläche:** Monitoring-Grenzwerte ohne gespeicherten Wert fielen auf das Minimum (55/56/15) statt auf den Standard (74/79/28). Der Lösch-Dialog wartete nach einem Neustart der App für immer auf „Wird gelöscht … 0 %“; nach drei Antworten „kein Auftrag“ zeigt er jetzt, dass die Rückmeldung verloren ging.
- **Vollbild-App:** Ein einzelnes Zeitlimit bei der Abfrage der Aufträge oder der Spiel-Extras zeigte „Kein Auftrag“ bzw. „nicht eingebaut“ und stieß ein überflüssiges Neuladen der Bibliothek an; jetzt bleibt der letzte Stand stehen, bis eine echte Antwort kommt.
- **Übersetzungen:** Die Karten „Offline-Konto aktivieren“ und „Fake-Anmeldung schreiben“ samt Meldungen standen in allen sechs Sprachen auf Deutsch; dazu die Beschriftung der Dateimenü-Knöpfe, „{0} (Konsole {1})“ und der Hinweis „Die eboot.bin nutzt SDK …“ (31 Einträge je Sprache).
- **Geprüft, nicht geändert:** `.fritz.box` im Host-Schutz (im Handbuch als gewollt beschrieben); das Entpacken im Spielordner folgt Verknüpfungen (nur mit vorbereitetem Ordner); die Spieleliste fragt ShadowMountPlus unter der Bibliothekssperre (bis 5,5 s Wartezeit für andere Anfragen); der Lüfterwächter setzt den Zustand nach dem Schreiben (Fenster von Mikrosekunden, heilt nach höchstens 5 s).

### Scrollen auf der PS5: Messung und Scroll-Test (10.11.2026)
- **Beobachtung des Users:** Auf den Seiten Spiele und Profil wird die Scrollbewegung langsamer. Eine Zählung der Zeichenarten je Seite zeigt, was nur diese beiden gemeinsam haben: Kästchen mit abgerundeten, abschneidenden Ecken, in denen ein Bild liegt (Spiele 25, Profil 31; alle anderen Seiten 0 oder 1). Schatten und Verläufe unterscheiden die Seiten nicht (Payloads hat über 100 und fällt nicht auf). Ob das die Ursache ist, lässt sich nur an der Konsole prüfen.
- **Messung (nur PS5):** Beim Scrollen zählt die Seite die Bildabstände und schreibt 400 ms nach dem letzten Schritt eine Zeile ins Protokoll („scroll games frames 52 avg 41 max 190 slow 12 nodes 5234 off none“), höchstens eine je 30 Sekunden.
- **Scroll-Test (Expertenmodus, Seite System):** Schalter für „Rundungen“, „Schatten“, „Verläufe“, „Bilder“, „Übergänge“ und den Zeichenbereich je Karte; jeder schaltet eine Zeichenart ab, die Messzeile nennt die abgeschalteten. So zeigt ein Durchlauf auf der Konsole, was das Scrollen bremst. Nur zum Suchen der Ursache.
- **Puffer und Zeichenbereich (nur PS5):** Die Titelbilder werden im Leerlauf nacheinander geladen und entpackt (nie beim Scrollen); jede Spielkarte ist ein eigener Zeichenbereich (`contain`; `content-visibility` bewusst nicht, die Scrollleiste spränge).

### Kompakte Ansicht: Titelbilder nicht mehr körnig (10.11.2026)
- **Sensoren (Kühlung):** Die Zeile unter dem Namen („Hauptprozessor“, „heißeste Messstelle am Chip“ …) beginnt jetzt unter dem Namen, nicht unter dem farbigen Punkt davor; sie saß 17 Pixel weiter links als der Name (Screenshot der PS5).
- **Symbol von „Infos & Metadaten“ mittig:** Der Knopf erbte aus der großen Ansicht `justify-content: space-between`; der Browser der PS5 setzte die eine Spalte des Rasters dadurch an den linken Rand, das Symbol saß etwa 4 Pixel links von der Mitte (Screenshot der PS5). Die Symbolknöpfe der kompakten Ansicht sind jetzt ausdrücklich mittig ausgerichtet.
- **Ursache:** Die Titelbilder der Konsole sind 512 × 512 Pixel, in der kompakten Karte stehen sie in 78 Pixel (156 Bildpunkte am Fernseher). Der Browser der PS5 verkleinert so stark nur grob, das Bild wurde körnig und verschwommen (Screenshot der PS5 vom 10.11.2026). In der großen Ansicht (240 Pixel) fällt das nicht auf.
- **Behebung nur für die kompakte Ansicht:** Die Seite verkleinert die Bilder selbst, in Halbierungen (jede Stufe mittelt je vier Bildpunkte), und zeigt sie in genau der Größe, in der sie stehen (Zwischenspeicher je Bild, kein neues Bild bei jedem Neuzeichnen). Das Original bleibt in `data-full`; die große Ansicht und alle anderen Stellen zeigen weiter das Original. Neue Abfragen an die Konsole gibt es nicht.

### Browser der PS5: größere Schrift zurückgenommen (10.11.2026)
- Die größere Schrift nur für den Browser der PS5 (Block `PS5-TEXT`, `tools/gen_ps5_text.py`, Umbruch-Ausnahme für den Payload-Pfad) ist auf Wunsch des Users wieder entfernt; die Schrift ist dort wie in jedem Browser. Die Klasse `ps5` bleibt für die übrigen PS5-eigenen Dinge.

### Firmware, die ein Spiel verlangt, auf den Karten (10.11.2026)
- **Browser-Oberfläche:** Unter dem Titel steht eine Marke „FW 13.60“ (PS4-Titel: „PS4-FW 9.00“) mit der Firmware aus der `param.json` bzw. `param.sfo`; verlangt das Spiel eine neuere Firmware als die Konsole hat, ist die Marke gelb. **Mit angewendetem Backport** steht stattdessen die Firmware, die der Backport trägt: die SDK-Fassung seiner `eboot.bin` (Marke neben „Backport“, im Hinweis steht, was die `param.json` verlangt). In „Infos & Metadaten“ kommt bei Backports die Zeile „Backport-Firmware“ dazu. Gilt in der großen wie in der kompakten Darstellung und in allen Designs.
- **Vollbild-App:** In der Zeile eines Spiels steht „· FW 13.60“ (Backport: dessen Firmware), in den Angaben „Benötigte Firmware“ und bei Backports „Backport-Firmware“.
- Nur Anzeige: Die Daten kamen schon aus der Konsolen-App (`system_ver`, `mods.eboot_sdk`); es gibt keine neue Abfrage. Texte in sechs Sprachen, Handbuch in sieben.

### Spielkarten: Darstellung wählbar (10.11.2026)
- **Neue Auswahl „Darstellung“ auf der Spiele-Seite** (neben der Sortierung): **groß (wie bisher)** oder **kompakt**. Die Vorgabe bleibt „groß“; nichts ändert sich, solange man nicht umschaltet. Der Browser merkt sich die Wahl je Gerät (wie das Design) und setzt sie vor dem ersten Zeichnen (`theme-boot.js`, `data-gmview` an `<html>`).
- **Kompakt:** Das Bild steht klein neben dem Titel, darunter „Starten“ und die übrigen Knöpfe als Reihe von Symbolen (Kopieren, Verschieben, Konvertieren, Asset-Packs bzw. AMPR & APR-EMU, Pfad öffnen, Ein-/Aushängen, Löschen); „Infos & Metadaten“ ist ein kleines Symbol in der Kartenecke. Eine Karte ist etwa 150 statt 430 Pixel hoch. Fährt man mit der Maus oder dem Steuerkreuz auf ein Symbol (oder drückt „i“ bzw. Dreieck), erklärt eine Leiste unten, was der Knopf macht, bei gesperrten Knöpfen auch warum.
- **Nur die Darstellung ändert sich:** Die Knöpfe sind in beiden Ansichten dieselben Elemente mit denselben Attributen, Sperren und Hinweisen, ihre Beschriftung steht weiter im Dokument (für Vorleser), nur das Symbol ist neu und in der großen Ansicht ausgeblendet. Geprüft: Klassen, `data`-Attribute, Sperren, Tooltips und Text aller 25 Karten sind in der neuen Fassung gleich der laufenden App an der Konsole (Prüfsumme je Karte). Neue Texte (Auswahl, Erklärungen der Knöpfe) in sechs Sprachen; Handbuch in sieben.
- Nicht geprüft: ob der Browser der Konsole die Dreieck-Taste an die Seite gibt; das Steuerkreuz (Fokus) und die Maus zeigen die Erklärung auch ohne sie.

### Helles Design: Schrift auf Bildern lesbar (10.11.2026)
- **Browser-Oberfläche, Design „Hell“:** Die Banner von **Payloads**, **Dateimanager**, **System**, **Protokoll** und **Profil** sind jetzt wie der Kopf der Spiele-Seite: helle Karte mit dunkler Schrift, das Bild nur rechts und nach links ausgeblendet, nicht mehr über die ganze Fläche. Vorher hatten „Payload Verwaltung“ und „Dateien & Ordner“ dunkle Schrift auf dem dunklen Bild und waren kaum zu lesen. Bei Handybreite bleibt vom Bild nur ein blasser Hauch; der Anfangsbuchstabe im Profilbild-Ersatz ist blau statt hellblau. Ebenso die drei Kacheln der Kühlung (Status, Lüfter, „Läuft gerade“): helle Karte, Bild nur rechts, der leuchtende Luftstrom und der Rand in der Farbe der Drehzahl bleiben. Der Satz im Status endet vor dem Bild; beim Lüfter steht die Zahl links auf dem hellen Grund und der Tacho rechts vor dem Bild.
- **Kopf der Spiele-Seite (Hell):** Das dunkle Bild rechts wird nach links jetzt in Hellgrau statt in Marineblau übergeblendet und reicht weiter; so bleibt auch die Zeile „… Spiele · … mit Backport, AMPR oder PlayGo“ lesbar, wenn sie bei einem schmalen Fenster bis ins Bild läuft (Handy: Bild zusätzlich blasser).
- **Alle Seiten durchgesehen** (Kühlung, Spiele mit allen Reitern, Dateimanager, Payloads, System, Profil, Protokoll, Credits): Ein Skript hat auf jedem Hintergrundbild die Textfarbe geprüft; mehr dunkle Schrift auf einem Bild gab es im hellen Design nicht. Angesehen wurden Payloads, Dateimanager und der Kopf der Spiele-Seite (Desktop, Tablet-, Handybreite) im PC-Browser.

### LZ4 und zstd nur bei Dump-Ordnern (10.11.2026)
- **Der Knopf auf der Spielkarte nennt, was er bei dem Spiel kann:** Bei einem Dump-Ordner heißt er weiter „LZ4-/zstd-Asset-Packs …“, bei einem Abbild (ffpkg, ffpfs, exFAT, ffpfsc) „AMPR & APR-EMU …“ und bleibt offen, denn AMPR-Index, APR-EMU-Version und PlayGo gehen bei Abbildern (ShadowMountPlus hängt es kurz ein). LZ4 und zstd gehen dort nicht; die beiden Packen-Knöpfe im Fenster sind aus und sagen warum. Das Fenster heißt bei einem Abbild ebenfalls „AMPR & APR-EMU“. Ebenso der Eintrag im Menü der Vollbild-App („Extras: AMPR, PlayGo …“ statt „… LZ4, zstd …“); neues Bild „spiel-menue-abbild“ in der Tour. Handbuch in sieben Sprachen ergänzt.

### zstd neben LZ4 benannt (10.11.2026)
- **Browser-Oberfläche:** Im Fenster „LZ4-/zstd-Asset-Packs & APR-EMU“ sind LZ4 und zstd jetzt zwei getrennte Zeilen mit eigener Überschrift, eigenem Text und eigenem Knopf (vorher eine Zeile „LZ4-Asset-Packs“ mit beiden Knöpfen, die nach Zusammenhang aussah). Der Fortschritt und das Ergebnis eines Auftrags erscheinen in der Zeile seines Formats; „zstd-Asset-Packs“ ist jetzt auch übersetzt. Der Knopf auf der Spielkarte hieß schon seit v1.79.0 „LZ4-/zstd-Asset-Packs …“.
- **Vollbild-App:** Der Eintrag im Menü eines Spiels heißt jetzt „Extras: AMPR, PlayGo, LZ4, zstd …“ (vorher nur LZ4); die Übersetzungen in den sechs Sprachen folgen. Im Fenster Extras hat zstd eine eigene Überschrift „ZSTD-ASSET-PACKS“ mit eigenem Knopf (vorher standen beide Knöpfe unter „LZ4-ASSET-PACKS“); neues Bild „extras-lz4-zstd“ in der Tour.
- **Handbuch (7 Sprachen):** Der Knopf auf der Spielkarte und das Fenster heißen dort „LZ4-/zstd-Asset-Packs …“ wie in der Oberfläche; vorher stand noch der alte Name nur mit LZ4.

## v1.79.0 – 10.11.2026

### ShadowMountPlus: die App fragt, welche Version läuft (09.11.2026)
- ShadowMountPlus hat ab 1.7beta12 eine neue `config.ini` (Abschnitte statt einer flachen Liste). Neue Einstellung **`smp_version`** (Standard **`auto`** = an der `config.ini` erkennen, `old` = vor 1.7beta12, `v2` = ab 1.7beta12; `""` = noch nicht gefragt, nur wenn ausdrücklich leer gespeichert; Teil von Export und Import): im Browser auf der Seite **System** ganz oben in der Karte **ShadowMountPlus** (Auswahl „Automatisch erkennen“, „Vor 1.7beta12“, „Ab 1.7beta12“), in der Vollbild-App unter **System → ShadowMountPlus** (eigener Reiter).
- **Fakelib automatisch aktualisieren wieder einschalten:** Die Frage „ShadowMountPlus umstellen?“ schaltet die automatische Fakelib-Aktualisierung von ShadowMountPlus ab (für AMPR EMU NG). Jetzt gibt es dafür einen Schalter, der sie auch wieder einschaltet (der Standard von ShadowMountPlus): im Browser in der Karte ShadowMountPlus auf der Seite System, in der Vollbild-App im Reiter ShadowMountPlus unter System. Er ändert nur die eine Zeile (`update_emulators` bzw. `update_enabled`) in `/data/shadowmount/config.ini`; `GET /api/v1/config` meldet den Stand als `smp_update_emulators` (nur lesend).
- **zstd im Titel:** Das Fenster eines Spiels und der Knopf, der es öffnet, heißen im Browser jetzt „LZ4-/zstd-Asset-Packs & APR-EMU“ (dort stellt man auch zstd ein); die Zeile unter dem Titel der Extras in der Vollbild-App nennt zstd ebenfalls.
- **Einmalige Rückfrage (nur bei leerer Einstellung; der Standard ist `auto`, dann entfällt sie):** Steht die Einstellung ausdrücklich auf leer, fragt die App beim Einbauen von NG zuerst danach, vor der Frage „ShadowMountPlus umstellen?“ (Browser und Vollbild-App). „Abbrechen“ bzw. „Zurück“ lässt alles, wie es ist, und baut NG nicht ein; die Antwort steht danach in den Einstellungen und lässt sich dort ändern.
- **Wer bestimmt, welche Zeile geschrieben wird:** Verrät die `config.ini` ihr Format (`[config]`/`[emulators]` = neu, flache Schlüssel = alt), gilt die Datei, denn das ist die Zeile, die ShadowMountPlus dann liest. Die Angabe entscheidet nur für eine fehlende oder leere Datei (bei „ab 1.7beta12“ mit `[config]`, `version=2` und `[emulators]` angelegt, sonst `update_emulators=0`) und löst einen Hinweis aus, wenn Datei und Angabe nicht zusammenpassen, etwa „ab 1.7beta12“ gewählt, die Datei aber noch die alte, weil ShadowMountPlus nach dem Update noch nicht gestartet wurde (es wandelt sie beim ersten Start um und übernimmt die Zeile aus `update_emulators`, so steht es in seinem Quelltext).
- **Alle Texte, die die Zeile nennen** (Hinweise zu AMPR EMU NG und PlayGo, Warnungen beim Packen, Meldung auf der Konsole, die Frage), nennen jetzt die Zeile, die die App wirklich ändert (`update_emulators` oder `update_enabled`); in den sechs Wörterbüchern von Browser und Vollbild-App sind sie dafür Muster mit einem Platzhalter. Die Antworten von `GET /api/v1/ampr-emu/smp-update` und das Feld `emus` von `GET /api/v1/library/emu` tragen dazu `key`, `format`, `smp_version` und `warning`, der AMPR-Auftrag `warn_key`.
- Geprüft: `test/smpcfg` (133 Prüfungen), ELF ohne Warnungen gebaut, Vollbild-App am PC: Bilder der neuen Zeile und beider Fragen angesehen, 328 Tests grün. Mit dem echten Leser und Umwandler aus dem Quelltext von ShadowMountPlus 1.7beta12 am PC: 11 Fälle (echte `config.ini.example`, Umwandlung einer flachen Datei, Hinweise bei Widerspruch, fehlende/leere Datei, die Datei von ShadowMountPlus selbst, CRLF) lesen alle den geschriebenen Wert richtig. An der PS5 Pro (Firmware 12.00, `config.ini` im neuen Format): Einstellung, Hinweis und Karte auf der Seite System; der ganze Ablauf im Browser (Rückfrage → Speichern → „ShadowMountPlus umstellen?“ mit `update_enabled` → NG-Einbau mit der Meldung „… schon abgeschaltet (update_enabled=0)“) an einer entpackten Testkopie von Double Dragon Revive (10,6 GB, wieder gelöscht); dabei war die automatische Aktualisierung dort schon aus, die Anzeige dieses Flags wurde im Browser vorgetäuscht, damit die Fragen erschienen; die echte `config.ini` blieb byte-gleich. **Nicht geprüft:** die Dialoge der Vollbild-App auf dem Fernseher und das Umschalten auf „an“ in der echten Datei.

### Firmware-Angaben (09.11.2026)
- Der Startcode der ELF (SDK v0.45) kennt die Firmware **4.03 bis 14.10** (`tools/check_sdk_firmware.py elf`: 04.00, 04.03, 04.50, 05.00, 05.10, 06.00, 07.00, 08.00, 09.00, 09.60, 10.00 … 14.10). Nutzer melden, dass die App auch auf **4.xx bis 10.xx** läuft. README (7 Sprachen, mit Kennzeichen und Zeile „FW 4.03 bis 14.10 Support“), FAQ (7 Sprachen) und Handbuch sagen das jetzt; getestet bleibt die Firmware 12.00.

### Fremdbausteine
- **ShadowMountPlus 1.7beta12** (drakmor, Vorabfassung vom 08.11.2026) geprüft. **Schnittstelle (Port 10101) unverändert:** gleiche Routen und JSON-Felder, die OpenAPI-Beschreibung ist nicht angefasst; neu ist nur, dass ein Ziel unter `/mnt/ext1` (M.2) nur angenommen wird, wenn dort wirklich etwas eingehängt ist, und dass M.2 nicht mehr als USB gilt. `smp.c`, Auswerfen, Aus-/Einhängen, Verschieben und die Bibliothek brauchen nichts.
- **Neues Dateiformat „v2“:** `config.ini` (globale Einstellungen, Suchpfade, SMB-Verbindungen als `[smb:NAME]`) und die neue `profiles.ini` (Einstellungen je Spiel und Abbild) beginnen mit `[config]` und `version = 2` und sind in Abschnitte gegliedert; beim ersten Start werden die alten Dateien (`config.ini`, `autotune.ini`, `smb.json`) umgewandelt und nach `config-v1-backup/` gesichert. Die App benutzt davon nur `update_emulators` (jetzt `update_enabled` unter `[emulators]`) und den Emulator-Ordner (`path`); Suchpfade, Profile und SMB verwaltet sie nicht.
- **`src/smpcfg.c` liest und schreibt die Datei jetzt wie der strenge v2-Leser von ShadowMountPlus:** (1) ein Kommentar hinter dem Wert (`update_enabled = off  # Test`) zählt nicht mehr als Teil des Werts – vorher las die App das als „an“; (2) Werte in Anführungszeichen (`path = "/data/mein ordner"`, mit `\\` und `\"`) werden verstanden – vorher wurde so ein Pfad ignoriert; (3) ein Abschnittskopf darf einen Kommentar tragen (`[emulators] # …`); (4) in einer v2-Datei zählen nur die Schlüssel der Abschnitte, die alten flachen Namen (`update_emulators`, `emulators_path`) werden dort wie bei ShadowMountPlus überhört, in einer flachen Datei weiter nur diese. Beim Ändern bleiben jetzt auch die Leerzeichen um den Wert und ein Kommentar dahinter (`update_enabled = on  # Standard` → `update_enabled = 0  # Standard`); der Rest der Datei, Zeilenenden und Dateirechte wie bisher.
- Geprüft: `test/smpcfg` (96 statt 66 Prüfungen, AddressSanitizer/UBSan) und ein Gegentest mit dem **unveränderten INI-Leser aus ShadowMountPlus 1.7beta12** (15 Dateien, 135 Prüfungen): gleicher Wert für `update_enabled` und `path`, und jede von der App geschriebene Datei nimmt dieser Leser an.
- Dokumentation: Handbuch (sieben Sprachen), `docs/HANDBUCH.md` und `docs/API.md` nennen jetzt beide Schlüsselnamen und die Umstellung. Die Fragetexte der Oberflächen nennen die richtige Zeile (siehe den Abschnitt „ShadowMountPlus: die App fragt, welche Version läuft“).
- Nur gelesen: **ps5upload bis v12.3.0** (phantomptr, 09.11.2026; „nur Wissen“, Stand in `tools/upstream.json` nachgezogen): Spielen übers Internet, Konvertieren direkt auf die PS5, PS4-Updates und -Zusatzinhalte, zstd-Pakete (so wie diese App sie seit 1.78.0 selbst kann, mit AMPR EMU NG), ein AMPR-Emulator für Spiele, die ohne keinen starten, Sicherung und Wiederherstellung, Fernspiel, Tailscale – nichts davon betrifft den Start oder die Sicherheit dieser App.

## v1.78.0 – 08.11.2026

### Unsignierte Programmdateien: Konvertieren und Packen schreiben sie als FSELF (nach ps5-dump-forge 0.5.4)
- Sind die Programmdateien eines Spiels einfache ELF-Dateien, startet die Konsole es nicht – ihr Lader lehnt sie ab. Beim **Konvertieren** eines PS5-Dump-Ordners (exFAT, ffpkg, ffpfsc) und beim **Packen in einen neuen Ordner** (nur LZ4, „Als neuen Ordner … daneben“) schreibt die App sie jetzt als **fake-signierte SELF-Dateien (FSELF)**; der Spielordner wird nur gelesen. Ein `ampr_emu.index` wird dabei auf die neuen Größen gebracht. Beim Packen im Spielordner selbst bleiben sie, wie sie sind (das Fenster sagt es); nur bei zstd, das immer im Spielordner packt, gibt es den **Schalter „Programmdateien als FSELF signieren“** (voreingestellt an): dann ersetzt die App sie dort durch FSELF-Fassungen (siehe unten).
- Der Aufbau ist byteweise der von `make_fself.py --ptype fake` (ampr-emu-ng) und von ps5-dump-forge 0.5.4 (`fself.rs`); gelesen wurde dessen Quelltext, nachgebaut in C (`src/fsign.c`). Es ist **nicht** der Aufbau der „Als FSELF neu signieren“-Funktion des Dumps (`src/dump_fself.c`).
- **Anzeige vorab:** Der Konvertierungsplan und der Packplan melden `unsigned_exec` (Anzahl, Namen, nicht signierbare); Browser und Vollbild-App warnen im Fenster („Unsignierte Programmdateien“, in der Vollbild-App die Zeile „Programmdateien“ im Packformular). Die Notiz des Auftrags nennt am Ende, wie viele signiert wurden.
- Prüfung: `test/fsign` (Host-Tests unter AddressSanitizer und UBSan): Ergebnis **Byte für Byte gleich** dem von `make_fself.py` und gleich dem Prüfwert (BLAKE3) aus dem Test von ps5-dump-forge, Erkennung, Index, 3000 beschädigte ELF-Dateien ohne Absturz, Abbruch ohne halbe Datei, und Abbilder (exFAT und UFS2) aus Spielordner plus signierten Dateien enthalten die SELF-Datei, nicht die ELF. **An der Konsole bestätigt (08.11.2026, siehe unten „An der Konsole geprüft“).**
- Nicht übernommen aus 0.5.4: `include_from`/`exclude_from` der Packregeln (die App kennt keine Regelprofile), das schnellere Entpacken fester 7z/RAR und die Laufwerke A: und B: (nur das PC-Programm).
- Neue Texte in allen sechs Sprachen (Browser und Vollbild-App), Handbuch und FAQ (sieben Sprachen), `docs/HANDBUCH.md`, `docs/API.md`, `docs/ENTWICKLUNG.md`, `THIRD_PARTY_NOTICES.md`; ELF 9,89 MB.

### An der Konsole geprüft (08.11.2026, Pro 192.168.1.94)
- **Fake-Signieren:** Ein PS5-Ordner mit einer einfachen ELF als `eboot.bin` (aus der laufenden `eboot.bin` von Rick Dangerous zurückgebaut, SHA-256 gleich der der ursprünglichen ELF) lässt sich **nicht starten**: `0x80020008` (Exec format error). Dieselbe Kopie, mit der App zu `.exfat` konvertiert (Notiz „1 Programmdateien waren nicht signiert … stehen im Abbild als FSELF“), **startet und lief 70 Sekunden im Vordergrund**. Der Aufbau der Datei stimmt Byte für Byte mit der laufenden `eboot.bin` überein, außer der Lage der angehängten Versionsbytes (dort unausgerichtet, bei uns ausgerichtet wie bei `make_fself.py`).
- Das **Konvertieren** (exFAT, ffpkg, ffpfsc), das **LZ4-Packen in einen neuen Ordner** (die Datei im Ergebnis ist die erwartete FSELF, der kopierte AMPR-Index nennt ihre neue Größe) und **zstd im Spielordner** mit dem Schalter (Datei ersetzt, Index angepasst, nach „zstd entpacken“ alles wieder da) liefen an einer Testkopie von Double Dragon Revive ohne Fehler.
- **zstd mit AMPR EMU NG 0.3.3 an einem echten Spiel:** Matchbox Driving Adventures (6 GB, Ordner auf usb0, libSceAmpr im `eboot.bin`): mit NG ungepackt startet es; in zstd-Pakete gepackt (Stufe 1, 166 Dateien, 5,87 → 2,96 GB, 528 s, jede Datei vor dem Entfernen der Originale Byte für Byte verglichen) **läuft es aus den Paketen mindestens zwei Minuten im Vordergrund** ohne Auffälligkeit im Kernel-Protokoll; „zstd entpacken“ brachte alle 166 Dateien wieder (560 s), NG wurde entfernt, der Ordner entspricht dem Zustand davor (198 Dateien, gleiche Größe). Ob das Spiel bei längerem Spielen fehlerfrei bleibt, ist nicht geprüft (nur der Zustand des Spiels, nicht das Bild, wurde gelesen).
- **LZ4 1.11.0** packte auf der Konsole 128,5 MB auf 24,7 MB, prüfte und entpackte fehlerfrei.

### AMPR EMU NG 0.3.3 fest eingebaut
- **AMPR EMU NG v0.3.3** (quer3q, 07.11.2026) löst die eingebettete 0.3.2 ab (beide Dateien, `libSceAmpr.sprx-0.3.3-ng` und `-ng-debug`; die alten fliegen heraus, Wunsch des Users bei jeder NG-Fassung). Prüfsummen gleich denen der `SHA256SUMS` der Veröffentlichung, SELF-Kopf und Kennung `ampr-emu-ng 0.3.3` geprüft.
- Behoben in der Laufzeit (laut Notizen): vollständig installierte Spiele fragen nicht mehr nach dem Weiterladen oder freiem Platz, Marvel's Wolverine startet wieder, weniger Hänger und schwarze Bilder bei Spielen, die viele gepackte Dateien gleichzeitig öffnen, Ghost of Yōtei und andere Spiele, die im Hintergrund über das System lesen, können gepackte Dateien lesen, Hollow Knight Silksong hält beim Start nicht mehr an (Ordner-Installation auf der internen SSD). Die Debug-Fassung listet gepackte Dateien jetzt auch bei Erfolg.
- **Format unverändert:** `docs/zstd-format.md` ändert nur, wie die Laufzeit gepackte Dateien als Kennungen hält (künstliche Nummern 512 bis 1023 statt reservierter `/dev/null`-Kennungen) und dass sie auch das AIO des Spiels bedient; Aufbau von Index und Paketen bleibt, `src/zstdpack.c` brauchte keine Änderung.
- Neu in `src/amprng.c` (`NG_VERSION`, Namen, Größen, SHA-256), `third_party/ampr_emu/ORIGIN.md`, `THIRD_PARTY_NOTICES.md`, Handbuch (sieben Sprachen), `docs/HANDBUCH.md`, `docs/API.md`, READMEs, Credits (Browser und Vollbild) und `tools/upstream.json`.
- Gelesen, nicht eingebaut: LZ4 v1.10.0 → v1.11.0 (die APR-EMU liest LZ4-Daten dieser Fassung, Tausch nur mit Absprache) und ps5upload v11.2.5 → v11.5.2 (nur Wissen).

### LZ4 1.11.0 statt 1.10.0
- Die LZ4-Bibliothek der LZ4-Pakete (`src/third_party/lz4`, nur die Blockfunktionen `LZ4_compress_fast_extState`, `LZ4_compress_HC_extStateHC`, `LZ4_decompress_safe`) ist auf **v1.11.0** (Yann Collet, BSD-2-Clause) getauscht (Wunsch des Users, 08.11.2026); Archiv-Prüfsumme gleich der der Veröffentlichung. Das Blockformat ist seit 2012 gleich; die APR-EMU entpackt weiter mit ihrer eigenen Fassung (1.10.0).
- **`test/lz4cross`** (neu): Blöcke der alten und der neuen Fassung (schnell und HC 3 bis 12, 17 Größen von 1 Byte bis 4 MiB, vier Arten Daten, 884 Blöcke, 457 MB) werden in beide Richtungen gelesen: kein Unterschied. 1.11.0 packt mit denselben Stufen minimal kleiner (−0,1 %).
- Neu in 1.11.0, aber hier nicht benutzt: Mehrfädigkeit im Rahmenformat (`lz4frame`), schnellere kleine Blöcke und Wörterbücher.

### zstd: keine Wahl mehr, ob die Originale bleiben
- Beim Packen mit **zstd** gibt es in Browser und Vollbild-App **keine Wahl** mehr, ob die Originale bleiben oder ein neuer Ordner `…-zstd` entsteht: gepackt wird immer im Spielordner, die Originale werden erst entfernt, wenn jede gepackte Datei Byte für Byte mit ihrem Original verglichen ist (bei einem Fehler bleibt alles wie vorher; **zstd entpacken …** bringt alle Dateien zurück). Das Fenster nennt das stattdessen (Browser: Hinweis unter der Warnung, Vollbild: Zeile „Originale“ – „entfernt nach der Prüfung“).
- **zstd signiert auf Wunsch im Spielordner** (Schalter „Programmdateien als FSELF signieren“, nur wenn der Plan unsignierte Programmdateien nennt; Browser: Kästchen, Vollbild: Schalter mit der Zeile „Programmdateien“): nach dem Packen und dem Entfernen der Originale wird jede Programmdatei neben dem Original als `<Name>.ps5cc-sign-tmp` geschrieben, gegen das Original zurückgelesen und darüber umbenannt (Rechte und Zeiten bleiben), der `ampr_emu.index` wird angepasst. Scheitert eine, bleiben die fertigen signiert und die übrigen, wie sie waren; die Notiz des Auftrags sagt es. `POST /api/v1/library/lz4` kennt dafür `sign`. Die Prüfung steht in `test/fsign` (Ersetzen, Zeit, Index, Änderung während des Signierens, Abbruch, nicht lesbare Datei).
- Das Kästchen **„ungepackt mit AMPR EMU NG getestet“** bleibt (Wunsch des Users); aus der Warnung „Vorher erledigen“ ist nur der Satz „Behalte das Original, bis du das gepackte Spiel gestartet hast“ entfallen, weil er nicht mehr stimmt. **LZ4 bleibt, wie es war.**
- Die Schnittstelle (`POST /api/v1/library/lz4`) nimmt `kind: copy` mit `format: zstd` weiter an; die Oberflächen schicken es nicht mehr. Handbuch (alle sieben Sprachen, zstd-Abschnitt, „Stand“, Unsignierte Programmdateien) und FAQ angepasst, neue Texte in allen sechs Sprachen.

### Fremdbausteine
- **ps5-dump-forge 0.5.4** (quer3q, GPL-3.0; vorher 0.5.3 gelesen): zstd-Format unverändert (`docs/zstd.md` ohne Änderung), die mitgelieferte ampr-emu-ng 0.3.2 steckt schon in der App; neu eingebaut ist das Signieren (siehe oben).

## v1.77.1 – 07.11.2026

### Die Kacheln ersetzen eine ältere laufende App
- Beide Kacheln (Vollbild FAKE86921 und Browser FAKE86922) schickten ihre eingebaute App bisher nur, wenn auf Port 8086 nichts antwortete. Wer neue Kachel-Ordner über alte kopierte, ohne die Konsole neu zu starten, behielt die **alte App** und sah zum Beispiel noch „1.76.9“ (gemeldet von einem Nutzer des Releases 1.77.0).
- Jetzt **vergleichen die Kacheln die Versionen**: Die App nennt ihre in `GET /api/v1/system` (`app_version`), die ELF in der Kachel trägt die Marke `PS5CC-VERSION:<Version>:` (neu in `src/main.c`). Ist die laufende App **älter**, geht die eingebaute über sie (die neue beendet die alte beim Start), und die Kachel wartet, bis die neue Version antwortet (höchstens 30 s). Eine **gleiche oder neuere** App bleibt, ebenso eine ohne Versionsangabe.
- **Läuft ein Auftrag** (Abbild/LZ4/zstd-Pakete, Dateimanager, Kopieren/Konvertieren/Löschen, Paket-Installation, Spielstände, AMPR-Index), wird nichts ersetzt: Das Ende der App würde den Auftrag abbrechen. Die Kachel meldet „Eine neuere App liegt bereit“; nach dem Auftrag noch einmal öffnen.
- Geprüft am PC: Einheitstests der Vergleichslogik und der Versionsmarke (`ps5cc_app_update_test`, 327 Tests insgesamt) und ein Ende-zu-Ende-Test der Browser-Kachel gegen eine nachgebaute App und einen nachgebauten Loader (älter und frei: ersetzt; gleich, neuer, mit Auftrag: bleibt; keine App: wie bisher gesendet). An der Konsole noch nicht erprobt.

## v1.77.0 – 06.11.2026

### Löschen fragt zuerst, und nennt, was weggeht
- Jeder „Löschen“-Knopf löschte bisher sofort. Jetzt öffnet sich erst ein Fenster, das **nennt, was weggeht**; erst **Löschen** im Fenster löscht. **Abbrechen** hat den Fokus, ein Enter oder die ✕-Taste löscht also nichts; ×, O, Escape und ein Klick daneben schließen das Fenster, ohne etwas zu tun.
- **Dateimanager** (Zeilenmenü, Auswahlleiste, Vollbild-Aktionen): die Namen (Datei oder Ordner, bei Dateien die Größe), der Ordner, in dem sie liegen, und bei Ordnern, wie viele Dateien mit wie viel Platz darin stecken (die Konsole zählt nach); bei vielen Einträgen „und N weitere“. Der Plan der Konsole mit seinem Kennwort (`files/delete/plan`) wird wie bisher geholt – im Browser schon beim Öffnen des Fensters, in der Vollbild-App beim Bestätigen.
- **Spiele** (der Plan der Konsole mit „Gelöscht wird: …“ und der Warnung blieb stehen, statt automatisch zu starten) und **Sicherungen der App**; in der Vollbild-App hat der Spiele-Dialog dafür eine Zeile „Abbrechen“ (dort liegt der Fokus), und △ löst das Löschen nicht mehr aus.
- Weiter mit Namen und Hinweisen: **Spielstände** (mit „wird vorher auf … gesichert“ oder dem Hinweis, dass es kein Laufwerk dafür gibt), **Payload-Dateien** und **Payload-Profile**, **Disc-Abbilder** und **Dumps**, **eigene Hintergrundbilder** (nur Browser), der **Verlauf**, der **Spielzeit-Verlauf** und die gespeicherten **Kernel-Log-Dateien**.
- Browser und Vollbild-App fragen gleich; die Vollbild-Seiten teilen sich die Klasse `DeleteAsk` (`tvapp/src/ps5cc/delete_ask.hpp`), der Browser die Funktion `askDelete` (Spiele und Dateimanager haben eigene Fenster, weil sie den Plan der Konsole zeigen).
- Nicht gefragt wird weiter bei Aktionen, die nichts löschen: Beenden, Abbrechen, Power-Optionen, Deaktivieren/Aktivieren, Entfernen aus einer Warteschlange oder einem Profil, Leeren der Zwischenspeicher.
- Neue Texte in allen sechs Sprachen (Browser und Vollbild-App), Handbuch (alle sieben Fassungen: Vollbild-Kapitel, Dateimanager, Spiele löschen, Spielstände, Verlauf) und FAQ angepasst; die Vollbild-Tour zeigt die Fragen („loeschen-rueckfrage“, „dateien-loeschen“, „sicherungen-loeschen“, „spielzeit-loeschen“).

### AMPR EMU NG fest eingebaut (v0.3.2)
- **AMPR EMU NG** (quer3q) steckt jetzt in der ELF, normal und als Debug-Fassung (`libSceAmpr.sprx-0.3.2-ng`, `-ng-debug`, 325 und 366 KB gepackt); sie erscheinen in der APR-EMU-Auswahl ganz oben mit der Marke „NG“, auch ohne Internet. Den Knopf „AMPR EMU NG von GitHub laden“ gibt es weiter für künftige Veröffentlichungen; solange eine Fassung mitgeliefert ist, gilt sie als vorhanden. `tools/gen_emu_blob.py` nimmt dafür auch die Kennung `ampr-emu-ng` an.
- **Platz:** Die beiden älteren drakmor-Fassungen **0.3.6.6 test** und **0.3.6.6 test-debug** liegen der App nicht mehr bei (Wunsch des Users); sie lassen sich mit „Datei hinzufügen“ von drakmors Veröffentlichungsseite wieder ablegen. Damit wächst die ELF von 9,61 auf 10,04 MB (die Konsole lud 10,5 MB nicht, 9,88 MB lud sie; erst an der Konsole bestätigen, ob 10,04 MB geladen werden).
- Handbuch (alle sieben Sprachen), `third_party/ampr_emu/ORIGIN.md`, `THIRD_PARTY_NOTICES.md`, Credits (Browser und Vollbild, mit Übersetzungen) und `tools/upstream.json` nennen NG 0.3.2; quer3q steht jetzt auch in den Credits und in den READMEs.

- **NG 0.3.2** (quer3q, 06.11.2026) löste am selben Tag die kurz eingebettete 0.3.1 ab (Wunsch des Users: die alte entfernen): Resonance: A Plague Tale Legacy läuft als vollständig gepacktes Spiel (vorher schwarzes Bild), geöffnete gepackte Dateien zählen nicht mehr gegen das Dateilimit der Laufzeit.

### AMPR EMU NG: die App fragt, ob ShadowMountPlus umgestellt werden soll
- ShadowMountPlus ersetzt die `libSceAmpr.sprx` beim Spielstart aus seinem Emulator-Ordner, solange seine automatische Fakelib-Aktualisierung an ist (`update_emulators=1`, Standard) – dann wäre die eingebaute NG-Datei gleich wieder weg. Statt nur zu warnen, **fragt die App jetzt beim Einbauen von NG** („ShadowMountPlus umstellen?“): **Ja** schaltet die Aktualisierung in ShadowMountPlus ab und baut NG ein, **Abbrechen** lässt alles, wie es ist (NG wird dann nicht eingebaut). Die Frage kommt nur, wenn die Aktualisierung an ist und eine NG-Version gewählt wurde; in Browser und Vollbild-App gleich.
- Geändert wird nur die eine Zeile `update_emulators` (bei der neuen Gliederung `update_enabled` unter `[emulators]`) in `/data/shadowmount/config.ini`: der Rest der Datei bleibt byteweise gleich (die Datei enthält auch SMB-Zugangsdaten), Zeilenenden, Kommentare und Dateirechte bleiben, geschrieben wird atomar, die ursprüngliche Datei liegt einmal als `config.ini.ps5cc-bak` daneben, danach liest die App die Datei zur Probe noch einmal. Die SMP-Schnittstelle (Port 10101) wird nicht benutzt. Neu: `GET`/`POST /api/v1/ampr-emu/smp-update`, in `GET /api/v1/ampr-emu` steht `emus.update_emulators` wie bisher. Neue Quelle `src/smpcfg.c` mit Host-Test `test/smpcfg` (66 Prüfungen), das Lesen der Einstellung in `src/amprlib.c` läuft jetzt über dieselbe Analyse.
- Bleibt: die Meldung nach dem Einbauen, wenn die Aktualisierung doch noch an ist (zum Beispiel, wenn die Änderung nicht geschrieben werden konnte); ist sie aus, steht nur ein kurzer Hinweis.
- Neue Texte in allen sechs Sprachen (Browser und Vollbild-App); Handbuch (sieben Sprachen), `docs/HANDBUCH.md` und `docs/API.md` angepasst; die Vollbild-Tour zeigt die Frage („extras-ng-frage“).

### zstd-Pakete: LZ4 oder zstd zur Wahl
- Neben den LZ4-Paketen packt die App Spiele jetzt auch in **zstd-Pakete** (`ampr_zstd.index` und `ampr_zstd-NNN.pak`), das Format von **AMPR EMU NG** (quer3q, `docs/zstd-format.md`). Im Fenster des Spiels stehen **Mit LZ4 packen …** und **Mit zstd packen …** nebeneinander (Browser und Vollbild-App); verglichen wurde mit dem Beispiel im PS5 Dump & Image Converter und mit ps5-dump-forge 0.5.3.
- Gepackt wird das **ganze Spiel außer den Systemdateien** (Programme, `sce_sys`, `sce_module`, `fakelib*`, leere Dateien, die Pakete und die Dateien der APR-EMU, dazu die wenigen Dateien, die die PS5 selbst auf der Platte sucht): kein Profil, kein „Automatisch“. **Stufe 1 bis 22** (angeboten 1, 5, 9, 19; Vorgabe 5), jedes Stück von 256 KiB ein eigener zstd-Frame mit Prüfsumme, roh, wenn zstd es nicht verkleinert. Drei Rechenfäden mit niedriger Priorität wie beim LZ4-Packen; die Zeitschätzung lernt die Geschwindigkeit der Konsole (`zstd-speed.txt`).
- Ablauf wie bei LZ4: Spielordner prüfen, AMPR-Index bauen, packen, **jede Datei Byte für Byte mit dem Original vergleichen**, AMPR EMU NG in `fakelib` einbauen, erst dann die Originale entfernen (jeder Fehler nimmt die Pakete wieder weg); **Pakete prüfen**, **zstd entpacken** und „Als neuen Ordner …-zstd daneben“ ebenso. Ein Spiel liegt in LZ4- **oder** zstd-Paketen; in einem zstd-Spiel sind alle APR-EMU-Versionen außer NG gesperrt (Browser, Vollbild-App und App-Schnittstelle). Das Spiel trägt in der Liste die Marke „zstd gepackt“.
- Neu: `src/zstdpack.c` (Packer, Prüfer, Entpacker; Host-Test `test/zstdpack` mit 70 Prüfungen unter AddressSanitizer, dazu der unabhängige Leser des Converters), zstd 1.5.7 in `src/third_party/zstd` (BSD-3, ohne Mehrfädigkeit und Assembler-Entpacker), in `POST /api/v1/library/lz4` und `GET …/lz4/plan` der Parameter `format` (`lz4`/`zstd`), in den Antworten `pack.kind`, `lz4_format`, `mods.zstd`, `packed_zstd`. Handbuch (sieben Sprachen, dazu eine FAQ-Frage „LZ4 oder zstd?“), `docs/API.md`, Wörterbücher (sechs Sprachen, Browser und Vollbild) und `THIRD_PARTY_NOTICES.md` angepasst.
- **An der Konsole noch nicht erprobt** (weder das Packen noch das Spielen eines zstd-gepackten Spiels); der Weg ist an der Konsole nur so sicher wie AMPR EMU NG selbst. Erst an einem Spiel versuchen, dessen Original bleibt.

### Die ELF ist wieder kleiner
- zstd, NG 0.3.2 und die neuen Funktionen hätten die ELF auf 10,5 MB gebracht (dort lud die Konsole sie nicht mehr). Gegengerechnet: die beiden bisher **unkomprimiert** eingebetteten Programme (np-fake-signin, der Installationshelfer) stecken jetzt als zlib-Datenstrom in der ELF und werden erst beim Gebrauch entpackt (`src/blobz.c`, −150 KB); die Textdateien der Oberfläche packt `tools/gen_assets.py` mit zopfli (gleiches gzip-Format, 5 % kleiner), die Bilder (Oberfläche und Kachel) sind verlustfrei neu gepackt (`tools/optimize_png.py`, pixelgleich geprüft); fünf Dateien, die nirgends mehr gebraucht wurden (`icon-tiles.png`, `icon-gear.png`, `icon-user.png` und zwei Credit-Bilder), sind weg; etwa 440 Wörterbucheinträge je Sprache, die kein Text des Programms mehr verlangt (Reste entfernter Funktionen), sind entfernt; zstd ist auf Größe gebaut (`-Os`). Ergebnis: **9,87 MB** (vorher 10,04 MB, mit zstd ohne diese Maßnahmen 10,5 MB).

### Fremdbausteine
- **PS4Mount 0.7.2 → 0.7.3** (gasb07, `src/ps4m_core.c`): bis zu **768 Spiele je Laufwerk** (vorher 256; darüber meldet die App die Zahl der nicht hinzugefügten Spiele), bis zu **64 Suchordner** (vorher 8; ignorierte Ordner werden gemeldet), und die Zusatzinhalte eines Spiels sind nicht mehr auf 64 begrenzt: Der Scan hält je Zusatzinhalt nur einen kleinen Eintrag, das Paket wird erst beim Einbauen geöffnet. **Reine Freischaltungen** (ohne Daten) zählen nicht mehr gegen die Grenze von 29; „Alle an“ schaltet alle Freischaltungen und die ersten 29 Zusatzinhalte mit Daten ein (Browser und Vollbild-App, Texte in allen Sprachen). `POST /api/v1/ps4mount/dlc` reicht lange Listen ohne Abschneiden weiter. Nicht übernommen: `web.c` und die Panel-Seiten des Originals. Unit-Tests der Portierung: 157 von 157.
- **Nur gelesen, nicht eingebaut:** LZ4 v1.10.0 → v1.11.0 (mehrfädige Kompression in `liblz4`; die Datei bleibt binärkompatibel, LZ4 wird nur mit Absprache getauscht), ps5upload v10.0.3 → v11.2.5 (nur Wissen; nichts, was den Start über eine Meldung oder die Prüfung der aufrufenden Prozess-ID berührt; der Eintrag steht jetzt auf v11.2.5).

### Dokumentation
- Der Dateimanager im **Handbuch** (alle sieben Sprachen) und in `docs/HANDBUCH.md` beschreibt jetzt den heutigen Stand: Orte und Zielwahl, „Kopieren nach“ / „Verschieben nach“, Ersetzen / Beide behalten / Überspringen, Auftragsanzeige mit Anzahl, Herkunft, Ziel, Geschwindigkeit und Restzeit, Bearbeiten, ganze Ordner hochladen, Löschen mit Rückfrage. `docs/API.md` kennt `POST /api/v1/files/write`.
- **README:** Schwedisch fehlte (Abzeichen, Sprachleiste, Fußzeile) – neu `README.sv.md`. Die übersetzten READMEs (en, es, fr, it, ru) waren hinter der deutschen zurück: Sie sprachen noch von den „Spiel-Updates“, die es seit 29.10. nicht mehr gibt (Datenschutz, Rechtliches, Technik), nannten die zweite Kachel nicht und ließen die Danksagungen an Knutwurst (PatchDL), BearSSL und das Vorbild für „Payloads herunterladen“ aus; alles nachgezogen, dazu der Dateimanager-Punkt („Ändern nur auf Laufwerken und in `/data`“ stimmte nicht mehr) und quer3q.
- `docs/HANDBUCH.md`: Sprachliste um Svenska (`sv-SE`) ergänzt; „Überschrieben wird nie etwas“ und „Löschen wirkt sofort“ berichtigt.

### Browser-Oberfläche: Logo mit runden Ecken
- Das Symbol links neben dem App-Titel hat jetzt abgerundete Ecken (10 px bei 40 px Kantenlänge). Die Bilddatei ist quadratisch ohne Transparenz; die Rundung macht das Stylesheet.

## v1.76.9

### Dateimanager: wie viele Einträge, von wo nach wo
- Die Auftragsanzeige beim Kopieren, Verschieben und Löschen sagt jetzt, **wie viele Dateien/Ordner** es sind und **welcher gerade** dran ist, und darunter, **woher und wohin**: „Kopiere 1 von 2 Dateien/Ordnern · 37 %“ und „von /mnt/usb0/neu nach /mnt/ext1“ (bei einem Eintrag „Kopiere 1 Datei/Ordner“; kommen die Einträge aus mehreren Ordnern, steht nur „Ziel: …“; beim Löschen „Ordner: …“). Im Browser steht in der Mengenzeile außerdem, wie viele der enthaltenen Dateien schon fertig sind („1 von 3 Dateien“). Browser und Vollbild-App zeigen dasselbe; der Text ist in allen Sprachen übersetzt.
- `GET /api/v1/files/job` nennt dazu `from` (der Ordner, aus dem alle Einträge kommen; leer bei gemischten Quellen); `items` und `items_done` gab es schon. Host-Test `test/filemgr/run.sh` jetzt 56 Prüfungen; neue Unit-Tests `format_job_title` und `format_job_route` (322 Tests).

## v1.76.8

### Browser-Oberfläche: Vollbild nur noch im Browser der PS5
- Der erste Druck irgendwo auf der Seite schaltete bisher in jedem Browser auf Vollbild; am PC musste man F11 drücken, um die Taskleiste wiederzusehen. Jetzt geschieht das nur noch im Browser der PS5 selbst (dort gibt es keine Taskleiste; erkannt am `PlayStation` im Browsernamen). Am PC und am Handy bleibt die Seite im normalen Fenster, bis jemand **Vollbild** drückt; der Knopf merkt sich das für die nächsten Starts, **Vollbild beenden** schaltet es wieder ab. Wer es schon einmal bewusst ein- oder ausgeschaltet hatte, behält seine Wahl.

### Dateimanager: Kopieren nach / Verschieben nach für die ganze Auswahl
- In der Leiste über der Auswahl stehen neben „Kopieren“, „Ausschneiden“, „Löschen“ und „Auswahl aufheben“ jetzt auch **Kopieren nach** und **Verschieben nach**. Sie öffnen dieselbe Zielwahl wie „⋯“ bei einem einzelnen Eintrag (Laufwerke oben, Ordner zum Hineinwechseln, „Hierhin kopieren/verschieben“), aber für alle ausgewählten Einträge auf einmal. Die Auswahl ist danach aufgehoben. „Verschieben nach“ ist wie „Ausschneiden“ nur möglich, wenn alle Einträge änderbar sind, und im selben Ordner gesperrt. Die Vollbild-App hat dafür weiter die Ablage („Kopieren/Ausschneiden“ und „Hier einfügen“), die mehrere Einträge auf einmal nimmt.

## v1.76.7

### Vollbild-App: Version im Titel
- Die Titelzeile zeigt nach „PS5 COOLING & SYSTEM CENTER Pro“ jetzt die Version der laufenden App („v1.76.7“), klein und gedämpft; der Trennstrich und der Name der Seite rücken ein Stück nach rechts. Die Zahl liest die Vollbild-App beim Start aus `GET /api/v1/system` (`app_version`), sie ist also die Version, die auf der Konsole wirklich läuft, nicht die der Kachel.

## v1.76.6

### Browser-Oberfläche: Versionsnummer links
- Die Versionsnummer („Version 1.76.5“) steht jetzt in der Seitenleiste direkt unter „FAQ“ statt in einer Fußzeile ganz unten, für die man scrollen musste. Sie liegt auf einer dunklen Unterlage, weil sie über dem Lüfterbild steht; in der schmalen Leiste oben (Handy) steht sie als schlichter Text unter den Reitern. Die Fußzeile entfällt.

### Dateimanager: Geschwindigkeit und Restzeit
- Beim Kopieren und Verschieben zeigt die Auftragsanzeige jetzt die **Übertragungsrate** („82 MB/s“) und die **geschätzte Restzeit** („noch ca. 12 Min.“), im Browser und in der Vollbild-App. Die App misst beides selbst: einmal pro Sekunde aus den schon übertragenen Bytes, leicht geglättet (70 % alter Wert, 30 % neue Messung); stockt die Übertragung länger als vier Sekunden (langsamer Stick, große Datei wird geschrieben), sinkt die Anzeige auf das, was seit der letzten Messung wirklich ankam. In den ersten Sekunden und beim Zählen und Löschen steht nichts da.
- `GET /api/v1/files/job` nennt dazu `rate_bps` (Bytes pro Sekunde) und `eta_s` (Sekunden, 0 = unbekannt); beide sind nur beim Kopieren/Verschieben belegt.
- Neue Prüfung in `test/filemgr/run.sh` (jetzt 55): eine auf 25 MB/s gebremste Kopie liefert eine Rate im richtigen Bereich und eine nicht wachsende Restzeit; nach dem Ende steht beides wieder auf 0. Neue Unit-Tests für `format_rate` und `format_eta` der Vollbild-App (320 Tests).

## v1.76.5

### Offline-Konto: wie OffAct (v0.4.2)
- Ein Vergleich mit dem Quelltext von OffAct (`master` ist noch v0.4.2; die neu geladene `OffAct-1.zip` ist ein Build davon, ohne Quelltext) ergab zwei Abweichungen unseres Einbaus, die jetzt behoben sind: OffAct **schreibt nie einen Namen**, sondern macht einen **vorhandenen Benutzer** zum Offline-Konto (Konto-ID, Typ `np`, Kennzeichen), und es setzt die Kennzeichen auf **4098**, wir hatten **1**. Außerdem hasht es den Namen mit vorzeichenbehaftetem `char`: Ein Name mit Umlaut bekommt jetzt dieselbe Konto-ID wie dort.
- **Oberfläche:** Im Browser steht statt des Namensfelds eine Liste der vorhandenen Benutzer (Platz, Typ, Konto-ID) mit „Aktivieren“; die aktive Zeile leuchtet grün. In der Vollbild-App öffnet „Offline-Konto aktivieren“ eine Auswahl der Benutzer (bei nur einem wird er sofort aktiviert); die Tastatur dafür entfällt. `GET /api/v1/profile/account/status` nennt dazu `accounts[]`, `POST …/account/activate` nimmt `slot` (und optional `id`); `username` geht weiter, sofern es den Benutzer gibt.
- Der Zustand „aktiv“ hieß bisher nur „Platz 1 hat einen Namen“ (das trifft auf jeden Benutzer zu); jetzt heißt er „Konto-ID gesetzt und Typ `np`“.
- Neuer Host-Test `test/account/run.sh` (29 Prüfungen gegen eine Registrierung im Speicher, auch dass das Zurücksetzen der Fake-Anmeldung das Konto stehen lässt).

### Dateimanager: Zielwahl und Ordnerfarbe
- In „Kopieren nach“ haben die Ordnerzeilen die Größe und Schrift wie im Dateimanager (vorher riesige Symbole mit kleinem Namen); Ordner sind überall **gelb**, Dateien bleiben grau. Bei `/data` leuchtet nur noch „Interne SSD“, nicht zugleich „Ganzes System“.
- Das Fenster „Namen schon vorhanden“ schließt sich nach dem Hochladen von selbst.

## v1.76.4

### Kacheln: Hintergrundbild im Startmenü
- Die Konsole zeigt das große Bild, das beim Anwählen (`pic0`) und beim Starten (`pic1`) einer Kachel im Startmenü erscheint, nur als **BC7-DDS in 3840×2160** (`pic0.dds`/`pic1.dds`); ein `pic0.png` daneben wird nicht gelesen. Die Kacheln `FAKE86921` und `FAKE86922` hatten bisher nur PNG, deshalb blieb das Bild aus. `tools/make_starter.sh` erzeugt die beiden Dateien jetzt beim Bau aus `starter/sce_sys/pic0.png` (neues Werkzeug `tools/png_to_bc7_dds.py`, ohne Zusatzprogramme: Pillow und numpy genügen; Kopf und Größe gleich wie die Beispiele des Vollbild-Kits) und legt sie in beide Kacheln. Bestehende Kacheln: die Dateien nach `/data/homebrew/<ID>/sce_sys/` kopieren und zusätzlich nach `/user/app/<ID>/sce_sys/` und `/user/appmeta/<ID>/`, denn die Konsole kopiert diese Bilder nur bei der ersten Anmeldung.
- Die von der ELF selbst angelegte Kachel `PSCC69690` bleibt bei PNG: Ein 8-MB-Bild passt nicht in die ELF (ab etwa 10,5 MB bedient die Konsole keine Anfragen mehr).

### Dateimanager: Hochladen in einen vergebenen Namen
- Lud man eine große Datei unter einem Namen hoch, den es im Ziel schon gab, antwortete der Server, bevor er die Datei empfangen hatte; der Browser meldete dann nur „Die Verbindung brach ab“. Der Server liest die Übertragung jetzt zu Ende, bevor er antwortet.
- Beim Hochladen fragt dieselbe Rückfrage wie beim Kopieren: **Ersetzen**, **Beide behalten** (`Name (2).ext`) oder **Überspringen**. `POST /api/v1/files/upload` nimmt dafür `conflict` (`replace`, `keep`, `skip`). 14 neue Host-Prüfungen (`test/filemgr/run.sh`, jetzt 50).

## v1.76.3

### Offline-Konto: Zustand gut sichtbar, Name auswählen statt eintippen
- Die Statuszeile von „Offline-Konto aktivieren“ und „Fake-Anmeldung schreiben“ bekommt einen leuchtenden grünen Rand, solange die Funktion aktiv ist (Browser); in der Vollbild-App zeigt die Zeile dafür einen grünen Punkt.
- Über dem Namensfeld erscheinen anklickbare Vorschläge: der echte lokale Benutzername der Konsole und, falls abweichend, der zuletzt aktivierte Name. Ein Klick übernimmt den Namen; ist das Feld leer, wird es automatisch damit vorbelegt.

## v1.76.2

### Fehlerbehebung (schwerwiegend): „Alles zurücksetzen“ löschte den Namen des Offline-Kontos
- Vier der Registrierungsschlüssel, die die Fake-Anmeldung zurücksetzte, sind dieselben wie Name, Konto-ID, Kennzeichen und Art des **Offline-Kontos** (beide Funktionen teilen sich auf der Konsole denselben Platz 1). „Alles zurücksetzen“ leerte sie deshalb mit, auch wenn das Offline-Konto weiter gebraucht wurde. Nach einem Neustart hatte die Konsole dann keinen Namen mehr für das Konto, und Programme, die eine vollständige Kontokennung erwarten (z. B. der Y2JB), liefen nicht mehr richtig.
- Der Knopf heißt jetzt **„Fake-Anmeldung zurücksetzen“** (Browser und Vollbild-App) und lässt Name, Konto-ID, Kennzeichen und Art des Offline-Kontos unangetastet; gelöscht werden nur auth.dat, config.dat und die Registrierungsschlüssel, die ausschließlich die Fake-Anmeldung betreffen.
- **Wer schon „Alles zurücksetzen“ gedrückt hat:** Unter „Profil“ → „Offline-Konto aktivieren“ den Namen erneut eingeben, das setzt die Kontokennung wieder.

## v1.76.1

### Vollbild-App: Reiter „Konto“ im Profil
- Neue Ansicht „Konto“ zwischen „Profil“ und „Danke“ (L2/R2): **Offline-Konto aktivieren** (der Name wird auf der Bildschirmtastatur abgefragt, vorbelegt mit dem Benutzernamen), **Fake-Anmeldung schreiben** und **Alles zurücksetzen**. Jede Zeile zeigt ihren Zustand („aktiv: Name“, „vorhanden“); ✕ führt sofort aus, ohne Rückfrage.
- `GET /api/v1/profile/account/status` nennt jetzt auch `username` (der Name des Offline-Kontos); die Browser-Karte zeigte vorher „Konto „?“ (Slot 1)“.
- Übersetzungen der neuen Texte in sechs Sprachen.

### Dateimanager: Laufwerke in der Zielwahl, Ersetzen und Beide behalten
- **Kopieren/Verschieben nach …** zeigt oben die Orte der Konsole (Laufwerke, `/data`, USB …) als Schaltflächen; vorher konnte man nur durch die Unterordner des aktuellen Ordners gehen.
- **Namen, die es im Ziel schon gibt:** Statt einer Fehlermeldung fragt ein Fenster wie im Prospero Manager: **Ersetzen**, **Beide behalten** (der neue Eintrag heißt `Name (2)`, bei Dateien `Name (2).txt`), **Überspringen** oder Abbrechen. Gilt für Kopieren, Verschieben und Einfügen aus der Ablage, in der Browser-App und in der Vollbild-App.
- Eine Datei wird beim Ersetzen erst neben die alte geschrieben und am Ende darübergelegt (bricht das Kopieren ab, bleibt die alte unverändert); ein Ordner wird zusammengeführt. Eine Datei lässt sich nicht durch einen Ordner ersetzen (und umgekehrt), und ein Eintrag nicht durch sich selbst. `POST /api/v1/files/copy|move` nimmt dafür `conflict` (`replace`, `keep`, `skip`).
- Neuer Host-Test `test/filemgr/run.sh` (36 Prüfungen mit echten Dateien, unter Adress- und Undefined-Behavior-Prüfung).

### Kachel für „nur ELF“
- Hält die Schutzprüfung die Anmeldung auf (ein anderer Titel wartet unter `/user/app`, die Sammelanmeldung hätte ihn mit erfasst), meldet die App das auf dem Fernseher und versucht es beim nächsten Start erneut, statt den einen Versuch zu verbrauchen.
- Wer nur die ELF sendet und keine Kachel hat, bekommt wieder eine: Etwa eine Minute nach dem Start legt die App die Medien-Kachel (`PSCC69690`, öffnet die Weboberfläche) einmal selbst an – im Hintergrund, nur wenn gerade kein Spiel läuft und weder sie noch ein Ordner `FAKE86921`/`FAKE86922` in `/data/homebrew` da ist. Dieselbe Anmeldung wie bei `POST /api/v1/tile/ensure`, also ohne die Sammelanmeldung, die fremde Titel mit erfasst hätte. Die Notiz `tile-auto` hält `ok` (nie wieder) oder `fail <Version>` (nächste Version wieder) fest und wird vor dem Aufruf geschrieben; eine später gelöschte Kachel kommt nicht von allein zurück. Die FAKE-Kacheln selbst kann die ELF nicht erzeugen (35 MB bzw. eine Kopie ihrer selbst).

### Fehlerbehebung: Fake-Anmeldung zurücksetzen
- Die Statusanzeige („Fake-Anmeldung vorhanden“) und „Alles zurücksetzen“ suchten `auth.dat` und `config.dat` unter `/system_data/priv/home/0x1EA2F4D9/`. np-fake-signin schreibt aber nach `/system_data/priv/home/1ea2f4d9/` (klein, ohne `0x`): Die Anzeige blieb bei „keine“, und das Zurücksetzen löschte die Dateien nicht.

## v1.76.0

### Profil: Offline-Konto und Fake-Anmeldung
- Neuer Reiter-Bereich im Profil: **Offline-Konto aktivieren** setzt Name, Account-ID und Flags über die Registry (OffAct-Verfahren, GPL-3.0, John Törnblom).
- **Fake-Anmeldung schreiben** injiziert die np-fake-signin v1.3 ELF (earthonion) an den lokalen ELF-Lader (127.0.0.1:9021). Die ELF liest den aktiven Benutzer selbst aus und schreibt auth.dat, config.dat sowie alle Registrierungseinträge — keine Eingaben nötig.
- **Alles zurücksetzen** löscht auth.dat/config.dat und leert alle Registry-Schlüssel beider Funktionen.
- Statusanzeige zeigt an, ob das Offline-Konto aktiv ist und ob auth.dat vorhanden ist.

### Durchsicht 03.11.2026
- **Kühlung:** Bei Fensterbreiten von 981 bis 1259 px war die Lüfter-Kachel zu schmal, die Drehzahl („19,0 %“) wurde abgeschnitten. Dort stehen die Kacheln jetzt untereinander wie unter 980 px.
- **Kühlung:** Bei 1260 bis 1379 px wurde „100,0 %“ rechts abgeschnitten. Dort ist der Tacho jetzt etwas kleiner (168 statt 190 px) und der Abstand enger.
- **Design „Hell“:** Die Buchstaben-Symbole im Payload-Store (eigene Payloads grün, Update verfügbar gelb) waren auf dem hellen Grund kaum lesbar (2,8:1 bzw. 1,5:1); jetzt dunklere Farben (über 4,5:1).
- **Übersetzungen der Browser-App:** 44 Protokollmeldungen und 38 sichtbare Texte, die in allen anderen Sprachen deutsch blieben (z. B. „Benutzer-ID“, „zuletzt gespielt“, „Dump-Warteschlange“, „Titel-ID“, „Eigene CPU-Menge …“, „Zeitzone der Konsole …“).
- **Vollbild-App:** fehlende Übersetzungen ergänzt („Selbsttest abbrechen“, „Keine auswählen“, „ja“/„nein“, „sofort“, „gespeichert“, „Auszug speichern“, Aufträge „Verschieben/Entpacken/Installieren → …“); der Reiter „Kernel-Log live“ war in es/fr/it/ru abgeschnitten, „Suma de control“ (es) ebenso – jetzt kürzere Fassungen. Die Angaben zu einem Spiel stehen einzeilig wie in den übrigen Detaillisten (sonst konnten die Zeilen pendeln).
- **PS4 von USB:** Öffneten zwei Seiten gleichzeitig „Updates/Add-ons“ zweier Spiele, konnte eines die Liste des anderen zeigen (gemeinsamer Puffer). Eine Content-ID mit `"` oder `\` aus einem Paketkopf machte die Antwort ungültig; sie wird jetzt maskiert.
- **Kernel-Log speichern:** Füllte eine Zeile den Schreibpuffer genau, stand in der Datei ein NUL-Byte statt des Zeilenumbruchs.
- **PS4 von USB:** Pakete, deren Content-ID nicht aus genau 36 Zeichen A–Z, a–z, 0–9, `-` und `_` besteht, werden nicht mehr geöffnet. Eine zu kurze ID hätte beim Abschalten eines Add-ons den Ordner aller Add-ons des Spiels gelöscht, `/` oder `..` darin hätte aus dem Ordner herausgeführt.
- **System:** Der Schalter heißt auch in den Einstellungen „Expertenmodus“ (vorher „Experte-Modus“), wie oben neben „Vollbild“.
- **Vollbild-App:** Die Reiter heißen „Payload-Profile“ (vorher „Profile“, blieb auf Englisch „Profile“) und „Gespeicherte“ im Protokoll (vorher „Gespeichert“; dadurch hieß auch die Meldung „Gespeichert“ auf Russisch und Französisch „die Gespeicherten“).

## v1.75.7

### Endzeile in `app.log`
- Endet die App, schreibt sie jetzt eine Zeile `===== <Zeit>  … <Version> beendet: <Grund> =====` in `app.log` – vorher alles, was noch nicht in der Datei stand, ohne die 2 Sekunden abzuwarten. Gründe: „Konsole wird ausgeschaltet“, „Konsole startet neu“, „Konsole startet in den abgesicherten Modus neu“ (jeweils über die Power-Optionen der App), „vom System beendet (SIGTERM/SIGINT)“ und „abgestürzt (Signal N)“. Lehnt die Konsole das Ausschalten oder den Neustart ab, folgt `===== … läuft weiter =====` und die Datei nimmt wieder Zeilen an.
- Vor dem Ruhemodus schreibt die App alles Offene sofort in die Datei (ohne Endzeile, denn sie läuft nach dem Aufwachen weiter).
- Einen Neustart über das Menü der Konsole, ein Abschalten per Stromstecker oder die Ablösung durch eine neu gesendete ELF bemerkt die App nicht (SIGKILL lässt sich nicht abfangen). Deshalb prüft jeder Start, ob die letzte Zeile der Datei eine Endzeile ist, und schreibt sonst „(Die vorige Ausführung hat keine Endzeile hinterlassen …)“ vor die neue Startzeile. Anlass: Nach dem Neustart am 03.11.2026 endete die Datei mitten in einer Messung.
- Handbuch (alle sieben Sprachen) und `docs/HANDBUCH.md` beschreiben die Endzeile.

## v1.75.6

### Protokoll übersteht einen Neustart
- Das App-Protokoll lag bisher nur im Speicher und war nach einem Neustart oder Absturz der Konsole weg (so am 03.11.2026: Was die App vor dem Neustart getan hatte, ließ sich nicht mehr feststellen). Jetzt schreibt die App jedes Ereignis zusätzlich in **`/data/PS5-Cooling-Center/app.log`**, im selben Aufbau wie der Export (Ortszeit der Konsole, Stufe, Kennung, Text).
- Ein eigener Faden schreibt alle 2 Sekunden, was neu ist, Warnungen und Fehler sofort, und sichert die Datei jedes Mal auf die Platte (`fsync`). Der Lüfter-Faden wartet dadurch nie auf die Platte. Was schneller kam, als der Speicher des Protokolls (256 Einträge) hält, steht als „… N Einträge kamen zu schnell und fehlen hier“ in der Datei.
- Jeder Start der App beginnt mit einer Zeile `===== <Zeit>  … <Version> gestartet =====`. Ab 1 MB wird die Datei zu `app.old.log` und eine neue beginnt (höchstens etwa 2 MB zusammen).
- Handbuch (alle sieben Sprachen) und `docs/HANDBUCH.md` nennen die Datei.

## v1.75.5

### AMPR EMU NG in der Versionsauswahl
- Die Auswahl „APR-EMU-Version“ kennt jetzt **ampr-emu-ng** von quer3q (<https://github.com/quer3q/ampr-emu-ng>, GPL-3.0), eine Weiterentwicklung von drakmors ampr_emu, die LZ4- und zstd-Pakete liest. Ihre Dateien tragen statt `[AMPR_EMU]` die Kennung `ampr-emu-ng <Version>`; die App erkennt sie daran, zeigt sie als **„NG 0.2.0“** (bzw. „NG 0.2.0 debug“) mit grüner Marke „NG“ ganz oben in der Liste und nennt eine eingebaute NG-Datei auch dann beim Namen, wenn sie nicht im Ordner liegt.
- Mitgeliefert wird NG nicht (die Nutzlast liegt bei 9,9 MB, nahe der Grenze, an der die Konsole sie nicht mehr lud). Stattdessen lädt **„AMPR EMU NG 0.2.0 von GitHub laden“** die beiden Dateien der Veröffentlichung v0.2.0 in den Ordner `AMPR_EMU` (`libSceAmpr.sprx-0.2.0-ng`, `-ng-debug`), nur auf Knopfdruck, über den eigenen HTTPS-Client der App, nur von GitHub; eine Datei bekommt ihren Namen erst, wenn ihre SHA-256 zur Veröffentlichung passt. Neu: `POST /api/v1/ampr-emu/fetch-ng`, in `GET /api/v1/ampr-emu` die Felder `ng`, `family` und `ng_release`.
- „Datei hinzufügen“ benennt Dateien einer neueren NG-Veröffentlichung (`libSceAmpr.sprx`, `libSceAmpr-debug.sprx`) selbst passend um – die Version steht in der Datei.
- Nach dem Einbauen von NG erscheint eine Meldung, dass in ShadowMountPlus das Aktualisieren der fakelib abgeschaltet sein muss (`update_emulators=0`), sonst ersetzt ShadowMountPlus die Datei beim Spielstart: im Browser als Warnkasten unter „✓ Eingebaut.“, in der Vollbild-App als Meldung, dazu als Meldung der Konsole. Ist `update_emulators=0` schon gesetzt, steht nur ein kurzer Hinweis da. Neu im Stand des Auftrags (`GET /api/v1/library/ampr/job`): `warn` und `warn_level` (2 = etwas zu tun, 1 = nur zur Info).
- Eine NG-Datei gilt wie jede APR-EMU nie als Original des Spiels und zählt bei LZ4-gepackten Spielen als Paketleser.
- Browser und Vollbild-App (Extras → APR-EMU) gleich; Übersetzungen in allen Sprachen.
- Vollbild-App: Die Vorschau-Tour öffnete die Extras eines Spiels nicht mehr (das Aktionen-Menü hatte sich geändert); korrigiert.

### Handbuch und FAQ auf Schwedisch
- Handbuch und FAQ gibt es jetzt auch auf Schwedisch (`web/handbuch.sv.html`, `web/faq.sv.html`, Quelle `tools/i18n/docs/docs_src.sv.json`). Bisher führte der Handbuch-Knopf bei schwedischer Oberfläche ins Leere (`/handbuch.sv.html` fehlte). Alle Fassungen nennen Svenska jetzt in der Sprachleiste und in der Sprachliste des Kapitels „Oberfläche“.
- Schwedische Oberfläche: Vier Knöpfe lasen sich wie Zustandsmeldungen („Packar upp LZ4 …“); jetzt in Befehlsform („Packa upp LZ4 …“, „Packa med LZ4 …“, „Packa upp …“, „Dela upp …“).

## v1.75.4

### Uhrzeiten in der Ortszeit der Konsole
- Die Vollbild-App zeigte Uhrzeiten (Protokoll, Kernel-Log, Dateien, Spielzeit, Sicherungen) in UTC – auf einer Konsole in Mitteleuropa eine Stunde zu früh. Sie kannte nur die Zeitzone, die ein Browser beim Speichern des Nachtmodus mitgeteilt hatte. Jetzt rechnet die App die Ortszeit der Konsole selbst aus (Zeitzone und Sommerzeit aus den Einstellungen der Konsole) und liefert sie als `local_offset_min` in `GET /api/v1/system`; die Vollbild-App nimmt diese.
- Die Systemseite im Browser zeigte den Zeitzonen-Versatz als „+60:00 (3600 min)“ – der Rohwert zählt auf der Pro Sekunden. Jetzt „+01:00 (60 min)“.
- Ob die Sommerzeit richtig dazukommt, ließ sich im November nicht prüfen; die App schreibt die Rohwerte einmal ins Protokoll.

### Kernlast: Hinweis statt „keine Angabe“
- Die Auslastung je Kern wird nur mit der Zusatzabfrage „Risikotelemetrie“ gemessen. Ist sie aus, sagen Vollbild-App und Browser das jetzt, statt nur „keine Angabe“ bzw. „Die Konsole meldet keine Werte“.

## v1.75.3

### Vollbild-App mehrsprachig
- Die Vollbild-App spricht jetzt Deutsch, Englisch, Spanisch, Französisch, Italienisch, Russisch und Schwedisch. Ab Werk folgt sie der Systemsprache der Konsole (andere Sprachen: Englisch); umstellen lässt sie sich mit der Options-Taste → „Sprache / Language“: eine Liste aller Sprachen, die aktive mit Häkchen, ein Druck wählt; die Wahl bleibt gespeichert (`tv_language` in der Konfiguration, Teil von Export/Import).
- Sie nutzt dieselben Wörterbücher wie die Browser-Oberfläche (`/lang/<xx>.json` von der App) und nach denselben Regeln; ihre eigenen rund 800 Texte liegen in der Kachel (`assets/lang/<xx>.json`). Was in keinem Wörterbuch steht, bleibt deutsch. Russisch braucht keinen neuen Zeichensatz – die Schriften enthalten die kyrillischen Buchstaben schon.

### Übersetzungen vervollständigt
- Schwedisch (beigetragen von Daniel Nylander, #3): die seither hinzugekommenen Texte der Browser-Oberfläche ergänzt.
- Englisch, Spanisch, Französisch, Italienisch und Russisch: je rund 200 fehlende Texte ergänzt (zusammen 1.023) – vor allem Meldungen zu Dumpen, PS4 von USB, Aus-/Einhängen über ShadowMountPlus, Spielstart, Lüfterwächter und die Auswahl von Updates und Zusatzinhalten. Dazu „entpackt“, das zur Laufzeit in die Meldungen beim Verschieben/Entpacken eingesetzt wird. Ohne Eintrag bleiben nur Texte, die in jeder Sprache gleich sind (Namen, Einheiten, Code-Bezeichner, englische PS4Mount-Protokollzeilen).

### Kein roter 409 mehr bei ausgeschalteter Laufwerks-Telemetrie
- `GET /api/v1/drives` antwortet bei ausgeschalteter Laufwerks-Telemetrie (Zusatzabfragen) mit 200 und `enabled: false` statt mit 409. Der Browser zeigte sonst bei jedem Diagnose-Schnappschuss und beim ersten Öffnen der Systemseite einen roten Fehler in seiner Konsole. Die Systemseite versteht beide Antworten.

### Dateimanager: überall ändern
- Hochladen, neuer Ordner, Umbenennen, Kopieren, Verschieben und Löschen gehen jetzt überall – auch in Systemordnern wie `/user/download` und im Ordner der App (Wunsch des Users; vorher nur auf den Laufwerken und in `/data`, im App-Ordner nur `payloads`, `AMPR_EMU`, `PlayGo`, `LZ4_Profile`). „Ganzes System“ ist kein reiner Ansehen-Ort mehr.
- Gesperrt bleibt nur, was vor Unfällen schützt: `/` selbst, `/dev`, `/proc`, Verknüpfungen (auch auf dem Weg) und eingehängte Laufwerke als Ganzes; Kopieren und Löschen gehen weiter nie auf ein anderes Laufwerk über, überschrieben wird nie. Schreibgeschützt eingehängte Bereiche lehnen selbst ab.
- Meldungen, Browser, Vollbild-App, Handbuch, FAQ (6 Sprachen) und `docs/HANDBUCH.md` nennen die neue Grenze und warnen vor `/system_data` und `/user`.
- Nachgeschärft (Prüfung an der Pro): Die Ordner direkt unter `/` und direkt unter `/mnt` selbst bleiben gesperrt, auch wenn gerade nichts darin hängt – ein leeres `/mnt/usb2`, `/mnt/ext0` oder `/mnt/disc` ließ sich sonst löschen oder umbenennen, und der nächste Stick, der erweiterte Speicher oder die nächste Disc fand keinen Platz mehr. Ebenso gesperrt: Ordner, unter denen etwas eingehängt ist (`/mnt/sandbox`, die Titel von ShadowMountPlus), und alles auf Gerätedateisystemen (die `/dev`-Ordner in den Sandboxen der Apps). Ihr Inhalt bleibt änderbar.

### Durchsicht 02.11.
- Vollbild-App: Eine Übersetzung wird nicht mehr ein zweites Mal übersetzt, wenn sie selbst ein deutsches Wort ist (Englisch zeigte nach dem Kürzen „Profiles“ statt „Profile“, Schwedisch „Starta“ statt „Start“). Beim Kürzen und Umbrechen laufen die Bruchstücke nicht mehr einzeln durch den Übersetzer (weniger Arbeit je Bild, kein Überlaufen des Zwischenspeichers).
- Vollbild-App: Geschützte und schmale Leerzeichen (Französisch vor jedem Doppelpunkt, rund 830 Stellen) und das Minuszeichen erschienen als „?“; sie werden jetzt als Leerzeichen bzw. „-“ gezeichnet.
- Vollbild-App: Eine im Menü gewählte Sprache lädt auch dann, wenn die Systemangaben der Konsole (`/api/v1/system`) nicht antworten; die Abfrage wiederholt sich dann alle 10 s statt bei jedem Durchlauf.
- Browser: Die Systemseite sagt bei ausgeschalteter Laufwerks-Telemetrie wieder „deaktiviert“ statt „Keine Laufwerke gemeldet“; die Textvorschau im Dateimanager bricht nach 30 s mit „Die Konsole antwortet nicht.“ ab, statt ewig zu laden.

### Aufräumen nach der alten Kachel, PS4Mount-Prüfungen als Skript
- Sprachdateien: 12 Übersetzungen der früheren Kachel-Oberfläche („Kachel installieren“, „Kachel-Paket auswählen“ …) und des entfernten Start-Versuchs gelöscht; sie wurden nirgends mehr angezeigt. Die Antwort auf ein hochgeladenes Kachel-Paket nennt statt des verschwundenen Knopfs `POST /api/v1/tile/ensure`.
- `test/ps4m/run.sh`: führt die Prüfungen des PS4Mount-Ports (`third_party/ps4mount/test/unit.c`, 157 Prüfungen) unter WSL/Linux aus; bisher nur über einen Befehl von Hand. Beschrieben in `docs/ENTWICKLUNG.md`.

## v1.75.2

### Kein Kachel-Versuch mehr beim Start
- Die App versucht beim Start nicht mehr, die frühere Medien-Kachel (PSCC69690) anzulegen. Bis 1.75.1 tat sie das noch einmal je Version – entgegen Handbuch und LIESMICH; auf Konsolen, wo es gelang, kam die alte Kachel zurück, und der Versuch hielt die Lüfterregelung bis zu 20 s auf. `POST /api/v1/tile/ensure` legt sie auf Wunsch weiter an.
- Entfernt: `ps5tm_tile_bootstrap()` und die Notiz `tile-auto` (wird nicht mehr gelesen oder geschrieben).

## v1.75.1

### Handbuch und FAQ auf dem Stand von 1.75
- Neues Kapitel „Die Vollbild-App“ (Tasten, Bereiche, was im Browser bleibt) in allen sechs Sprachen.
- Installation: Abschnitt „Die Kacheln“ – FAKE86921 und FAKE86922 starten die App selbst; die frühere Medien-Kachel (PSCC69690) wird nicht mehr gebraucht.
- Kühlung: Abschnitt „Lüfterwächter“. Profil: eigene Bilder in der Vollbild-App (Ordner `Profilbilder`, USB). Dateimanager: Laufwerke heißen `usb0`, `usb1` …
- FAQ: Neustart/Kachel und „leere Seite“ berichtigt, Wächter ergänzt; neu: Seitenleiste mit dem Touchpad, Ablageort eigener Profilbilder, warum „usb0“.
- `docs/HANDBUCH.md`: Abschnitte Vollbild-App, Lüfterwächter, eigene Profilbilder und USB-Namen; die alte Homescreen-Kachel als veraltet gekennzeichnet. `docs/API.md`: `tile/ensure` beschrieben als frühere Kachel.

## v1.75.0

### Vollbild-App: Seitenleiste nur mit dem Touchpad
- Links auf dem Steuerkreuz öffnet die Seitenleiste nicht mehr (vorher auf jeder Seite außer Kühlung); sie öffnet nur noch die Touchpad-Taste. Links gehört damit überall der Seite.
- Profil: eigene Bilder (JPG, PNG bis 16 MB) aus `/data/PS5-Cooling-Center/Profilbilder` (legt die App beim Start an, getrennt von ihrem Zwischenordner `avatar`), dem Hauptverzeichnis eines USB-Laufwerks oder dessen Ordner `Profilbilder` stehen vorn im Raster („EIGENES“) und werden mit der X-Taste wie die mitgelieferten übernommen.

### USB-Laufwerke heißen wie auf der Konsole
- Statt „USB-Speicher 1“ für `/mnt/usb0` heißen die Laufwerke jetzt `usb0`, `usb1` … – überall (System, Dateimanager, Payloads, Pakete, Auswerfen, Browser und Vollbild). Hinweis eines Nutzers: die Zählung ab 1 passte nicht zu den Pfaden der Konsole.
- Vollbild, Profil: die Profilbilder im Raster werden in voller Größe geladen (256 px statt der 132-px-Vorschau) und sind scharf; auf der Profil-Seite kommen ihre Bilder zuerst und bis zu vier auf einmal. Links bleibt im Raster (die Seitenleiste öffnet das Touchpad). Behoben: Bilder und Upload scheiterten am kodierten Punkt im Dateinamen („Das Bild ließ sich nicht laden“, „Unbekannter Dateiname“).

### Vollbild-App: Phasen 8–10 und „Aufteilen“
- **Kühlung → Einstellungen (Phase 8):** neue Abschnitte Warnungen (Warnung ab, Warn-Countdown), Lichtleiste (Ampel an/aus, Gelb ab, Rot ab; Rot bleibt über Gelb) und Spielprofile („„Spiel“ merken“ für das laufende Spiel, Liste der Profile mit „aktiv“, die X-Taste entfernt eines sofort).
- **Payloads (Phase 9):** neuer Reiter „Profile“: ausführen/anhalten, beim Start der App ausführen, löschen (Dreieck-Taste), bearbeiten und neu anlegen (Quadrat-Taste; Name über die Bildschirmtastatur, Payloads aus dem internen Ordner, Pausen 1/2/5/10 s, Einträge ordnen und entfernen). Unter „Meine Payloads“ löscht die Quadrat-Taste eine interne Datei bzw. kopiert eine vom USB-Stick in den internen Ordner.
- **Protokoll (Phase 10):** neuer Reiter „Gespeichert“: Auszug des Kernel-Logs auf der Konsole speichern, die gespeicherten Aufnahmen und Auszüge, Ordner leeren; die Dreieck-Taste startet/beendet die Aufnahme auch hier.
- **Profil (Phase 10):** eigene Seite statt Platzhalter: Bild, Name, Konsole, Benutzer-ID; die 30 mitgelieferten Profilbilder als Raster, die X-Taste macht eines zum Profilbild (die Vollbild-App rechnet es wie die Browser-App in DXT5-DDS und PNG um, `tvapp/src/ps5cc/avatar_build.*`), die Quadrat-Taste holt das vorherige zurück, die Dreieck-Taste ändert den Namen. Reiter „Danke“ mit allen Einträgen der Credits-Seite (`credits.hpp`, aus `web/index.html` erzeugt).
- **Pakete:** „Aufteilen …“ im Dreieck-Menü: erst das Ziel-Laufwerk (ohne genug Platz gesperrt), dann die Teilgröße (FAT32-Stick 4 GB, Blu-ray 23/46 GB, 1–16 GB, mit der Zahl der Teile); Fortschritt unten in der Karte, „Aufteilen abbrechen“ im Menü.
- Bleibt in der Browser-App: eigenes Profilbild hochladen, Profile und Spielprofile als Datei sichern/laden, Dateien herunterladen, Links der Danke-Seite.

### Offene Punkte der Durchsicht vom 31.10. behoben
- **Lüfter:** Eine feste Schwelle von Hand wird abgelehnt, solange der Lüfterwächter die Kühlung hält (409, vorher überschrieb sie seine volle Kühlung). Während der Wächter hält, laufen die Messwerte im Status weiter, statt stehen zu bleiben.
- **Verlauf:** `history.csv` schreibt ein eigener Faden; der Lüfter-Faden wartet nie mehr auf ein belegtes Laufwerk.
- **Absturz:** Jeder Faden der App bekommt einen eigenen kleinen Signal-Stack (`ps5tm_thread_create`, `SA_ONSTACK`), damit die Lüfterrückgabe an die Konsole auch nach einem Stack-Überlauf läuft.
- **Startmenü-Kachel:** Die Sperre der Systemschnittstelle bleibt gehalten, bis die Rechte zurückgestellt sind; vorher liefen andere Aufrufe bis zu 30 s unter der ShellCore-Identität.
- **Webserver:** Antworten 403/503 (fremde Herkunft, zu viele Verbindungen, kein Speicher, kein Faden) werden sauber beendet statt mit einem Verbindungsabbruch, der die Antwort verschluckte.
- **Spielstand zurückspielen:** Dateien des Titels, die es in der Sicherung nicht gibt, werden danach entfernt (sie liegen in der Sicherung vor dem Zurückspielen); vorher sah das Spiel eine Mischung.
- **Löschen:** Die Bestätigung gilt nur noch für genau die Daten, die beim Planen dort lagen (Gerät und Inode), in Bibliothek und Dateimanager; ausgetauschte Ordner werden abgelehnt.
- **Dateimanager:** Neuer Ordner und Umbenennen sind gesperrt, solange er kopiert, verschiebt oder löscht (Umbenennen auch während anderer Aufträge der App). Umbenennen einer Datei überschreibt keinen inzwischen entstandenen gleichnamigen mehr. Ein abgebrochenes Kopieren räumt seinen Zielordner auch nach einem „existiert schon“ tief darin wieder ab.
- **Vollbild-App:** Der QR-Code unter System folgt einer neuen IP-Adresse (etwa jede Minute neu geholt).

### Vollbild-App: `make lint` im Projektordner
- `tools/setup-native-dependencies.sh` baut zlib in einem Ordner ohne Leerzeichen (zlibs Makefile verträgt keine im Zielpfad); `tools/fetch-opengl-sdk.sh` lädt ps5-opengl 1.0.2 von der erhaltenen Kopie (JMUtechnologies), weil das Original-Repository fehlt – Prüfsumme unverändert. `make lint` läuft damit im Projektordner vollständig durch.

### Vollbild-App: laufendes Spiel im Aktionen-Menü beenden
- Spiele → Bibliothek → Quadrat-Taste: Beim laufenden Spiel steht oben „Spiel beenden“. Es beendet das Spiel sofort, ohne Rückfrage, wie „Spiel beenden“ in der Browser-App; nicht Gespeichertes geht verloren.

### Vollbild-App: System (Phase 7), Startmenü-Kachel entfernt, Durchsicht 31.10.
- **System** in fünf Unterreitern (L2/R2): Konsole, Speicher, Kühlleistung, Zusatzabfragen, Experte – wie die Systemseite im Browser (`tvapp/src/ps5cc/system_page.cpp`). Fühler-Schalter speichern sofort, reagieren erst mit geladenen Einstellungen, ein gewünschter Wert verfällt nach 12 s; fehlende Anteile zeigen „–“; Laufwerke ohne Temperatur sagen das, der Grund steht darunter. Die Zeile „CPU-Modus“ entfällt.
- **Startmenü-Kachel** (Abschnitt und „Kachel installieren“) aus beiden Oberflächen entfernt: seit FAKE86921/FAKE86922 nicht mehr nötig. Handbuch-Tipp in allen Sprachen angepasst; eine gelöschte alte Kachel wird nicht neu angelegt.
- R2 steht direkt hinter dem letzten Unterreiter statt am rechten Rand.
- Listen behalten beim Aktualisieren die Auswahl (`refocus`); die Spieleliste wird nur bei echter Änderung neu gefüllt.
- Datenabfrage: Ein Fehler beendet die Runde statt sie zu blockieren; die Laufwerksliste wird nicht mit leeren Antworten überschrieben.
- Durchsicht App-Kern: Dump ohne „Überschreiben“ löscht keinen fertigen Dump mehr; Spiel löschen erkennt ein Laufwerk direkt unter dem Startordner; Verschieben bleibt nach wiederholter Ablehnung „beschäftigt“; Kopieren bricht ab, wenn die Unfertig-Marke fehlt; das automatische Ziel wird begrenzt; der Lüfterwächter liest nur noch Temperaturen (`ps5tm_platform_read_temps`).
- Durchsicht Browser: Disc-Reiter friert nach Reiter- oder Fensterwechsel nicht mehr ein; Löschdialog schreibt nach dem Schließen nicht in einen anderen Dialog und lädt die Liste nach; USB-Auswerfen gibt nach etwa 30 s ohne Antwort auf; Wächter-Meldungen, Kopier-Schalter „PS4 von USB“, „von/bis“ im Nachtmodus übersetzt; Dateinamen in der Ansicht werden nicht übersetzt; toter Code der alten Kacheln entfernt.
- Vollbild-App mit clang-format formatiert; `make lint` kennt die ps5cc-Dateiköpfe, `build/gen` für clang-tidy und prüft Leerzeichen nur in `tvapp/`.

### Dateimanager-Name, Seitenleiste deckend
- Browser: Die Seite des Dateimanagers heißt in der Überschrift „Dateien & Ordner“ (der Reiter bleibt „Dateimanager“).
- Vollbild: Die aufgeklappte Seitenleiste deckt die Seite dahinter ganz ab; zugeklappt bleibt sie durchsichtig wie die anderen Flächen.

### Einzelnes Spiel über ShadowMountPlus aus- und einhängen
- Browser (Spielkarte) und Vollbild (Spiele → Quadrat → Aktionen): eingehängt „Neu einhängen“ und „Aushängen“, ausgehängt „Einhängen“. Hilft, wenn ein Spiel hängt oder nach Kopieren, Verschieben oder Löschen neu erkannt werden soll. Ohne Rückfrage; die Meldung sagt, was geschah.
- Nur für Ordner und Abbilder, die ShadowMountPlus verwaltet (nicht für installierte Pakete), ab SMP 1.7beta10 (`mount_game`/`unmount_game`). Gesperrt, solange das Spiel läuft oder ein Auftrag der App läuft.
- Neue Route `POST /api/v1/library/remount` (`src/smpremount.c`); die Spielliste meldet `can_remount`.

### Vollbild-App: Ziel der Spielstand-Sicherung als Liste, Covers unter System
- Spielstände → Dreieck → „Ziel wählen …“ klappt eine Liste aller Speicherorte mit freiem Platz auf; der gewählte ist markiert. Vorher wechselte jeder Druck das Ziel, und man sah nicht, ob schon gesichert wurde.
- „Covers & Metadaten speichern“ und „Covers-Ordner leeren“ stehen jetzt unter System → Speicher statt im Fenster „Bibliothek: Doppelte Abbilder“.
- Kernel-Log: „Aufnahme läuft …“ beginnt rechts neben der R2-Marke und überdeckt sie nicht mehr.
- Neue Symbole in der Seitenleiste: Lüfter (Kühlung), zwei Ordner (Dateien), Rakete (Payloads), Zahnrad (System), Person im Kreis (Profil), Klemmbrett (Protokoll); der Controller (Spiele) bleibt.
- System ist in Abschnitte geteilt: Laufwerke, Covers & Metadaten, Hintergrundbilder. Die Hintergrundbilder der Bereiche sind von Kühlung → Einstellungen hierher gezogen und aufklappbar; die X-Taste wechselt je Bereich zwischen Standard, Eigenes Bild und Aus.
- Dateimanager aufgeräumt: Pfadleiste mit Ordnerkette, Anzahl und Sortierung; Symbole je Zeile (Ordner, Datei, Verknüpfung, Laufwerk, „Übergeordneter Ordner“ mit Pfeil); ausgewählte Einträge mit Häkchen statt Marke; rechts großes Symbol mit Pfad, Auswahl und Ablage als kleine Karten. Die Seite beginnt auf Höhe der Seitenleiste. L2/R2 blättern 8 Zeilen, gehalten weiter. Der Bereich heißt jetzt „Dateimanager“. Die O-Taste führt zurück, wo man vor dem Öffnen des Ordners war (ein Ort aus „Orte“ zurück zu „Orte“), der Fokus steht auf dem verlassenen Ordner; ein offenes Fenster oder Menü schließt O wie bisher zuerst.
- Abschnittstitel in den Listen (System, Spielstände, Pakete, Spielzeit, Payloads, Disc) haben eine Linie wie in den Einstellungen.
- Payloads (Browser und Vollbild): Dieselbe Fassung zweimal (intern und auf USB, anders geschrieben, z. B. „ShadowMountPlus_1.7beta10“ und „shadowmountplus_v1.7_Beta10“) zählt als eine Version – der Lade-Punkt leuchtet dann.

### Vollbild-App: Spiel-Extras (Phase 6), Fenster deckend, Payloads „Bereits geladen“
- **Extras je Spiel** (Bibliothek → Quadrat-Taste → „Extras: AMPR, PlayGo, LZ4 …“, nur für Spiele mit eigenem Ordner oder Abbild): AMPR-Index bauen, APR-EMU und PlayGo (Version wählen, mitgelieferte in den Ordner legen, Original wiederherstellen, entfernen), Plugins aktualisieren/prüfen, LZ4 packen (Stufe, Profil, im Ordner oder als Kopie, mit Plan und Pflicht-Häkchen „ungepackt getestet“), prüfen, entpacken, Optimierung per Mitschnitt (starten, einsammeln, Profil erzeugen, beenden); je Bereich der Zustand, rechts Erklärung und laufender Auftrag (`tvapp/src/ps5cc/game_extras.*`)
- **Bibliothek: Doppelte & Covers**: doppelte Abbilder deaktivieren/aktivieren, Covers & Metadaten speichern an/aus, Ordner leeren
- **Protokoll → Kernel-Log**: Dreieck startet/beendet die Aufnahme in eine Datei (Herunterladen im Browser)
- Seitenflächen durchsichtiger (62 %/76 % statt 78 %/88 %); Fenster, Menüs, Details und Meldungen decken jetzt ganz ab, nichts von der Seite dahinter scheint durch (`solid_theme`, `solid_floor` in `look.hpp`)
- Payloads: „Laufend“ heißt „Bereits geladen“ (beide Oberflächen), mit Überschrift. Jeder Payload trägt einen Punkt: grün leuchtend, wenn er läuft, grau, wenn nicht; nicht heruntergeladene Store-Einträge haben keinen. Die App merkt sich, aus welcher Datei sie einen Payload gestartet hat (`/data/PS5-Cooling-Center/payload-starts.json`, Feld `file` in `GET /api/v1/payloads`), damit bei zwei Fassungen nur die gestartete leuchtet. Im Store heißt „geladen“ jetzt „gespeichert“

### Vollbild-App: Dateien (Phase 5) und bündige Marken
- Der Bereich **Dateien** ist jetzt der Dateimanager (wie im Browser, `src/filemgr.c`): Orte (Laufwerke, Daten der Konsole, ganzes System nur ansehen), Ordner öffnen, „..“ hoch; Quadrat-Taste: Auswählen, Ansehen (Text), Größe messen, Kopieren, Ausschneiden, Umbenennen, Löschen (sofort); Mehr: Hier einfügen, Ablage leeren, Neuer Ordner, alle/keine auswählen, sortieren nach Name/Größe/Datum, Auftrag abbrechen. Namen über die Bildschirmtastatur mit eigener Seite äöüß. Ändern nur auf den Laufwerken und in /data (sonst „Nur ansehen“). Hochladen, Herunterladen und die Bildvorschau bleiben im Browser
- Listen: Die Marken (BEREIT, AUSGEWÄHLT …) verrutschten mit der Länge des Werts daneben. Neue Kit-Einstellungen `value_width` (feste Spalte für den Wert) und `badge_column` (eine Spalte für die Marken, jede darin zentriert), in allen zehn Listen mit Marken eingeschaltet

### Vollbild-App: Spielzeit, Spielstände, Sicherungen (Phase 4)
- Spiele hat jetzt die Unterreiter der Browser-App in derselben Reihenfolge: Bibliothek · Spielzeit · Spielstände · Pakete · PS4 von USB · (Disc) · Sicherungen · Aufträge (je so breit wie das Wort)
- **Spielzeit:** laufendes Spiel, meistgespielt (Anteil, Schnitt, zuletzt, höchste Temperatur), letzte Sitzungen (X-Taste: mehr); rechts Heute / 7 / 30 Tage / Insgesamt und die letzten 14 Tage als Balken; Mehr → Verlauf löschen. Gerechnet wie im Browser (eine Sitzung über Mitternacht zählt anteilig an beiden Tagen), `tvapp/src/ps5cc/playtime_rules.*` mit Tests. Die CSV-Ausfuhr bleibt im Browser
- **Spielstände:** je Benutzer, X-Taste wählt für die nächste Sicherung, Mehr → Ausgewählte sichern / Ziel / alle / keine / abbrechen; Quadrat-Taste → Löschen (vorher gesichert); bei einer Sicherung Prüfen und je Spiel Zurückspielen (nur dieselbe Konsole, der Stand davor wird gesichert); Fortschritt rechts
- **Sicherungen:** die Spiel-Sicherungen der App mit UNFERTIG / MIT PRÜFSUMME; Quadrat-Taste → löschen (sofort, läuft unter Aufträge)

### USB-Laufwerk sicher auswerfen
- Neu, auf Wunsch des Users: In der Browser-App unter System → Speicher hat jedes USB-Laufwerk einen Knopf **Auswerfen**, in der Vollbild-App die neue Seite **System → Speicher** (Laufwerke mit Belegung; X-Taste wirft aus). Ausgeworfen wird über ShadowMountPlus 1.7beta10 (`POST /api/v1/storage/eject` dort), aber nur, wenn **kein Auftrag der App läuft** und PS4Mount keine Spiele von dem Laufwerk spielt – sonst lehnt die App ab und nennt den Auftrag. Neue Routen `POST`/`GET /api/v1/storage/eject`, Feld `ejectable` in `/api/v1/system` (`src/usbeject.c`)
- Grenze: Das Auswerfen auf der eigenen Seite von ShadowMountPlus lässt sich nicht aufhalten. Laut seinem Quelltext (`sm_api_service.c`, `run_usb_eject_job`) prüft es nur seine eigenen Einbindungen, Abbilder und das laufende Spiel; eine Sperre ließe sich nur durch eine gefälschte Spiel-Einbindung samt `mount.lnk` erzwingen, und das macht die App nicht. Das Handbuch sagt es

### Fremdbausteine
- **PS4Mount 0.7.1 → 0.7.2** (gasb07): Ausschalten hängt vorher jedes Spiel aus (nie erzwungen), damit der Ruhemodus kein eingehängtes Spiel vom abgeschalteten Laufwerk findet; läuft ein Spiel, lehnt die App ab und nennt es; bleibt ein Spielordner belegt, hängt sie alle wieder ein und lehnt ab (`unmount_games` in `src/ps4m_core.c`). Einschalten hängt die Spiele wieder ein. Handbuch (6 Sprachen) und Meldungen übersetzt
- **ShadowMountPlus 1.7beta10 → 1.7beta11** (drakmor): keine Änderung an den Schnittstellen der App (Aus-/Einhängen, Auswerfen, Fähigkeiten, `config.ini`). Übernommen die bessere Paket-Erkennung: Inhaltstyp 0x1B/0x1C/0x21 = Zusatzinhalt, ein PS5-Medien-Remaster (Typ 0x26, Flags 0x40400000) ist ein Grundpaket statt eines Updates (`src/pkgparse.c`). Neu bei SMP, von der App nicht benutzt: gemeinsame Installations-Warteschlange mit Covern, Restzeit, json-c-Speicherlecks behoben
- **ps5upload 9.0.1 → 10.0.3** (nur Wissen): Die App lauscht nach dem Ruhemodus schon von selbst wieder (`listener_refused` in `src/http.c`); sonst nichts für Start oder Sicherheit Relevantes
- **PS4Mount 0.7.0 → 0.7.1** (gasb07): Spiele, deren param.sfo fehlerhafte Kennungen hat (TITLE_ID, CONTENT_ID, APP_VER, VERSION oder irgendein `SERVICE_ID_ADDCONT_ADD_*`, jetzt alle statt nur der ersten sieben), werden nie angemeldet – so ein Paket hat bei gasb07 die Konsole ausgeschaltet und ihren Speicher beschädigt; ein beschädigtes Update bleibt liegen („Update ignoriert: Das Update-Paket ist beschädigt …“). `p4m_sfo_check` in `src/ps4m_pkg.c`, Aufruf in `src_open` (`src/ps4m_core.c`). Das beschädigte Spiel steht in der App unter „Auf USB gefunden, nicht eingebunden“ (Grund „beschädigt“); 7 neue Prüfungen in `third_party/ps4mount/test/unit.c` (157, alle grün)
- **ShadowMountPlus 1.7beta9 → 1.7beta10** (drakmor): keine Änderung an der Schnittstelle, die die App benutzt (`config.ini` unverändert). Ab 1.7beta10 sperrt ein laufendes Spiel nur noch seinen eigenen Titel und seine Quellen (`game_active`), auch im Vordergrund; Kommentare und Handbuch sagen das jetzt („bis 1.7beta9“). Neu bei SMP, von der App nicht benutzt: USB-Laufwerk auswerfen (`POST /api/v1/storage/eject`), SMB-Gastzugang und Nur-Lesen-Modus; das Zwangsbeenden von FPKG-Spielen beim Ruhemodus ist weg
- **ps5upload 7.1.10 → 9.0.1** (nur Wissen): nichts für Start oder Sicherheit der App Relevantes

### PS4 von USB: übersprungene PKGs mit Grund, verständlichere Knöpfe
- Vollbild-App: Die Schrift hat keine Zeichen für die Tasten X, Dreieck und Quadrat im Fließtext (sie erschienen als „?“). Die Hinweise sagen jetzt „X-Taste …“, „Quadrat-Taste …“ oder „Mehr → …“ (Disc, Pakete, PS4 von USB)
- Bibliothek: Ein Spiel, das PS4Mount vom USB-Laufwerk spielt, heißt jetzt „PS4Mount“ (Vollbild: „PS4Mount (USB)“; Format: „PS4Mount: läuft vom USB-Laufwerk, nicht installiert“) statt „Installiert“ – für die Konsole ist es ein installiertes Paket, darum sah es aus wie eins vom Paket-Installer. Neues Feld `ps4mount` in `GET /api/v1/library` (`library.c`, über `ps4m_title_managed`). Die Vollbild-App zeigte installierte Pakete bisher als „pkg“, jetzt als „Installiert“
- Unter den Spielen steht jetzt „Auf USB gefunden, nicht eingebunden“: jede PKG, die die Suche liegen lässt, mit dem Grund – schon auf der Konsole installiert, mit „Nicht mehr von USB spielen“ herausgenommen, kein PS4-Paket (etwa ein PS5-PKG), kein Fake-PKG, Zusatzinhalt ohne PS4-Spiel oder bei ausgeschalteten Zusatzinhalten, zweite Kopie. Vorher blieb die Liste leer, obwohl PKGs auf dem Laufwerk lagen (an der Pro: fünf PS4-Spiele, alle schon intern installiert). In der Browser-App und in der Vollbild-App; neues Feld `skipped` in `GET /api/v1/ps4mount` (`src/ps4m_core.c`, `skip_note`). Die Liste ist zugeklappt und öffnet sich auf Klick (Vollbild: X-Taste auf der Zeile)
- Neue Namen (Wunsch des Users): „Aus Bibliothek entfernen“ → „Kachel entfernen“, „Hinzufügen“ → „Kachel wieder anlegen“, „Zurückverschieben“ → „Nicht mehr von USB spielen“ (auch „Ausgewählte …“ / „Alle …“), „Zurückverschobene wieder aufnehmen“ → „Herausgenommene wieder zulassen“; Zustand „Kachel entfernt“; ein Spiel, das im Ordner `ps4/` liegt, aber fest installiert ist, zeigt „INTERN“ und „Auf der Konsole installiert, läuft nicht von USB“ (vorher „INSTALLIERT“, das wie eingebunden klang). Übersetzungen in allen Sprachen, Handbuch angepasst

### „Als Abbild bauen“ aus einem PS4-Dump entfernt
- Die Funktion, aus einem PS4-Dump der Liste „Dumps aus dem Disc-Bereich“ ein exFAT-, ffpfsc- oder ffpkg-Abbild zu bauen, ist ganz aus der App genommen (Wunsch des Users, 29.10.2026): der Knopf in der Browser-App mit seinem Planfenster, die Routen `POST /api/v1/dumps/image/plan` und `/api/v1/dumps/image` und der Code dahinter (`ps5tm_gameconvert_dump_plan/_start`, `ps5tm_dumplib_source`). Das Feld `dump_image` im Konvertier-Auftrag fällt weg
- Geblieben ist `src/dump_image.c`: Das Konvertieren von PS4-Spielordnern legt damit weiter die `param.json` aus der `param.sfo` (und bei Bedarf eine korrigierte `npbind.dat`) mit ins Abbild. Der Hinweis auf Trophäen und „Bitte warten“ steht nach dem Konvertieren jetzt bei jedem PS4-Spiel

### Vollbild-App: Spiele → Disc (Phase 3, Teil 2)
- Neuer Unterreiter „Disc“ zwischen „PS4 von USB“ und „Aufträge“ – nur auf Konsolen mit Laufwerk, wie in der Browser-App (`has_drive`; die App fragt das außerhalb der Seite alle 30 s). Eine Liste mit Abschnitten: Laufwerk (✕ Abbild der PS2-DVD/PS3-Disc erstellen oder abbrechen, ausgeschaltet: einschalten), Disc-Spiele dumpen (✕ startet das Spiel, läuft es, dumpt es – liegt schon ein fertiger Dump da, ersetzt es ihn; □ in die Warteschlange), Warteschlange (□ entfernen), Dumps aus dem Disc-Bereich (□ löschen) und Abbilder (□ löschen)
- Rechts die Angaben zum Eintrag, darunter der laufende Dump oder das Abbild mit Fortschritt, Tempo und Restzeit, und das Ziel der Dumps
- △: Laufwerk ein/aus, neue Disc automatisch sichern, Warteschlange starten/stoppen/überspringen/leeren, Dump stoppen, Dump-Einstellungen, Dumps neu suchen (auch mit Nachmessen der Größen), Ereignisse am Laufwerk – alles ohne Rückfrage
- Dump-Einstellungen im Vollbild: Ziel, Spielname im Ordnernamen, DLC, Prüfen, PS4 Spiel/Update, Entschlüsseln, FSELF, Backport mit PS4-/PS5-Ziel, dazu „Vorhandene Dumps“ und die Ladezeit der Warteschlange; gespeichert wird sofort. Der Ordner auf dem Ziel und eigene Einstellungen je Spiel der Warteschlange bleiben in der Browser-App

### Vollbild-App: Spiele → PS4 von USB (Phase 3, Teil 1)
- Neuer Unterreiter „PS4 von USB“ (Bibliothek · Pakete · PS4 von USB · Aufträge), wie der Reiter der Browser-App: die PS4Mount-Spiele mit Bild, Titel-ID, Version, Update, Zusatzinhalten, Größe und Zustand (BEREIT, KOMMT, ENTFERNT, INTERN, DLC); rechts die Angaben zum Spiel und der Stand von PS4Mount
- ✕ nimmt ein Spiel in die Bibliothek auf oder heraus; □ öffnet, was das Spiel erlaubt: Updates wählen, Zusatzinhalte an/aus (mit „Alle an“ bis zum Höchstwert), PKGs zurückverschieben; △: PS4Mount ein-/ausschalten, Jetzt suchen, Einstellungen, Protokoll, Zurückverschobene wieder aufnehmen, Alle zurückverschieben – alles ohne Rückfrage
- Einstellungen im Vollbild: Zusatzinhalte einbinden/kopieren, Gruppen und Gruppengröße, schneller anmelden, Gruppengröße beim Zurückverschieben; gespeichert wird sofort und immer als Ganzes (wie die Browser-App). Die Ordner mit PKG-Dateien werden angezeigt, geändert werden sie in der Browser-App

### Vollbild-App: Spiele → Pakete (Phase 2)
- Neuer Unterreiter „Pakete“ zwischen Bibliothek und Aufträge (L2/R2), wie der Reiter Pakete der Browser-App: die gefundenen Pakete nach Laufwerk, je mit Bild (oder Marke P5/P4/UP/DLC), Art, Version, Plattform, Titel-ID und Größe; Marken für die Warteschlange (WARTET, LÄUFT, FERTIG, FEHLER) und für geteilte Pakete mit fehlenden Teilen
- ✕ installiert das Paket sofort (lehnt die Konsole ab, sagt ein Hinweis warum), □ legt es in die Warteschlange oder nimmt es heraus, △ öffnet „Neu suchen“, „Warteschlange starten/anhalten/fortsetzen“, „Installation abbrechen“, „Fehlgeschlagene wiederholen“ und „Erledigte entfernen“ – alles ohne Rückfrage
- Rechts die Angaben zum Paket, darunter der Stand der Warteschlange und die laufende Installation mit Fortschritt und Restzeit. Beim ersten Öffnen ohne frühere Suche sucht die Seite selbst
- Aufteilen und Senden vom PC bleiben in der Browser-App

### Vollbild-App: Bilder hinter den Bereichen, auch eigene
- Jeder Bereich der Vollbild-App (Kühlung, Spiele, Dateien, Payloads, System, Profil, Protokoll) hat ein eigenes Hintergrundbild, links, oben und unten abgedunkelt, damit die Schrift lesbar bleibt; die Flächen lassen etwas davon durchscheinen. Die sieben Bilder (1280 × 720, je etwa 70–110 KB) liegen nur in der Vollbild-Kachel (`tvapp/assets/backdrops`), nicht in der App-ELF
- Eigene Bilder: In der Browser-App unter System → „Hintergründe der Vollbild-App“ je Bereich „Bild hochladen“ (die Seite schneidet auf 1280 × 720 zu, Mitte, und schickt ein JPEG), dazu die Wahl Standardbild / Eigenes Bild / Kein Bild und „Löschen“. Die Konsole legt die Bilder in `/data/PS5-Cooling-Center/backdrops` ab (`src/backdrops.c`, Routen `/api/v1/backdrops*`); die laufende Vollbild-App fragt alle 5 s nach und zeigt ein neues Bild ohne Neustart
- In der Vollbild-App dieselbe Wahl unter Kühlung → Einstellungen → „Hintergründe“ (◀ ▶ je Bereich, sofort wirksam). Ohne hochgeladenes Bild springt die Wahl über „Eigenes Bild“ hinweg
- Kit: `GlBatch::replace_texture()` gibt einer Textur neue Pixel, damit ein ausgetauschtes Bild keinen Grafikspeicher verliert

### Spiele: leuchtende Linie um das laufende Spiel wieder sichtbar
- Die grüne Linie und das Schild „läuft“ hingen daran, dass das Spiel im Vordergrund steht – im Browser der Konsole steht es nie vorn, solange die Seite offen ist. Sie erscheinen jetzt, sobald das Spiel läuft, wie „Spiel beenden“

### Reiter Disc nur mit Laufwerk
- Hat die Konsole kein Laufwerk, blendet die Spiele-Seite den Reiter „Disc“ aus. `GET /api/v1/disc` meldet dafür `has_drive`: die App sucht nur das Durchreichgerät von `cd0` (kein Befehl an ein Laufwerk), auch bei ausgeschalteter Erkennung, höchstens alle 30 s neu – eine später angesteckte Pro-Laufwerkseinheit erscheint beim nächsten Öffnen der Spiele-Seite

### Dump-Warteschlange nur für Disc-Spiele
- „Weiteres Spiel wählen“ bot jedes installierte PKG-Spiel an, und jedes Spiel ließ sich in der Liste zwischen DISC und PKG umschalten; ein Umschalten auf DISC merkte sich die App dauerhaft (`disc_titles.txt`), so trug etwa Tetris (ein installiertes PKG) die Marke „DISC“. Jetzt nimmt die Warteschlange nur Spiele, die laut Konsole von Disc installiert sind – wie die Liste „Disc-Spiele dumpen“ darüber; die Auswahl zeigt nur diese, der Schalter DISC/PKG ist weg, und was die Seite behauptet, merkt sich die App nicht mehr

### Spiel-Updates entfernt
- Die Funktion „Spiel-Updates“ (Reiter „Updates“ unter Spiele, dazu „Versionen“, Diagnose, Neuinstallation und PS4-Downgrade) ist wieder aus der App genommen (Wunsch des Users, 29.10.2026). Entfernt: `src/updates.c`, `src/upd_sony.c`, `src/upd_patchsite.c`, die Routen `/api/v1/updates*`, die Paketlisten-Installation (`upd-<Titel-ID>`) und die nur dafür gebauten Befehle des Installationshelfers (nur das Spiel / nur das Update deinstallieren, ShellCore-Kennung; Protokoll `PKGI_VERSION` 6). Ebenso der eben gebaute Unterreiter „Updates“ der Vollbild-App
- PatchDL wird nicht mehr als Fremdbaustein verfolgt. Geblieben ist nur, was der Payload-Store braucht: die eigene Namensauflösung (`upd_dns.c`) und der HTTPS-Abruf (`upd_http.c`) mit den Stammzertifikaten; dafür bleibt der Dank an PatchDL in den Credits und in `THIRD_PARTY_NOTICES.md`
- Internet braucht die App damit nur noch für „Payloads herunterladen“ (FAQ und README angepasst). Der Ordner `/data/PS5-Cooling-Center/updates` auf der Konsole wird nicht mehr benutzt; die App löscht ihn nicht selbst

### Trophäen bei PS4-Spielen aus Abbildern: klare Auskunft statt Versuch
- Ein Versuch vom 28.10.2026, die Trophäendateien eines PS4-Spiels aus einem Abbild selbst abzulegen, ist wieder ausgebaut: An der Konsole (Tetris Ultimate als `.ffpkg`, 29.10.2026) kam trotzdem „Trophy registration failed (0x80551618)“, obwohl der Trophäen-Ordner `/user/trophy/conf/NPWR06412_00` mit `TROPHY.TRP` vorhanden war, und das Spiel blieb bei „Bitte warten“ stehen. Das deckt sich mit den Messungen im PS5 Dump & Image Converter (22.08.2026): Die alte PS4-Trophäenkette verlangt ein echt installiertes Paket, Nachlegen von Dateien ändert nichts; dasselbe Paket installiert läuft und registriert seine Trophäen
- Die Hinweise beim Konvertieren eines PS4-Spiels (Browser, Vollbild, Handbuch, 6 Sprachen) sagen das jetzt so und nennen den Weg: das Paket im Reiter „Pakete“ installieren

### Pakete: Installieren mit einem Klick
- „Installieren“ an einem Paket startet die Installation sofort; vorher öffnete der erste Klick einen Plan mit einem zweiten Knopf „Jetzt installieren“. Lehnt die Konsole ab, öffnet sich der Plan und zeigt, was im Weg ist

### Vollbild-App: Spiele → Updates (Phase 2, Teil 1)
- Neuer Unterreiter „Updates“ zwischen Bibliothek und Aufträge (L2/R2), wie der Reiter Updates der Browser-App: oben „Nach Updates suchen“ und „Alle aktualisieren“ (bzw. „Spiel-Updates einschalten“), darunter jedes Spiel mit Version → angebotener Version und Zustand. ✕ macht den nächsten Schritt (Laden und installieren, Fortsetzen, Installieren, Anhalten), □ bricht ab oder löscht das Paket (sofort, ohne Rückfrage), △ schaltet die Updates des Spiels an/aus. Rechts Titel-ID, Versionen, Tempo und der Fortschritt des laufenden Downloads mit Restzeit
- Ältere Versionen („Versionen“, mit eingetipptem Spielnamen) bleiben in der Browser-App; die Karte sagt das, wenn ein geladenes Paket nur so passt

### Reiter Disc: Dump-Einstellungen aufgeräumt
- Drei Abschnitte (Speicherort, Was wird gedumpt, Spielbar machen) und „Für Fortgeschrittene“ zugeklappt (klappt von selbst auf, wenn der Backport an ist). Je Schalter eine kurze Zeile statt eines Absatzes, mehr Abstand, Auswahllisten im Stil der App
- „Spielbar machen“ sagt deutlich: Entschlüsseln und FSELF müssen beide an sein, sonst zeigt die Konsole den Dump nicht, ShadowMountPlus bindet ihn nicht ein und er lässt sich nicht spielen
- Klarere Namen: „Nur bei PS4-Spielen: Spiel und Update“ (getrennt, nur Spiel, nur Update, in einem Ordner); die SDK-Ziele sind Auswahllisten mit Klartext („SDK 9.00 (Standard)“) statt Zahlenfeldern mit Stufen. Dieselben Namen in den Einstellungen je Spiel der Warteschlange. Gespeichert wird wie bisher

### Bedienung mit dem Controller: Stick verstellt keine Auswahl mehr
- Im Browser der Konsole kommt der Stick als Pfeiltaste an. Stand der Fokus auf einer Auswahlliste, einem Optionsknopf oder einem Zahlenfeld (etwa im Reiter Disc), verstellte Hoch/Runter still den gewählten Wert. Jetzt gehen die Pfeiltasten dort zum vorigen/nächsten Bedienelement, auf der ganzen Seite und in jedem Fenster; gewählt wird nur noch mit ✕. Ein Schieberegler behält links/rechts für seinen Wert, Rollen über einem Zahlenfeld rollt die Seite

### Spiele-Seite: „Starten“ aus, solange das Spiel läuft
- Auf der Karte des laufenden Spiels ist „Starten“ ausgegraut (Hinweis „Das Spiel läuft schon.“); daneben bleibt „Spiel beenden“. Die Vollbild-App behält auf ✕ „Nach vorn holen“, weil sie selbst vor dem Spiel liegt

### Apps wie YouTube bleiben unberührt
- **Spielstände:** Apps (YouTube, Browser, System-Apps; erkannt an `applicationCategoryType` ≠ 0 bzw. `CATEGORY` ohne „g“) stehen nicht mehr in der Liste, werden nicht mehr gesichert (auch nicht bei „alles sichern“), nicht zurückgespielt und nicht gelöscht; ältere Sicherungen bieten sie nicht mehr an, die Schnittstelle antwortet 403. Die Gruppen „PS5-Apps“ und „PS4-Apps“ sind entfallen
- **Spiel-Updates:** Nur was die Spiele-Seite als Spiel führt, kommt in die Liste. Ist diese Liste einmal nicht lesbar, steht nichts drin (vorher wäre dann auch eine App hineingeraten). PS5-Titel ohne Adresse einer offiziellen Versionsliste (Homebrew-Apps wie Gitarren Tuner oder SurfStation, die bisher mit „Prüfung fehlgeschlagen“ dastanden) stehen ebenfalls nicht mehr in der Liste

### Spiel-Updates: keine Updates mehr für Fake-PKGs und PS4Mount-Spiele
- **Fehler:** Die App lud für PS4Mount-Spiele offizielle Updates und legte sie als `ps5tm-update-<Titel-ID>-<Zeit>.pkg` in den Hauptordner des USB-Laufwerks. Diese Pakete sind offiziell signiert (Retail); PS4Mount hängt nur Fake-PKGs ein. Bei Indiana Jones (CUSA41353) war das „Update“ ein ganzes Spielpaket: PS4Mount meldete bei jeder Suche „hat schon ein Spielpaket“, die Datei wurde zweimal geladen, und nach dem Zurückverschieben des Spiels nahm PS4Mount sie als Spiel auf – das dann nicht startete (0x80a4000c)
- **Jetzt wie in PatchDL** („fake“: prüfen ja, laden und installieren nein): PS4Mount-Spiele und installierte Fake-PKGs (PS4 und PS5) stehen nicht mehr in der Update-Liste und werden weder geladen noch installiert. Schon geladene Pakete bleiben in den Einstellungen zum Löschen. Laden, Installieren, „Alle aktualisieren“ und Links von Hand werden mit einer Erklärung abgelehnt; ein schon eingereihter Auftrag läuft nicht mehr los. `ps4m_place_update` ist entfernt
- **Update beim Spielstart:** Das „No Title · Update-Datei“, das die Konsole beim Start von Two Point Hospital (installiertes Fake-PKG) lud und dann nicht installieren konnte (CE-109596-0), kam nicht von der App, sondern von der automatischen Update-Funktion der Konsole (`/user/download/CUSA15884`). Handbuch und Karte sagen jetzt, wo man sie abschaltet

### Spiel-Updates: geladene Pakete mit Namen
- In den Einstellungen der Spiel-Updates stand bei Paketen von Spielen, die nicht (mehr) in der Liste sind, nur die Titel-ID. Die App liest Name und Art jetzt aus dem Kopf des Pakets und zeigt „Update“, „Ganzes Spiel“ oder „Zusatzinhalt“ davor (`GET /api/v1/updates/packages` mit `kind`)
- Die Spieleliste meldet `fresh_ok` jetzt auch für PS4-Spiele von Disc/aus dem Store (wie der Bereich „Versionen“), damit der Knopf „Versionen“ bei einem gesperrten PS4-Spiel nicht fehlt

### Fremdbausteine: PatchDL 0.3.8 -> 0.3.9
- **PS4-Spiele können auf eine ältere Version zurück:** Ein PS4-Update baut immer auf dem Grundspiel auf. Die App entfernt nur das installierte Update (`sceAppInstUtilAppUnInstallPat` im Installationshelfer, neue Operation `PKGI_OP_UNINSTALL_PATCH`), das Spiel bleibt, dann geht die ältere Version drauf. Im Bereich „Versionen“ lassen sich bei PS4-Spielen von Disc oder aus dem Store jetzt ältere Versionen holen, der Knopf heißt „Downgrade auf … installieren“
- **Kein halbstündiges Warten mehr beim Ersetzen:** Entfernt die Konsole die alte Version nicht, versucht es die App ein zweites Mal unter der Kennung von ShellCore (wie ps5upload; nur für den einen Aufruf) und wartet 60 s bzw. 5 min statt bis zu 30 Minuten. Klappt beides nicht, kommt „Läuft das Spiel noch? Dann schließen und noch einmal versuchen“ – verloren ist nichts. Geprüft wird jetzt `app.json` auf allen Laufwerken der Konsole (`/user/app`, `/mnt/ext0|1/user/app`)
- **Schalter pro Spiel:** Er hält ein Spiel nur noch aus „Alle aktualisieren“ und dem normalen Update heraus. Eine Version von Hand lässt sich weiter holen – so kann ein Spiel nach einem Downgrade wieder wechseln
- Nicht übernommen: das Mitwandern der Spieleliste zwischen den Reitern (eigene Oberfläche) und PatchDLs eigenes Diagnose-Protokoll (die App schreibt ins eigene)

### Kacheln mit neuer Titel-ID

- **Löschen bei offener Kachel:** Die Kacheln heißen jetzt **`FAKE86921`** (Vollbild) und **`FAKE86922`** (Browser) statt `PPSA86921`/`PPSA86922`. ShadowMountPlus hielt eine offene PPSA-Kachel für ein laufendes Spiel und lehnte Löschen, Aushängen, Verschieben und Entpacken ab („a game is active or awaiting exit“, Konsolentest 28.10.2026 mit Tetris). Die alten Ordner in `/data/homebrew/` und ihre Kacheln löschen; die App erkennt die alten IDs weiterhin als eigene Kacheln
- **Löschen aus der Vollbild-App mit ShadowMountPlus 1.7beta5fix1:** Diese Fassung sperrt jedes Abmelden, solange irgendein von ihr eingehängter Titel läuft – auch die eigene Kachel mit FAKE-ID (Protokoll 28.10.2026: `uninstall blocked … a game is active`, während FAKE86921 lief). Der Lösch-Auftrag wartet jetzt bis zu 10 Minuten, bis die eigene Kachel geschlossen ist, und fragt dann erneut; die Vollbild-App schließt sich dafür selbst („„…“ wird gelöscht – Die Vollbild-App schließt sich dafür“), das Ergebnis kommt als Meldung der Konsole. Mit ShadowMountPlus 1.7beta9 sperrt die FAKE-Kachel nicht, dann wird ohne Schließen gelöscht
- **Verschieben und Entpacken über ShadowMountPlus** genauso: ShadowMountPlus 1.7beta5fix1 nimmt den Auftrag an und bricht ihn gleich mit „game_active“ ab, solange die Vollbild-App läuft. Ein Wächter der App sieht das, wartet bis zu 10 Minuten auf das Schließen der Kachel (Status „Wartet, bis die Vollbild-App geschlossen ist“), startet denselben Auftrag neu und meldet das Ende („… ist verschoben/entpackt“); die Vollbild-App schließt sich dafür selbst
- Die Bau-Prüfungen der Vollbild-App (`tvapp/tools/build.sh`, `lint.sh`, `deploy.sh`, `console-tour.py`) nehmen jede Titel-ID aus vier Großbuchstaben und fünf Ziffern statt nur PPSA

### Die Kachel ist jetzt eine Vollbild-App mit Controller
- **Statt des kleinen Browserfensters** öffnet die Kachel (PPSA86921, im Medien-Bereich) eine Vollbild-Oberfläche: Reiter **Kühlung** (Temperaturen, Lüfter, Zieltemperatur mit dem Steuerkreuz ändern, Statusanzeigen, Verlauf der letzten 2 Minuten), **Spiele** (Liste mit Details, Starten) und **Aufträge** (Kopieren, Konvertieren, Abbild mit Fortschritt und Abbrechen, Verlauf); L1/R1 wechselt die Reiter, Options öffnet das Menü mit „Weboberfläche im Browser öffnen“, „Neu laden“ und „Beenden“. Läuft die App noch nicht, startet die Kachel sie wie bisher selbst (die ELF steckt in der `eboot.bin`)
- **Die Browser-Kachel bleibt** als zweite Kachel **PPSA86922** („PS5 Cooling & System Center - Browser“, `starter/browser/`): ein Tipp startet die App und öffnet die Weboberfläche im Browser der Konsole, wie die bisherige Kachel. `tools/make_starter.sh` baut beide, ins Release gehören `PPSA86921.zip` und `PPSA86922.zip`
- Gebaut auf dem Oberflächen-Baukasten **ps5-homebrew-ui** (BlackBearReloaded, GPL-3.0-or-later; erhaltene Kopie, OpenGL-SDK per SHA-256 geprüft), Quelltext in `tvapp/`, Herkunft in `tvapp/ORIGIN.md`; `tools/make_starter.sh` baut die Kachel jetzt über `tools/tvapp-build.sh` (WSL); der bisherige Browser-Starter ist nach `starter/browser/` umgezogen (siehe oben)
- **Spiel starten aus der Vollbild-App:** Weil die Kachel selbst ein Titel ist, hielt die Konsole sie für das laufende Spiel und lehnte den Start ab. Neu: `POST /api/v1/library/launch` mit `"after_exit":"<Titel-ID der Kachel>"` – die App sagt sofort zu (202 `state: after_exit`), die Vollbild-App beendet sich über das System, danach startet das Spiel. Die Vollbild-App selbst taucht nicht mehr in der Spieleliste und nicht als „Im Spiel“ auf
- Medien-Bereich: `applicationCategoryType` 65536, `contentBadgeType` 2; die Seitentabellen sind dort auf höchstens 128 MiB begrenzt (mit 256 MiB startete sie nicht: CE-100022-5)
- An der Pro (FW 12.00) vom User getestet: Vollbild, Controller, Ton, Zieltemperatur, Spieleliste, Spielstart, Menü
- **Cover** in der Spieleliste und in den Details: Der Netzwerk-Faden lädt sie nacheinander (das Spiel im Fokus zuerst), verkleinert sie auf 160×160 und blendet sie über die Buchstaben-Kachel ein; PNG-Decoder stb_image 2.30 (gemeinfrei/MIT, Prüfsumme in `tvapp/ORIGIN.md`)
- Behoben: Die Details (△) flackerten und sprangen, weil sie mit jeder Status-Aktualisierung neu befüllt wurden und dabei ihre Einblend-Animation neu begann; Werte bleiben einzeilig
- Der Verlauf auf „Kühlung“ reicht immer bis mindestens 91 °C (Achse 20–100), damit eine normale Temperatur nicht wie „am Anschlag“ aussieht
- **Neue Reiter** (sechs in einer eigenen Leiste, L1/R1; L2/R2 wechselt innerhalb einer Seite): **Einstellungen** (Modus, Betriebsart, Sicherheitsgrenze, Nachtmodus mit Beginn/Ende/Nachtziel, gespeichert 0,6 s nach der letzten Änderung; Lüfter-Selbsttest mit Verlauf), **Payloads** (eigene starten, laufende beenden, Payload-Store mit Laden/Aktualisieren/Starten), **Protokoll** (App-Protokoll und Kernel-Log live, hält beim Hochblättern an). Das Menü hat die **Power-Optionen** (Ruhemodus, Neu starten, Ausschalten, Abgesicherter Modus, jeweils mit Rückfrage). Abgefragt wird nur, solange der Reiter offen ist. An der Pro vom User getestet (Einstellungen, Payloads starten, Protokoll, Power-Optionen)
- **Entwicklerbilder statt Kacheln:** Für Payloads aus dem Store (und eigene oder laufende Payloads, deren Name zu einem Store-Eintrag passt) zeigt die Vollbild-App das GitHub-Profilbild des Entwicklers. Die App lädt fehlende Bilder nach jedem Laden der Payload-Liste im Hintergrund von `avatars.githubusercontent.com` (gleiche GitHub-Zertifikate wie der Store, je Entwickler einmal pro Lauf, höchstens 256 KB, nur PNG/JPEG) nach `/data/PS5-Cooling-Center/payload-avatars/`; ohne Internet bleibt es bei den Kacheln. Die Bilder gehören ihren Inhabern und werden nicht mit der App verteilt. Neu: `owner`/`avatar` je Eintrag in `GET /api/v1/paystore`, `GET /api/v1/paystore/avatar?owner=…`
- **Payload-Kacheln:** Payloads haben kein eigenes Bild; sie bekommen jetzt Kacheln wie Spiele ohne Cover – bekannte mit festem Kürzel (SM ShadowMountPlus, P4 PS4Mount, PK PKG-Manager, eH etaHEN, KS kstuff, EL elfldr, FT ftpsrv, CC diese App …), andere mit ihren Anfangsbuchstaben; die Farbe zeigt die Art (Netzwerk, System/Jailbreak, Lader, Werkzeuge, Cheats)

### Keine doppelten Bestätigungen mehr
- **Jede Aktion wirkt beim ersten Klick** (Browser-Oberfläche): Löschen (Spiele, Sicherungen, Dateimanager, Spielstände, Dumps, Payloads), Power-Optionen, Payload beenden, Zurückverschieben (PS4Mount), Deaktivieren/Aktivieren, Ordner und Verläufe leeren, Avatar übernehmen/zurückholen, Aufträge abbrechen, Dump ersetzen – kein Häkchen, kein „Wirklich? Nochmal klicken“, keine „Trotzdem?“-Frage. Solange eine Aktion läuft, ist ihr Knopf gesperrt
- Löschen mit Plan (`/library/delete/plan`, `/files/delete/plan`): Die Seite holt Plan und Kennwort still und löscht direkt; ein Fenster bleibt nur bei einer Ablehnung mit dem Grund stehen
- **Starten bei laufendem Spiel** schließt das laufende und startet das gewählte in einem Zug (`close` bei `/library/launch`)
- Spielstände löschen sichert vorher automatisch auf das erste passende Laufwerk; ohne Laufwerk wird mit `no_backup` gelöscht, und ein Hinweis sagt das
- Updates/Versionen: Den Spielnamen für „Neu installieren“ und ältere Versionen schickt die Seite selbst mit (die Prüfung der Konsole bleibt); weicht die Größe von der Liste ab, wird es gesagt und trotzdem geladen
- Payload-Store: Liegt schon eine Datei gleichen Namens im Ordner, wird sie ersetzt und es gibt einen Hinweis
- Die Ruckler-Warnung vor ffpfsc bleibt als Hinweis im Fenster, ohne zweite Frage

### Konvertieren: auch PS4-Spielordner
- Ordner mit nur `sce_sys/param.sfo` (PS4-Spiele) lassen sich jetzt in exFAT, ffpkg und ffpfsc umwandeln – ShadowMountPlus bindet PS4-Abbilder seit 1.7beta8 ein. Wie bei den PS4-Dumps legt die App eine `param.json` aus der `param.sfo` (und bei Bedarf eine korrigierte `npbind.dat`) mit ins Abbild, damit auch ältere Versionen es finden; der Ordner bleibt unverändert. Ziel nur USB/M.2 (ein PS4-Titel aus dem Speicher der Konsole bringt sie zum Absturz), kein AMPR-Index. Plan-Feld `ps4`. An der Konsole noch nicht getestet

### Dateimanager: ganze Ordner hochladen
- Neuer Knopf **„Ordner hochladen“**, und Ordner lassen sich auf die Liste ziehen: Die Ordnerstruktur wird auf der Konsole angelegt, dann laufen die Dateien einzeln mit Fortschritt; Dateien, die es am Ziel schon gibt, werden übersprungen und gezählt, nie überschrieben

### Oberfläche
- Der Streifen über der Bibliothek (Kopieren, Konvertieren, Aufträge von ShadowMountPlus) ist farbig und größer: läuft blau, fertig grün, gescheitert rot, abgebrochen gelb (Hinweis eines Nutzers: im Hintergrund fiel er nicht auf)
- Doppelte Abbilder: „Beiseitelegen“ heißt jetzt **„Deaktivieren“**, „Zurückholen“ **„Aktivieren“**

### Lüfter: unabhängiger Wächter, kein Pendeln mehr bei Zielen um 72–82 °C
- **Wächter (neu, `src/fanguard.c`):** eigener Faden mit eigenen Sensorabfragen, unabhängig von der Regelung. Meldet sich die Regelschleife 20 s nicht, geht der Lüfter an die Konsole zurück (bei Hitze volle Kühlung), bis sie wieder läuft; nach dem Ruhemodus löst er nicht aus. Liegt die Konsole 10 s über Sicherheitsgrenze + 6 °C (mindestens 84 °C), während der Lüfter unter 90 % läuft, hält er volle Kühlung (mindestens 30 s, bis 8 °C unter der Grenze); die Regelung schreibt solange nichts. Bei einem Absturz (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) wird die Lüftereinstellung noch an die Konsole zurückgegeben (SIGKILL lässt sich nicht abfangen; an der Konsole noch nicht geprüft). Status: `control.guard_state` in `GET /api/v1/status`, Anzeige in der Lüfterdiagnose
- **Pendeln behoben:** Bei Zielen um 72–82 °C liegt die automatische Sicherheitsgrenze nur 4 K über dem Ziel, das Band verlangt die nötige Drehzahl aber erst weiter oben. Unter Volllast lief die Konsole in die Grenze, die Notrampe trieb den Lüfter fast auf 100 %, danach fiel er auf den niedrigen Bandwert zurück – alle 6–10 Minuten, bei Ziel 75 zwischen 36 und 100 %. Jetzt bleibt die Drehzahl, auf der die Notrampe endet, als Untergrenze stehen: unter dem Ziel wird sie langsam abgebaut (1 % je 15 s, weit darunter je 6 s), knapp unter der Grenze bei steigender Temperatur aufgebaut (1 % je 3 s), dazwischen gehalten; gelöscht wird sie erst 45 s nach einem Fall von 10 K unter das Ziel (Spielende)
- **Stabilitätsprüfung (neu, `test/sim/`):** Die echte Regelschleife und der Wächter laufen in virtueller Zeit gegen ein Wärmemodell (Firmware-Kennlinie aus Messungen, Kühlkörper und Chip geschätzt). Ergebnis: kein Pendeln bei Zielen 50–91 °C, auch nicht bei halber/doppelter Firmware-Verstärkung, doppelter Lüfterträgheit, halber/doppelter Zeitkonstante oder ±3 K Rauschen; erst bei dreifacher Verstärkung pendelt der Servo. Das Wärmemodell ist nicht an der Konsole vermessen. Aufruf siehe `docs/ENTWICKLUNG.md`

### Spiel-Updates: PS4Mount-Spiele bekommen ihr Update jetzt auf das Laufwerk statt in die Installation
- **Ursache gefunden:** Ein Update für ein PS4Mount-Spiel (USB-Laufwerk) lief über die normale Installation, die Konsole meldete sie als fertig, aber `app.json` des Titels zeigte danach weiter auf die unveränderte Datei auf dem USB-Laufwerk und `param.sfo` behielt die alte Version – das Update erreichte nie das Spiel, das PS4Mount tatsächlich startet. Geprüft an der Pro: CUSA44880 blieb nach einem „erfolgreichen“ Update auf 01.00 statt 01.02
- Die Spiele-Update-Seite erkennt PS4Mount-Spiele jetzt (neue Funktion `ps4m_title_managed`) und legt ein geladenes, geprüftes Update für sie auf ihrem Laufwerk ab (`ps4m_place_update`), statt es zu installieren – PS4Mounts eigene Suche ordnet es beim nächsten Mal ein, mit derselben Versionsprüfung wie ein von Hand aufs Laufwerk gelegtes Update (siehe PS4Mount 0.7.0 unten). Die Liste zeigt dazu `via_ps4mount`
- Zusätzlich behoben: `installed_state()` (prüft, ob eine Installation in der Konsolenliste angekommen ist) kannte nur `param.json`, aber PS4-Titel unter Abwärtskompatibilität haben nur `param.sfo` (binäres Format) – ein neuer Leser dafür ist ergänzt, betrifft normal installierte PS4-Spiele

### PS4-Spiele von USB: Updates wählen, Zusatzinhalte an/aus
- Jedes PS4Mount-Spiel mit mehr als einem Update oder mit Zusatzinhalten bekommt in der schon vorhandenen Liste „PS4 von USB“ zwei neue Knöpfe: **Updates** (welche der vorhandenen Versionen benutzt wird, auch eine ältere, in `replaced/` abgelegte) und **Zusatzinhalte** (einzeln an/aus, mit dem Höchstwert von PS4Mount). Kein eigener Knopf in der Seitenleiste und kein „Starten“ – die Bibliothek deckt das Starten schon ab, eine eigene Seite wäre sonst nur dieselbe Liste noch einmal gewesen (Wunsch des Users, 26.10.2026)
- Neuer Schalter bei den PS4Mount-Einstellungen: **Zusatzinhalte auf den internen Speicher kopieren** – statt vom USB-Laufwerk einzuhängen, kopiert die Konsole jeden Zusatzinhalt wie bei einem normal installierten Spiel; braucht mehr Platz und Zeit, zeigt sie dafür wie gewohnt im Speicherbildschirm
- Neue API-Routen: `GET /api/v1/ps4mount/game`, `POST /api/v1/ps4mount/update`, `/dlc` (siehe `docs/API.md`)

### Spiel-Updates: Installation über die Paketliste, Bereich „Versionen“ mit Neuinstallation und Downgrade
- **Wie ein Update der Konsole:** PS5-Updates von Spielen von Disc oder aus dem Store gehen jetzt über eine lokale Paketliste an die Konsole (die offizielle, deren Stücke auf den Server der App zeigen, mit PlayGo-Prüfdatei). Die Konsole schreibt dann ihre Spieldaten wie nach einem eigenen Update. Lehnt sie das ab, installiert die App das Paket im selben Auftrag wie bisher; im Protokoll steht, warum. Downloads ohne gespeicherte Paketliste holen sie vor der Installation einmal nach
- **Grundversion:** Ein Update, das auf einer anderen Grundversion aufbaut als das installierte Spiel (häufig bei Disc-Spielen), wird nicht mehr normal installiert, sondern als „passt nur als Neuinstallation“ gemeldet
- **Neuer Bereich „Versionen“** je Spiel (ersetzt „Andere Versionen“): installierte Version und Grundversion, das geladene Paket, ob beide passen; die Versionsliste lädt erst ein Klick. **Neu installieren** und **Downgrade** (nur PS5, Disc/Store): ältere oder die installierte Version holen, dann – nur mit eingetipptem Spielnamen, nie während das Spiel läuft – das Spiel deinstallieren (Spielstände und Zusatzinhalte bleiben) und das Paket als ganzes Spiel installieren; ein Abbruch dazwischen lässt sich fortsetzen; nach einem Downgrade sind die Updates des Spiels in der App aus
- Neu: `GET /api/v1/updates/fit`, Aktion `reinstall`, `download_url` mit `older`/`confirm`, Felder `pkg_version`, `orphan`, `fresh`, `held`, `fit`, `fresh_ok` je Spiel; der Installationshelfer kennt „nur das Spiel deinstallieren“; der Paket-Server liefert Paketliste, Prüfdatei und Paketstücke (`/stream/install/p/<Versatz>/<Größe>/<Name>`)
- An der Konsole noch nicht getestet

### Fremdbausteine: PatchDL 0.3.4 -> 0.3.8
- Übernommen (GPL-3.0, jetzt auch als Code-Port mit Herkunftsangabe): 0.3.5 Installation über die Paketliste, 0.3.6 Grundversions-Prüfung und Neuinstallation, 0.3.7 Paketliste für ältere Downloads nachholen und Prüfung über das `_sc`-Stück, 0.3.8 Downgrade (PS5)
- Nicht übernommen: der eigene Webserver, die Kachel-/Cover-Anzeige von PatchDL und das Herauslösen des Covers aus dem Paket (die App zeigt das Bild aus dem Paket bei der Installation schon selbst); PS4-Downgrade (erst PatchDL 0.3.9)
- Weiter nur Direktdownload: keine SC-Links werden unverändert abgerufen, keine zusätzlichen Abfragen bei Prospero/Orbis Patches

### Fremdbausteine: PS5-Payload-SDK v0.44 -> v0.45
- Gebaut wird mit **v0.45** (offizielles `ps5-payload-sdk.zip`, SHA-256 `5fac6409…1a2c`, Dateien gleich mit dem Projekt-SDK; v0.44 als Sicherung neben dem Projekt). Neu ist nur der Startcode: Er kennt jetzt auch **Firmware 14.00 und 14.10** (`crt1.o`, `libkernel_sys.so`). Der Bau prüft das mit (`FW_NEED` = 13.00 bis 14.10), die Systemseite nennt die Gruppe „14.00-14.10“. Auf 14.xx ist die App nicht getestet
- Auch die Kacheln bauen mit v0.45: die Browser-Kachel über das Projekt-SDK, die Vollbild-App über die im Baukasten festgelegte Fassung (`tvapp/`, Prüfsumme dort eingetragen)

### Fremdbausteine: ShadowMountPlus 1.7beta7 -> 1.7beta9
- ShadowMountPlus meldet über seinen Auftragsstatus seit 1.7beta8 auch eigene Installationen (`operation=install`: ein PKG oder ein Spielordner/Abbild aus seiner Oberfläche, teils ohne Titel-ID). Der Streifen über der Bibliothek nennt sie jetzt „Installieren“ und zeigt ohne Titel-ID den Paket- oder Dateinamen statt einer leeren Angabe (`web/app.js`)
- Handbuch (sechs Sprachen): die Installationen im Streifen und der Hinweis, dass die Schnittstelle von ShadowMountPlus seit 1.7beta8 standardmäßig im ganzen Heimnetz erreichbar ist (`api_bind_address=127.0.0.1` in dessen `config.ini` schränkt das ein; die App spricht es ohnehin über `127.0.0.1` an)
- Geprüft, ohne Änderung nötig: Die von der App benutzten Routen (`games/move`, `games/unpack`, `games/storage/status|cancel`, `debug-log`) sind unverändert; Backports, die ShadowMountPlus selbst installiert, landen in `/data/homebrew/backports`, das die App schon durchsucht; Kopieren und Dumps legen `param.json` und `param.sfo` schon zuletzt ab (ShadowMountPlus erkennt Spielordner jetzt auch an `param.sfo` und `eboot2.bin`)
- Nicht übernommen: SMB-Freigaben, die Paket-Warteschlange und das Installieren aus Ordnern/Abbildern von ShadowMountPlus (eigene Funktionen von ShadowMountPlus, die App ruft sie nicht auf)

### Fremdbausteine: PS4Mount 0.6.1 -> 0.7.0
- Ein gefundenes Update wird jetzt nach Versionsnummer behandelt: Ist das neu gefundene Update älter als das benutzte, legt es sich selbst in `replaced/` statt das benutzte zu verdrängen (vorher ersetzte jedes andere Update das vorhandene, auch ein älteres)
- Beim Schließen eines Spiels vor dem Zurückverschieben wartet die App zusätzlich, bis die PFS-Einhängungen des Spiels wirklich freigegeben sind (bis zu 20 s) – vorher konnte ein zu früh neu gestartetes Spiel seine eigenen Dateien kurz nicht lesen (Fehler 0x80020060)
- **Nachträglich übernommen:** „Add-ons intern kopieren“ (`src/ps4m_accopy.c`/`.h`, neu) – siehe oben „PS4-Spiele von USB“ und `third_party/ps4mount/ORIGIN.md`. Das Starten eines Spiels aus dem Panel (`ps4m_launch`) ist bewusst nicht übernommen, die Bibliothek deckt das schon ab


### Lüfterregelung reagiert in Sekunden statt in Minuten
- **Ursache gemessen, nicht vermutet:** Aufzeichnung an der Pro (CFI-7021) mit zwei bis sieben Messungen pro Sekunde (Temperatur, Schwelle, Sollwert, gemessene Drehzahl) bei Zielwechsel 91 ↔ 50 °C. Die Firmware folgt einer neuen Schwelle in **unter einer Sekunde**; die Zeit ging in der Regelung der App verloren: Zielwechsel wurden erst im nächsten 5-Sekunden-Takt bemerkt (gemessen 5,5 s), der Sollwert kroch bei 0 % los, obwohl der Lüfter schon auf dem Spiel-Mindestwert von 33 % stand, und die Schwelle wanderte in Schritten von 91 °C zur Temperatur, die Firmware antwortet aber erst, wenn die Schwelle die Temperatur erreicht (bis zu 20 s). Weit unter dem Ziel dauerte das Herunterfahren fünf Minuten (1 % je Takt)
- **Takte:** Regeltakt aller Profile 1 s (vorher 4–5 s), Schwellen-Nachstellung (Servo) 2 s (vorher 6 s), Temperatur-Mittelung 3–4 s (vorher 8–12 s)
- **Herunterfahren:** Ab 8 K unter dem Ziel springt der Sollwert sofort auf den Bandwert und die Schwelle in derselben Sekunde auf die Ruhelage (gemessen: von 89 % auf 33 % in etwa 3 s nach dem Umstellen); zwischen 3 und 8 K unter dem Ziel sinkt er um ein Viertel der Lücke je Takt, nahe am Ziel bleibt es bei 1 % mit der alten Wartezeit, damit der Lüfter nicht pendelt
- **Hochfahren:** Der Sollwert beginnt bei der tatsächlichen Drehzahl statt bei 0 % und holt die Hälfte der Lücke je Sekunde auf (vorher ein Viertel je 5 s). Steht die Schwelle über der Temperatur, setzt eine **Vorsteuerung** sie sofort in den wirksamen Bereich: gemessen gibt die Firmware etwa **5,6 % Drehzahl je Grad** zwischen Temperatur und Schwelle über ihrem Mindestwert (1,5 K → 43 %, 5 K → 61 %, 7 K → 72 % ab 33 %). Danach trimmt der Servo wie bisher; ein erster Versuch, die Vorsteuerung dauerhaft laufen zu lassen, erzeugte einen Sägezahn (Sprung von 65 % auf 83 %) und gilt deshalb nur für den ersten Sprung aus der Ruhelage. Größere Abweichungen (ab 20 Prozentpunkten) bewegen die Schwelle in größeren Schritten
- Gemessen an der Pro: Schwelle 0,3 s nach dem Umstellen, Drehzahl reagiert nach etwa 1 s, Ziel in 4–8 s ohne Sägezahn. Die 5,6 % je Grad stammen nur von der Pro; an der Slim (CFI-2116) nicht aufgezeichnet. Geändert: `src/fan.c`, `src/config.c`

### Lüfteranzeige in Zehntelprozent (Skala 0–1024)
- Das Drehzahlregister liefert 0–1024 (10 Bit), nicht 0–1000 wie bisher angenommen (ShellCore rechnet mit 100/1024). Aus dem Rohwert wird jetzt `raw × 1000 / 1024` gebildet; die Anzeige zeigt eine Dezimalstelle (z. B. 33,0 % statt starr 34 %), `GET /api/v1/status` liefert dazu `fan.measured_duty_pct10`. Werte über 1024 gelten als ungültig statt als 100 %
- Behoben: Das Fan-Element schrieb bei jedem Anzeigeereignis die gerundete Ganzzahl zurück in die Kachel und überschrieb die Dezimalstelle; der Wert wird jetzt vor dem Ereignis gesetzt. Die Fortschrittsbalken, der Mini-Verlauf, die Soll-Ist-Abweichung und die Überwachung nutzen den Zehntelwert
- **Diagnose:** `GET /api/v1/status` gibt zusätzlich `fan.duty_scratch_8` (die ersten acht Bytes des bisher ungenutzten Puffers von `sceKernelGetCurrentFanDuty`) und `fan.icc_raw_6` mit `icc_raw_duty16` (direkter Aufruf des ICC-Befehls `0xC0068F06`) aus; `GET /api/v1/channels` liefert sie je Messpunkt unter `fan_diag` mit „moved/min/max“ je Byte. Erste Messung: der Puffer trägt Werte (z. B. 96 2 1 1), der direkte ICC-Aufruf liefert nur Nullen; was die Bytes bedeuten, ist offen

### Zieltemperatur bis 50 °C wählbar
- Die Untergrenze der Zieltemperatur (Betriebsart „Automatik“, ebenso das Nachtziel) sinkt von 60 auf **50 °C** (`PS5TM_TARGET_MIN_C` in `ps5tm.h`) – damit deckt sie denselben Bereich wie ShadowMountPlus' eigene feste Schwelle (`fan_target_temperature=system|50..91`)
- Oberfläche, Vorgabewerte im Skript, API-Dokumentation und README in allen sechs Sprachen auf 50–91 °C angepasst; die Notfallgrenze (Ziel + 4 °C, mindestens 72) und die Warnschwelle (Ziel + 2 °C) folgen automatisch wie bisher, kein zusätzlicher Code nötig

### Dumpen im Disc-Reiter
- Das Dumpen von Spielen steht im Reiter **Disc** der Spiele-Seite; den Reiter „Dumps“ gibt es nicht mehr, und die Spielkarten haben keinen Knopf „Dumpen“ mehr. Die Karte **Disc-Spiele dumpen** zeigt die PS5- und PS4-Spiele, die von Disc installiert sind (Marken DISC und „Disc raus“, Stand der Installation, DLC): **Starten**, und läuft das Spiel, **Dumpen**. Darunter folgen die Karten **Dump** (Fortschritt mit Tempo, Restzeit und Dateizähler, Stoppen mit Rückfrage, ein fertiger Dump wird nur nach Bestätigung ersetzt, **Nur DLC**), **Einstellungen** und **Dumps aus dem Disc-Bereich**
- **Ablauf wie beim ps5-app-dumper 2.10** (EchoStretch, slopmaster33; GPL-3.0-oder-später): Ein laufendes PS5-Spiel wird aus `/mnt/sandbox/pfsmnt/<TITEL>-app0` samt Metadaten, Trophäen- und UDS-Dateien auf ein USB-Laufwerk oder in den Konsolenspeicher kopiert (Platzprüfung mit 10 GB Reserve, FAT32-Grenze, Ruhemodus-Sperre, optional Zurücklesen mit Prüfsummen-Datei, Protokoll je Dump). DLC kommen als eigene Ordner dazu. Dateien aus `app.pkg` und `ac.pkg` ergänzen `sce_sys`. Neben jedem Ordner liegt `<Ordner>.dump-info.json` im Format des Dumpers
- **PS4-Spiele** liest die App aus dem PFS-Abbild (`<CUSA>-app0-nest/pfs_image.dat`, beim Update `-patch0-nest`; ohne Abbild aus dem eingehängten Ordner) mit der **PS4-Aufteilung** App und Patch getrennt, nur App, nur Patch oder ein Ordner (Quelle zusätzlich: ps4-app-dumper, Alazif, Scene-Collective). Die Paketsuche folgt Verknüpfungen und schaut auch in `/system_data/ps4mount`. Entschlüsselt wird noch nichts
- **Die Liste zeigt nur Dumps aus dem Disc-Reiter**: Jeder Dump bekommt in der Info-Datei das Feld `origin: disc-tab`. Dumps anderer Herkunft, ältere Dumps ohne dieses Feld und die Spielordner von ShadowMountPlus erscheinen nirgends und markieren kein Spiel als GEDUMPT oder UNFERTIG. Jeder Eintrag hat **Löschen** (zwei Klicks, nur Dumps dieser Liste), bei PS4-Dumps **Als Abbild bauen**
- **PS4-Dump als Abbild für ShadowMountPlus**: Die Konsole legt Dump (`<CUSA>-app0`) und Update (`<CUSA>-patch0`) beim Schreiben übereinander, ohne etwas zu kopieren, erzeugt aus der `param.sfo` die `param.json` und bessert die `npbind.dat` aus (wie das PC-Werkzeug PS4FFPFSC 0.2.9, GPL-3.0-oder-später) und packt als **ffpfsc** (komprimiert, empfohlen), **ffpkg** (UFS2, 64-KiB-Blöcke) oder **exFAT** nach `homebrew` eines USB- oder M.2-Laufwerks, nie in den Speicher der Konsole. Neue Dateien `src/dump_job.c`, `src/dump_lib.c`, `src/dump_ps4.c`, `src/dump_unpkg.c`, `src/dump_image.c`; Routen `GET/POST /api/v1/dump`, `GET /api/v1/dumps`, `POST /api/v1/dumps/scan`, `/delete` und `/image`
- Neue Oberflächentexte in en, es, fr, it, ru

### Dumpen: Entschlüsseln und FSELF
- Im Reiter **Disc** gibt es in den Einstellungen die Schalter **Ausführbare Dateien entschlüsseln** und **Als FSELF neu signieren** (nur mit Entschlüsseln). Beides ist ab Werk aus, bis es an der Konsole erprobt ist. Nach dem Kopieren ersetzt die App wie der ps5-app-dumper die Programmdateien (`.elf`, `.self`, `.prx`, `.sprx`, `.bin`, auch `eboot.bin`) im Dump durch ihre entschlüsselte Fassung; PS5 aus dem Ordner des laufenden Spiels, PS4 aus dem eingehängten App- und Patch-Ordner. Mit FSELF werden die Dateien zu FSELF-Dateien, die einfachen ELF-Dateien liegen zusätzlich im Ordner `decrypted` des Dumps. Das Fortschrittsfeld zeigt „Entschlüsselt: n von m Dateien“.
- Das Entschlüsseln schreibt kurz in den Kernel der Konsole (nach ps5-self-pager von idlesauce, SELF-Aufbau nach Cryptogenic; Quelle der Logik: ps5-app-dumper 2.10 von EchoStretch und slopmaster33, GPL-3.0-oder-später). Abgesichert ist es so: nur Firmware, die die Tabelle des Self-Pagers kennt (67 Stände von 1.00 bis 13.60, Zeile für Zeile gegen das Original geprüft), beide Zeiger der Tabelle müssen vorher wie Kernel-Zeiger aussehen, das Fenster ist ein einziger `mmap`-Aufruf unter einer Sperre, der alte Wert wird danach zurückgeschrieben, zurückgelesen und verglichen (scheitert das dreimal, verweigert die App alles Weitere und sagt, die Konsole neu zu starten), und es läuft nur im Faden des Dumps, immer nur eines zur Zeit. Die App selbst bildet sonst nirgends Dateien in den Speicher ein. Bei anderer Firmware läuft der Dump ohne Entschlüsseln weiter; was sich nicht entschlüsseln lässt, bleibt, wie die Konsole es zeigt.
- Gegenüber dem Original: Die entschlüsselte Datei wird Abschnitt für Abschnitt in eine Zieldatei mit vorläufigem Namen geschrieben (statt ganz im Speicher aufgebaut) und ersetzt die Kopie erst, wenn sie vollständig ist; alle Größen und Abstände aus den Dateiköpfen werden geprüft; ein Abschnitt über 1 GB oder eine Datei über 4 GB wird übersprungen; Platz wird vorher geprüft. Mit Entschlüsseln gibt es keine Prüfsummen-Datei, weil die Dateien nach dem Kopieren ersetzt werden (das Zurücklesen prüft die Kopie). DLC werden wie beim Dumper nicht entschlüsselt.
- Getestet nur auf dem PC (gcc mit ASan und UBSan, die Konsole als Attrappe): Ergebnis Byte für Byte wie erwartet, die FSELF-Datei Byte für Byte gleich der des Dumpers, 4000 zufällig veränderte SELF-Köpfe ohne Absturz, Abbruch, nicht unterstützte Firmware und falsche Tabellenzeiger ohne Kernel-Schreibzugriff, Rückstellung des Zeigers nach jedem Fenster. An der Konsole noch nicht erprobt.

### Dumpen: SDK-Versionen herunterpatchen (Backport)
- Im Reiter **Disc** gibt es in den Einstellungen **SDK-Versionen herunterpatchen (Backport)** mit **PS4-SDK-Ziel (1–6)** und **PS5-SDK-Ziel (1–10)** (Vorgaben aus, 4 und 1; Werte außerhalb des Bereichs werden hineingezogen). Es greift wie im ps5-app-dumper nur zusammen mit dem Entschlüsseln und läuft danach: Die SDK-Felder der einfachen ELF-Dateien im Dump (`.bin .elf .prx .sprx`, bei PS5 auch `.self`) und die Parameterdatei (PS4: `param.sfo` mit `PUBTOOLINFO sdk_ver` und `SYSTEM_VER`, PS5: `param.json` mit `requiredSystemSoftwareVersion` und `sdkVersion`) werden auf den Wert der Stufe gesenkt, wenn sie höher sind. Die unveränderten Dateien liegen zusätzlich im Ordner `decrypted` des Dumps
- Es ändert nur Dateien im Dump, nie die Installation des Spiels auf der Konsole (die Pfade `/user/app`, `/mnt/sandbox`, `/app0` u. a. werden abgelehnt, Verknüpfungen nie verfolgt). Dateien mit FSELF-Signatur lässt es wie der Dumper aus: Mit „Als FSELF neu signieren“ werden nur `param.sfo` und `param.json` angepasst. Im Fortschritt steht „Backport: n von m Dateien“, die Info-Datei des Dumps trägt Schalter und Stufen ein
- Gegenüber dem Original: Es bildet keine Dateien in den Speicher ab, prüft alle Offsets und Größen gegen die Datei, patcht ein ELF an Ort und Stelle (vier Bytes, danach zurückgelesen) und schreibt die Parameterdateien über einen vorläufigen Namen; `param.json` wird nur umgeschrieben, wenn der Wert genau 18 Zeichen hat (das Original hätte andere Längen überschrieben)
- Quelle der Logik: ps5-app-dumper 2.10 (EchoStretch, slopmaster33; GPL-3.0-or-later). Getestet nur auf dem PC (gcc mit ASan und UBSan): 480 Beispielbäume (PS4 Stufe 1–6, PS5 Stufe 1–10) kommen Byte für Byte gleich zum Original heraus, 800 zufällig beschädigte Bäume ohne Absturz, Verknüpfungen und geschützte Pfade werden abgelehnt. Auf der Konsole ist es noch nicht erprobt

### Dumpen: Warteschlange
- Im Reiter **Disc** gibt es die Karte **Dump-Warteschlange**: bis zu 64 Spiele nacheinander dumpen (nach der Warteschlange des ps5-app-dumper 2.10 von EchoStretch und slopmaster33, GPL-3.0-oder-später). An jedem Disc-Spiel steht **In die Warteschlange**; weitere Spiele lassen sich über die Auswahl unter der Liste hinzufügen. Die Liste liegt auf der Konsole, die Seite darf geschlossen werden; ein Neustart der App verwirft sie und wird einmal gemeldet („unterbrochen“).
- **Ablauf je Titel:** Das Spiel wird gestartet (ein laufendes Spiel wird dabei beendet; der Start der Warteschlange fragt vorher mit zwei Klicks), bekommt die **Ladezeit** (5–600 Sekunden, Vorgabe 30) und wird dann mit den Einstellungen der Seite gedumpt. Ein Titel, der nicht hochkommt, sich nicht starten lässt, noch installiert wird, über ShadowMountPlus läuft oder scheitert, wird markiert, und die Warteschlange macht weiter.
- **Disc-Spiele:** Liegt die Disc nicht im Laufwerk, hält die Warteschlange so lange, wie es dauert, erinnert die Konsole alle zwei Minuten (einzeilige Meldung) und macht ein paar Sekunden nach dem Einlegen weiter. Die Marke **DISC/PKG** eines Titels lässt sich vor dem Start per Klick umschalten; gesehene Disc-Spiele merkt sich die App in `/data/PS5-Cooling-Center/dump/disc_titles.txt`, damit „Disc raus“ auch ohne Disc stimmt (wie beim Dumper).
- **Einstellungen je Titel:** Entschlüsseln, FSELF, Backport-Ziele und PS4-Aufteilung können je Spiel abweichen; solche Titel tragen die Marke **EIGENE**, „Wie die Einstellungen oben“ nimmt sie zurück. Jeder Dump schreibt die verwendeten Einstellungen ins Protokoll.
- **Vorhandene Dumps:** „Überspringen“ (Vorgabe) lässt Spiele mit fertigem Dump aus, „Ersetzen“ dumpt sie neu; Titel mit fertigem Dump aus dem Disc-Reiter tragen die Marke **GEDUMPT**. Ein unfertiger alter Dump wird wie sonst vor dem neuen entfernt.
- **Stoppen:** „Warteschlange stoppen“ bricht den laufenden Dump ab und lässt den Rest aus; „Diesen Titel überspringen“ beendet ein Warten (Disc, Start, Ladezeit), „Diesen Dump stoppen“ gibt nur diesen Dump auf (Rückfrage, die bisher geschriebenen Dateien bleiben unvollständig) und geht zum nächsten. Solange die Warteschlange läuft, starten keine anderen Dateiaufträge, und der Ruhemodus bleibt gesperrt.
- Weicht vom Dumper ab: Die Liste wird mit den Aktionen `queue_add`, `queue_remove`, `queue_clear`, `queue_set`, `queue_settings`, `queue_start`, `queue_stop`, `queue_skip` auf der Konsole geführt (`POST /api/v1/dump`, Zustand in `GET /api/v1/dump`, Feld `queue`); der Start eines Spiels nutzt den Weg der App („Spiel beenden und starten“), nicht den des Dumpers. Getestet nur auf dem PC (gcc mit ASan und UBSan, die Konsole als Attrappe: Reihenfolge, Disc-Warten mit Erinnerung, Überspringen, Stoppen, vorhandene Dumps, Installationssperre, gescheiterte Starts und Zeitüberschreitung, Einstellungen je Titel, Neustart-Hinweis, Merkliste der Disc-Spiele); an der Konsole noch nicht erprobt.
- Neue Oberflächentexte in en, es, fr, it, ru

### Übersetzungen
- Die neuen Texte des Update-Reiters (Regeln, Download-Ordner, Diagnose, Andere Versionen, Meldungen der Konsole) sind jetzt auch auf Englisch, Spanisch, Französisch, Italienisch und Russisch da

## v1.74.0

### Spiel-Updates: PKG und fpkg

- Fake-PKGs und fpkgs nehmen jetzt am Update-Prüfvorgang und am direkten Download teil – genau wie Disc- und Store-Spiele und ShadowMountPlus-Titel
- Der direkte Download-Link (CDN-URL) aus der offiziellen Versionsliste wird nach jeder Prüfung je Spiel gespeichert und über **Link** in der Zeile in die Zwischenablage kopiert; damit lassen sich Updates auch ohne die eingebaute Installationsfunktion herunterladen
- Hinweis in der Zeile erklärt, wenn ein Spiel als Fake-PKG installiert ist

### Spiel-Updates: Regeln, Ordner und Diagnose (nach PatchDL 0.3.4)
- **Standardregel** „Updates erlauben/verweigern“ und je Spiel ein eigener Schalter **Updates**; ausgeschaltete Spiele werden nicht geprüft, nicht in „Alle aktualisieren“ aufgenommen und nicht geladen
- **Nach dem Download installieren** als Einstellung für alle und je Spiel; ohne Installation bleibt das Paket „bereit“ und wird mit dem Knopf **Installieren** eingespielt. „Alle aktualisieren“ installiert bereitliegende Pakete nur bei Spielen, bei denen das eingeschaltet ist
- **Prüfsummen-Schalter**: Jedes Stück wird wie bisher mit der offiziellen Prüfsumme (SHA-256) geprüft; ausgeschaltet entfällt die Prüfung, ein schon vorhandenes, unfertiges Paket wird dann neu geladen
- **Download-Ordner** wählbar: interner Speicher oder ein angeschlossenes USB-Laufwerk (Ordner `PS5-Updates`). Schon geladene Pakete bleiben, wo sie liegen, und werden dort weiter gefunden; fehlt das gewählte Laufwerk, geht es in den internen Speicher, mit Hinweis
- **Diagnose** je Spiel (`GET /api/v1/updates/diag?title=`): prüft Update-Liste, Paketliste, die ersten Bytes des ersten Stücks und den freien Platz und liefert einen Bericht zum Kopieren; ohne IP-Adresse und Konsolenname
- Die Einstellungen stehen in `settings.json` (`default_policy`, `title_policy`, `install_after`, `install_after_title`, `verify`, `download_drive`); ältere Dateien gelten weiter

### Spiel-Updates: Andere Versionen und Patches für ältere Firmware (nach PatchDL 0.3.4)
- **Andere Versionen** je Spiel (`GET /api/v1/updates/versions?title=`): die Versionsliste von prosperopatches.com (PS5) bzw. orbispatches.com (PS4), jede Version mit Größe und nötiger Firmware; wählbar sind nur Versionen, die neuer als die installierte sind und auf dieser Firmware laufen. Die Liste wird nur nach einem Klick geholt, nie im Hintergrund, und nur bei eingeschalteten Spiel-Updates
- Das Paket selbst kommt weiter nur von den offiziellen Update-Servern: Nach der Wahl einer Version fügst du einen Link von der Verlaufsseite ein (SC-Link, bei PS4 ein Stück-Link, oder „Copy all links“; Anleitung mit Link zur Seite und QR-Code für das Handy in der Zeile). Die App baut daraus die Adresse der Paketliste im selben Ordner des offiziellen Servers neu zusammen (nur erlaubte Hosts, https, keine `..`, die Titel-ID muss in Ordner und Dateiname stehen) und holt den eingefügten Link nie selbst
- Vorab werden die Paketliste geladen, der freie Platz geprüft und die Größe mit der Liste verglichen; passt sie nicht (Link einer anderen Version), fragt die App mit „Trotzdem starten“ nach. Keine Downgrades
- **Patch für ältere Firmware**: Braucht der neueste offizielle Patch eine neuere Firmware, sucht die Prüfung („Nach Updates suchen“) auf der Verlaufsseite die neueste Version, die hier läuft, und zeigt „älterer Patch passt“ mit Anleitung. Fehlt die Firmware-Angabe der Seite, bleibt es bei „braucht neuere Firmware“ – es wird nie geraten (nur PS5; PS4-Listen kennen keine solche Grenze)
- Neu: `src/upd_patchsite.c/.h` (Verlaufsseiten, Link-Umbau); Antworten der Seiten sind auf 512 KB begrenzt, ihr Text wird auf einfache Zeichen gefiltert. Die Antwortformate der Seiten konnten nicht live geprüft werden (ungetestet an der Konsole)
- Die neuen Texte stehen nur auf Deutsch; in en/es/fr/it/ru fehlen sie

### Fehlerbehebungen
- „Alle aktualisieren“ nahm Fake-PKG-Spiele nicht auf, obwohl die Liste sie mit Update zeigte
- Überläufe beim Lesen fremder Dateien behoben: Längenprüfungen in `lz4pack.c` und `amprlib.c` (Speicherfehler bei fehlgeschlagener Vergrößerung), Schiebezahl im Abbild-Leser `imgread.c`
- Der Arbeitsfaden der Spielstand-Sicherung bekommt einen eigenen, größeren Stapel
- Fehlertexte der Weboberfläche gelangen nur noch als einfacher Text in das Protokoll
- Antworten auf zu lange oder ungültige Adressen kommen im Browser als Meldung an statt als Netzwerkfehler

### PS4 von USB
- Die Suche nach PKG-Dateien nimmt ab Werk die oberste Ebene **und** den Ordner `pkg` jedes Laufwerks (vorher nur die oberste Ebene); wer schon Ordner in den Einstellungen hat, behält seine Liste

## v1.73.1

### PatchDL 0.3.4: Weboberfläche nach Netzwerkwechsel
- Wird das WLAN aus- und wieder eingeschaltet oder das Kabel gezogen, beendet die Konsole mitunter den Zugang der Weboberfläche, obwohl die App weiterläuft. PatchDL 0.3.4 merkt das und öffnet seinen Webserver neu; die App prüft jetzt ebenso alle zehn Sekunden, ob ihr Zugang noch Verbindungen annimmt (ein Verbindungsversuch an die eigene Adresse), und öffnet ihn bei „Verbindung abgelehnt“ von selbst neu. Handbuch (sechs Sprachen): Hinweis im Kapitel „Wenn etwas nicht klappt“

### ShadowMountPlus 1.7beta7
- Auf dem Stand von ShadowMountPlus 1.7beta7 (Quelltext und ELF verglichen mit 1.7beta6-fix2): geändert hat sich nur das Aushängen der Sandbox-Ebenen eines USB-Laufwerks, das mitten im Betrieb abgezogen wurde (kein Hängenbleiben mehr, wenn die Konsole den Zugriff mit „Broken pipe“ oder „Try again“ abweist). Schnittstelle, Backport-Ordner, Konfiguration und APR-EMU-Regeln sind unverändert; an der App war nichts zu ändern
- Die Prüfliste der Fremdbausteine (`tools/upstream.json`) führt jetzt 1.7beta7

## v1.73.0

### Vor dem Packen zu ffpfsc, neues Symbol
- Vor dem Konvertieren zu `.ffpfsc` sieht die App nach, ob das Spiel viele Daten oder Texture-Streaming hat (ab 40 GB Daten, große Streaming-Container wie `.ucas`/`.utoc`/`.pak`/`.forge`/`.streamdb`/`.resources`/`.cas`/`.rpf` ab 4 GB zusammen, oder ein Streaming-Ordner ab 2 GB; Dump-Ordner, `.exfat`, `.ffpkg` und `.ffpfs`, Abbilder ohne Entpacken). Trifft das zu, steht im Fenster ein Hinweis, und beim Start fragt die App, ob das Spiel trotzdem umgewandelt werden soll: „OK“ macht normal weiter, „Abbrechen“ beendet den Vorgang. Der Plan liefert dafür `stream_risk`
- Neues App-Symbol (Oberfläche, Kachel, Starter-Kachel, Benachrichtigung); in den READMEs steht oben stattdessen das Hintergrundbild der App
### Disc im Laufwerk (nach ps-disc-detect)
- Neuer Reiter **Disc** auf der Spiele-Seite: zeigt, welche Disc im Laufwerk liegt (PS2, PS3, PS4, PS5), mit Profil und Titelkennung wie bei der Konsole; aus ab Werk, die App fragt das Laufwerk erst nach dem Einschalten (alle 1,5 s)
- Von einer PS2-DVD oder PS3-Disc legt der Knopf „Abbild erstellen“ ein ISO in `/data/PS5-Cooling-Center/disc/` an (Fortschritt, Geschwindigkeit, Restzeit, Abbrechen; CRC-32, MD5, SHA-1; Platzprüfung, Ruhemodus-Sperre, Eintrag im Verlauf); ein zweiter Schalter sichert neue Discs automatisch (aus ab Werk)
- Mit einer Redump-Liste unter `/data/redump.db` (gebaut mit `tools/build_redump_db.py` aus ps-disc-detect) bekommt das Abbild den Namen aus der Liste und wird gegen sie geprüft
- PS4- und PS5-Discs werden nur erkannt; PS1-/PS2-CDs kann das Laufwerk der PS5 nicht lesen
- Schnittstellen `GET`/`POST /api/v1/disc`; Handbuch (sechs Sprachen), FAQ, Wörterbücher ergänzt
- `sqlite_ro.c` liest jetzt auch WITHOUT-ROWID-Tabellen (Index-Bäume)

### Fremdbausteine auf dem neuesten Stand (21.10.2026)
- **PS4Mount 0.6.1** (gasb07): Ein Laufwerk, das nach einem Neustart unter anderem Namen wiederkommt (`usb1` statt `usb0`), behält seine angemeldeten PS4-Spiele; die Verknüpfungen werden nur umgeschrieben. Handbuch: Hinweis, keine zusammengeführten PKGs zu verwenden
- **PatchDL 0.3.3** (Knutwurst): Schreibblöcke der Spiel-Updates 2 MiB statt 1 MiB, Empfangspuffer 4 MiB angefragt (die Konsole begrenzt auf ihr Maximum)
- Geprüft und schon aktuell: APR-EMU 0.4.2.1, PlayGo-Stub 0.6, DLC-Module 0.3, Payload Manager 0.6.3, SDK 0.44, libdeflate 1.26, LZ4 1.10.0, exFAT2AMPR 1.104. MkPFS 1.1.0: nimmt ISA-L nicht mehr automatisch, weil dessen Datenströme vom Hardware-Entpacker der PS5 für manche Spiele abgelehnt werden (Fehler 132); die App nutzt libdeflate, keine Änderung nötig

### ShadowMountPlus 1.7beta6-fix2
- Backport-Ordner: ShadowMountPlus nimmt seit 1.7beta6 unter `<Scanpfad>/backports/` auch einen Ordner, dessen Name nur mit der Titel-ID beginnt (etwa `PPSA12345 Spielname`): der genau so benannte gewinnt, sonst der erste in Byte-Reihenfolge. Die Marke „Backport“, APR-EMU und PlayGo (Ablage, Prüfung, Entfernen) erkennen diese Ordner jetzt ebenfalls
- Handbuch (alle sechs Sprachen), Repo-Handbuch und Kommentare auf den Stand von 1.7beta6-fix2 gebracht; neu in dieser Fassung außerdem: laufende `FAKE*`/`LAPY*`-Anwendungen blockieren Suche, Kopieren, Verschieben und Löschen anderer Titel nicht mehr, und die globale fakelib lässt sich je Spiel ausschließen (`ignore_global_fakelib`, von der App weder gelesen noch geändert)

### Updates
- Nur noch der Direktdownload: Der SC-Link-Weg samt „Versionen …“, „Älteren Patch holen …“, QR-Code und der Abfrage von Prospero/Orbis Patches ist entfernt (Schnittstellen `/api/v1/updates/versions` und `/api/v1/updates/qr` und die Aktionen `link`/`forget_link` entfallen)
- Die Adresse der Versionsliste wird zuerst aus der `sce_sys/param.json` des Spiels gelesen (Abbild `.exfat`/`.ffpkg`/`.ffpfs`/`.ffpfsc` oder Ordner, ohne Entpacken), sonst aus `AppInfoJson` der App-Datenbank, sonst aus `tbl_appbrowse`
- Die Abbild-Leser kennen jetzt auch `.ffpfs` (unkomprimierter PFS-Container)
- Bei Spielen über ShadowMountPlus zählt die Version aus dem Abbild, wenn sie höher ist als die der App-Datenbank
- Der Name des Herstellers kommt in Oberfläche, Handbuch, FAQ und Dokumentation nicht mehr vor

## v1.72.0

### Updates: Versionsauswahl für alle Spiele
- Im Reiter „Updates“ gibt es jetzt bei jedem Spiel (nicht nur bei „neuere Firmware nötig“) die Schaltfläche „Versionen …“ mit der Liste der Versionen
- Die neueste zur Firmware passende Version ist als „✓ direkt laden“ markiert und startet den Download direkt von den offiziellen Servern
- Nach dem Vorbild von PatchDL 0.3.3 (Knutwurst)

### Fehler
- Apps wie YouTube und Homebrew-Werkzeuge erscheinen nicht mehr im Reiter „Updates“ – geprüft werden nur noch Titel, die auch auf der Spiele-Seite stehen

## v1.71.4

### Fehler
- „Ausschalten“ in den Power-Optionen schaltet die Konsole jetzt wirklich aus: Der bisherige Aufruf folgte der Ruhemodus-Einstellung der PS5 und schickte sie in den Ruhemodus. Die App nutzt nun den direkten Ausschalt-Aufruf des Systems (mit dem alten als Rückfall, falls die Firmware ihn nicht bietet)

## v1.71.3

### Fehler
- Updates: Ein Titel, der gerade gelöscht wird, kommt nicht mehr aus der Warteschlange wieder an die Reihe
- Payload-Profile und Startprofil nehmen jetzt auch Dateien mit der Endung `.bin` an; die Startmarke eines früheren Starts wird nur noch entfernt, wenn sie älter als der aktuelle Programmstart ist
- Payloads herunterladen: Ein zweites „Aktualisieren“ während die Liste noch lädt meldet jetzt „Die Liste wird gerade geladen.“ statt zu kollidieren; beim Löschen wird die Prüfsumme verglichen – eine inzwischen geänderte Datei bleibt liegen, nur der Eintrag verschwindet
- Temperaturprotokoll schreibt im Hintergrund (kein Warten mehr auf das Laufwerk); doppelte `Content-Length`-Kopfzeilen werden mit 400 abgelehnt; Schutz gegen zweite App-Instanz auf dem Port verbessert; Löschen und Dateimanager prüfen Pfade und Änderungszeiten strenger

### Verbesserungen
- Bessere Kompatibilität mit älteren Browsern (PS5-Browser): Stilregeln mit `:hover` und `:focus-visible` sowie `:has()` sind getrennt, sodass ein nicht unterstütztes Selektorstück nicht mehr die ganze Regel verwirft
- Benutzerhandbuch und FAQ (App, HTML, PDF, alle sechs Sprachen) sind vollständig auf dem Stand: Updates für SMP-Spiele, eingeklappte Karte „Doppelte Abbilder“, `.bin`-Hinweis bei Payloads, neue FAQ zum Ruckeln beim Scrollen; die Handbuch-Bilder liegen wieder im Projekt (`docs/shots/`)
- Dokumentation (HANDBUCH, API, ENTWICKLUNG) bereinigt: kein Installer mehr, neue Routen ergänzt

## v1.71.2

### Verbesserungen
- Das Scrollen in der Web-Oberfläche ruckelt im Design „Standard“ im PS5-Browser weniger: Der Hintergrund ist jetzt einfarbig statt eines Musterbilds, und während des Scrollens pausieren die regelmäßigen Aktualisierungen kurz (danach holen sie auf)
- Die Oberfläche fragt die Laufwerkstemperaturen nicht mehr ab, wenn diese Messung ausgeschaltet ist; damit entfällt die ständige Fehlermeldung „409 Conflict“ in der Browser-Konsole

## v1.71.1

### Fehler
- Die App startete nach 1.71.0 nicht richtig: Die Seite blieb leer und keine Schaltfläche reagierte (falsche Anführungszeichen in der Karte „Doppelte Abbilder“ brachen die Oberfläche beim Start ab); behoben, zusätzlich stürzt ein fehlendes Element die Oberfläche nicht mehr ab

## v1.71.0

### Updates für SMP-Spiele
- Spiele, die über ShadowMountPlus laufen (exFAT- oder ffpkg-Abbilder), werden jetzt auf Updates geprüft und können heruntergeladen und installiert werden – genau wie Disc- und Store-Spiele
- Unter jedem SMP-Spiel mit Update steht ein Hinweis, dass das Update normal installiert wird; ob es auf das eingehängte Abbild wirkt, hängt vom Spiel ab
- Fake-PKGs bleiben weiterhin ausgeschlossen (keine offiziellen Retail-Updates möglich)

### Doppelte Abbilder
- Die Karte „Doppelte Abbilder" auf der Spiele-Seite startet jetzt eingeklappt; ein Klick auf die Kopfzeile klappt die Liste auf und zu
- Der Zustand (auf-/zugeklappt) wird innerhalb der Sitzung gespeichert
- „Neu suchen" funktioniert weiterhin ohne Aufklappen

## v1.70.0

### Payloads herunterladen (nach dem Payload Manager 0.6.3 von itsPLK)
- **Neuer Bereich auf der Payloads-Seite:** die Liste des Payload Managers von itsPLK (`ps5-payloads-mirror`, jeden Tag aktualisiert, zurzeit 28 Payloads: ShadowMountPlus, etaHEN, kstuff-lite, ftpsrv, elfldr, ps5upload, PKG-Manager u. v. m.) mit Suche und Kategorien. Herunterladen, Aktualisieren, Starten und Löschen mit einem Klick
- Jede Datei wird vor dem Speichern mit der **SHA-256-Prüfsumme** aus der Liste verglichen; eine abweichende wird verworfen. Gespeichert wird im Payload-Ordner der App, wo Profile und Startprofil sie finden
- **Updates erkannt** wie im Payload Manager (Versionsvergleich, bei gleicher Version die Prüfsumme): die Karte zeigt „alt → neu“ und „Aktualisieren“; die alte Datei wird ersetzt
- Eine Datei gleichen Namens, die nicht aus der Liste stammt, wird erst nach einem zweiten Klick ersetzt; gelöscht wird nur, was die App selbst geladen hat
- **Ins Internet nur auf Wunsch:** Die Liste lädt erst „Liste laden“; danach wird sie beim Öffnen der Seite höchstens alle drei Stunden erneuert. Nur GitHub (https, feste Wurzelzertifikate)
- Payloads mit der Endung `.bin` (etaHEN) lassen sich jetzt wie `.elf` starten, mit derselben Prüfung des Inhalts

### Spiel-Updates (nach PatchDL 0.3.0 von Knutwurst)
- **Neuer Reiter „Updates“ auf der Spiele-Seite:** lädt das offizielle Update jedes PS5- und PS4-Spiels direkt von den offiziellen Servern und installiert es. Nur für Spiele, die offiziell signiert sind (Disc, PlayStation Store); Spiele über ShadowMountPlus, Fake-PKGs und Homebrew werden nicht angefragt. **Aus**, bis man den Schalter umlegt
- **Passend zur Firmware:** Angeboten wird die neueste Version, die auf dieser Firmware läuft. Braucht das neueste offizielle Update eine neuere Firmware, sagt das die Karte
- **Warteschlange, Fortsetzen, Prüfsummen:** Ein Update nach dem anderen, 1 bis 16 Verbindungen; Anhalten und Fortsetzen auch nach einem Neustart der App; jedes Stück wird mit der offiziellen Prüfsumme verglichen (PS5 SHA-256, PS4 SHA-1), ein falsches neu geladen; der Fortschritt steht live auf der Seite
- **Aufräumen:** Das Paket in `/data/PS5-Cooling-Center/updates` wird gelöscht, sobald das Spiel nach der Installation die neue Version meldet (abschaltbar); die Liste der Pakete im Ordner steht in den Einstellungen des Reiters. Ein schon vorhandenes Paket der richtigen Größe wird Stück für Stück geprüft, nur falsche Stücke werden neu geladen
- **DNS wie PatchDL:** eigene Anfragen an Cloudflare, Google und Quad9 (UDP, dann TCP, dann DNS über HTTPS, zuletzt die gemerkte Adresse); die DNS-Einstellungen der Konsole bleiben unverändert. TLS mit BearSSL 0.6 (MIT) und festen Wurzelzertifikaten: das der Download-Server und öffentliche für DNS über HTTPS
- **Schneller wie PatchDL 0.3.1:** Was ankommt, wird in Blöcken von 1 MiB geschrieben statt in vielen kleinen Stücken, und jede Verbindung bekommt einen Empfangspuffer von 2 MiB
- **Sicherheit:** Jedes Stück wird immer mit seiner Prüfsumme verglichen (eine Paketliste ohne Prüfsummen wird abgelehnt), und vor der Installation muss das geladene Paket offiziell signiert sein; eine Antwort, die ohne sauberes TLS-Ende abbricht, gilt als unvollständig
- **Stabiler:** Das Warten auf die Installation endet nie mehr in einer Endlosschleife (Ergebnisse werden je Auftrag aufbewahrt, Höchstdauer 6 Stunden), `job.json` wird nie von zwei Verbindungen zugleich geschrieben, Ausschalten leert die Warteschlange, „Fortsetzen“ klappt auch direkt nach einem Neustart der App, während der Installation lässt sich nichts anhalten oder löschen
- Neue Schnittstellen `/api/v1/updates`, `/api/v1/updates/packages`; Handbuch in allen sechs Sprachen ergänzt
- Die Oberfläche nennt keinen Herstellernamen mehr („offizielle Update-Server“, „offiziell signiert“)

### Stabilität und Tempo (Durchsicht 19.10.2026: vier Prüf-Agenten über das ganze Projekt)
- **Oberfläche flüssiger:** Die Statusabfrage zeichnet jede Sekunde nur noch, was auf der sichtbaren Seite steht (vorher auch Kerne, Sensoren, Diagramme und Diagnose der anderen Seiten), und schreibt Texte und Listen nur, wenn sie sich geändert haben. Die Systemabfrage fragt Controller, Kanäle und Laufwerke nur noch auf der Systemseite ab
- **Seiten-Dateien werden zwischengespeichert:** Oberfläche, Skripte und Wörterbücher bekommen ein ETag; der Browser lädt sie nur neu, wenn sich die App geändert hat (vorher bei jedem Aufruf, das Wörterbuch allein 0,4–0,5 MB)
- **Controller:** Fenster (Kopieren, Löschen, Dateimanager) holen den Fokus, geben ihn beim Schließen zurück und schließen mit der Kreis-Taste/Escape; L1/R1 blättert hinter einem offenen Fenster nicht mehr um
- **Pakete:** Während einer Suche wird nur noch die Liste abgefragt und nur bei Änderung neu gebaut (vorher jede Sekunde vier Abfragen und alle Karten)
- **Fehler behoben:** schlummernder Stapelüberlauf beim Lesen der Netzwerkdaten (Puffer jetzt doppelt so groß wie die Struktur des Systems); zwei Abbild-/LZ4-/AMPR-Aufträge konnten im selben Augenblick starten; ein fragmentiertes exFAT-Abbild wurde beim Prüfen und Entpacken quadratisch langsam gelesen; die Abfrage bei ShadowMountPlus hielt bis zu 5,5 s eine Sperre, auf die andere Seiten warteten; nach Verschieben/Entpacken/Löschen konnten Aufträge mit der alten Spieleliste geplant werden; Installationsergebnis konnte von einer gleichzeitig startenden Installation überschrieben werden; ein Update-Paket konnte gelöscht werden, während es installiert wurde; fehlende Speicherprüfung in PS4Mount; der Lüfter-Thread nahm nach einer langen Pause mitten in einer Kachel-Installation seine Identität zurück; Fehlermeldungen bei zu großen Anfragen kamen als Verbindungsabbruch an; Import der Einstellungen übernahm den Schalter „Covers & Metadaten speichern“ erst später; ein Schritt eines Payload-Profils scheiterte, wenn gleichzeitig etwas kopiert wurde
- **Robuster:** langsame Clients belegen keine Verbindungsplätze mehr unbegrenzt (Mindesttempo 64 KiB je 30 s); Löschen großer Spiele gibt regelmäßig Rechenzeit ab; die Sicherungsliste misst Ordnergrößen nicht mehr bei jeder Anfrage neu; Kanalaufzeichnung, Prozessliste und das Alter von Leistungs- und Taktwerten rechnen mit der monotonen Uhr; ein Installationshelfer, der schon beim Start hängt, wird gefunden und beendet statt bei jedem neuen Versuch liegen zu bleiben; Löschen-Dialog verwirft verspätete Antworten eines vorher geöffneten Dialogs; der Reiter „Updates“ aktualisiert sich nach einem Seitenwechsel wieder; die Verbindungsanzeige wird nach einem Ausfall sicher wieder grün
- **cJSON:** Schutz vor NULL in `cJSON_SetNumberHelper` (upstream b2890c8, noch in keinem Release)

### Auslieferung
- **Kein Kachel-Installer und kein Kachel-Paket mehr:** Die App legt ihre Kachel seit 1.47.0 selbst an; `cooling-center-launcher-installer_v….elf` und `IV9999-PSCC69690_00-….pkg` werden nicht mehr gebaut und nicht mehr ausgeliefert. Die Veröffentlichung besteht aus der App, `PPSA86921.zip` (Starter-Kachel) und der LIESMICH

### Pakete
- **Rückportier-Patches erkannt** (nach ps5-pkg-manager 1.5.1): Ein PS5-Paket mit dem Kopf `LIH`, das früher als „Unbekanntes Paket“ übersprungen wurde, wird gelesen und als **Update** gezeigt, ebenso ein Paket, dessen Inhaltsmarke (`0x40000000` oder `0x00100000`) es als Patch ausweist, auch wenn sonst nichts im Paket darauf hindeutet
- **SDK-Ziel:** Bei einem PS5-Paket steht jetzt „SDK 5.xx“ (o. ä.) als leise Marke auf der Karte, gelesen aus den Prozessparametern des Hauptprogramms. Es wird nur gelesen, wenn das Programm unverschlüsselt und unkomprimiert im Paket liegt und eindeutig ist (höchstens 16 Blöcke zu 8 KiB und 24 Byte am Ende); sonst steht keine Marke da, und das Paket bleibt trotzdem lesbar. Geprüft mit künstlichen Paketen (FIH, LIH, Versatz über 4 GiB, beschädigte Felder) unter ASan/UBSan und mit 300 beschädigten Abbildern

### PS4-Spiele von USB (PS4Mount 0.6.0)
- Neuer Schalter **„Neue Spiele schneller anmelden“** in den Einstellungen (**aus**): Die Anmeldung der Konsole geht sonst jeden Ordner in `/user/app` durch, etwa eine Dreiviertelsekunde je Spiel der Bibliothek, auch für schon angemeldete. Mit dem Schalter sieht sie nur die neuen Spiele (bis zu zwei Minuten, nur wenn kein Spiel läuft); danach sind wieder alle da. Hinzufügen, Entfernen und Zurückverschieben warten in dieser Zeit, und ein Neustart der App räumt eine liegen gebliebene Einbindung weg. Noch nicht an der Konsole erprobt, deshalb aus

### Spiele (nach exFAT2AMPR 1.104 von V-10)
- **Doppelte Abbilder:** Liegt ein Spiel in mehr als einem Abbild auf den Laufwerken (oberste Ebene und eine Ordnerebene darunter, dazu `homebrew` und `etaHEN/games`), zeigt die Spiele-Seite eine Karte „Doppelte Abbilder“ mit allen Quellen; die Quelle, die ShadowMountPlus gerade nimmt, ist markiert. **Beiseitelegen** benennt ein Abbild in `….off` um, ShadowMountPlus nimmt es dann nicht mehr (zwei Quellen ließen es sonst bei jeder Suche umhängen); **Zurückholen** macht es rückgängig. Gelöscht wird nichts. Ein eingehängtes Abbild oder eines, dessen Spiel läuft, lässt sich nicht beiseitelegen. Welches Spiel ein Abbild enthält, liest die App aus dem Abbild selbst (`param.json`/`param.sfo`), ohne es einzuhängen

### LZ4-Packer
- **Dauer vorab:** Das Packfenster nennt, wie lange Packen und Prüfen etwa dauern; bei Stufe 10 bis 12 auch, wie lange Stufe 9 bräuchte (die Stufen 10–12 sparen meist weniger als ein Prozent Platz). Zuerst sind es Schätzwerte (die von exFAT2AMPR 1.104), nach jedem Packen lernt die App die Geschwindigkeit dieser Konsole (`lz4-speed.txt` im Datenordner)
- **Verknüpfungen werden abgelehnt:** Liegt im Spielordner eine Verknüpfung oder etwas, das weder Datei noch Ordner ist, oder lässt sich ein Ordner nicht ganz lesen, beginnt das Packen gar nicht erst und nennt die Stelle. Vorher wurden Verknüpfungen still übergangen; in einer gepackten Kopie fehlten sie dann
- **Startprüfung:** Bei einem gepackten Spiel warnt die Zeile „LZ4-Asset-Packs“, wenn `eboot.bin` nicht ausgeführt werden darf oder ShadowMountPlus für das Spiel keine fakelib einbindet (fakelib-Modus „Deaktiviert“ seit ShadowMountPlus 1.7beta5, oder global ausgeschaltet): Dann fehlt beim Start die APR-EMU, die die Pakete liest. Die App fragt dafür nur die Spieleliste von ShadowMountPlus ab, die sie ohnehin liest
- Die Paketverzeichnisse von Remedys Spielen (`*.rmdtoc`) bleiben jetzt immer lose, gleich mit welchem Profil: Alan Wake 2 öffnet sie ohne APR und bricht gepackt mit „No packfiles found!“ ab (gefunden mit MkPFS.CSharp 2.5.0 von danthespal; die `.rmdblob`-Daten bleiben gepackt). Schon gepackte Spiele holt „Pakete prüfen“ wie die anderen Marker-Dateien auf die Platte zurück (unter „Repariert“)

### Starter-Kachel
- `sce_sys/param.json` der Starter-Kachel (`PPSA86921`) erklärt der Konsole jetzt, dass die App HDR ausgibt und kein VRR braucht (`attribute` `0x62000000`, `attribute3` `0x100000`, die Werte der Vorlage ps5-native-app-boilerplate seit deren Änderung vom 11.10.2026). Mit `0` und `0` schaltet die Konsole bei einem Fernseher mit VRR und HDR „wenn unterstützt“ beim Start der Kachel und noch einmal beim Ende die Bildausgabe um, und der Bildschirm bleibt ein bis zwei Sekunden schwarz. An dieser Konsole noch nicht geprüft; zu ersetzen genügt die `param.json` im Ordner `/data/homebrew/PPSA86921/sce_sys/`

### Werkzeuge
- Gebaut wird jetzt mit dem **PS5-Payload-SDK v0.44** (vorher v0.43; neu im SDK: Stub-Bibliotheken `libSceAgc` und `libSceAgcDriver`, `killpg()`, Verbesserungen der Werkzeugkette). Die ELF ist bis auf die zufällige 16-Byte-Build-ID dieselbe wie mit v0.43; das Archiv stimmt mit dem offiziellen Release-Anhang überein (SHA-256 `4d06eab7…`)

---

## v1.69.1

### Oberfläche
- **Flüssiges Scrollen** (Stick am Fernseher) in „Standard“, „Hell“ und „Dunkel“, nicht nur in „Optimal“. Gefunden mit fünf Testfassungen von „Standard“ an der Konsole: Bremsen waren der weiche Leuchtrand der Karten (Schatten mit 20 Pixel Unschärfe) und die Textschatten. Der Leuchtrand besteht jetzt aus drei feinen Ringen ohne Unschärfe (in „Hell“ der Schatten unter den Karten aus feinen Kanten), der allgemeine Textschatten entfällt (auf den dunklen Karten war er nicht zu sehen), in der Seitenleiste bleibt ein kleiner statt drei großer. „Hell“ hat deckende Karten und einen flachen Hintergrund im selben Farbton, „Dunkel“ ebenfalls einen flachen Hintergrund. Außerdem liegt die klebende Seitenleiste auf einer eigenen Ebene. Das Aussehen ändert sich kaum; an der Konsole in allen vier Designs bestätigt
- **Flackern der Spiele-Seite alle paar Sekunden behoben:** Die Lüfterfarbe (`--fan-rgb`) stand am Wurzelelement der ganzen Seite. Im Spiel ändert sich die Drehzahl alle paar Sekunden um einen Prozentpunkt, und jede Änderung ließ die Stilberechnung des gesamten Dokuments von vorn beginnen, auf jeder Seite. Die Farbe steht jetzt nur noch auf der Kühlungsseite, wo allein sie gebraucht wird; das Aussehen ist unverändert
- Die Suche in der Spieleliste zeichnet das Raster erst nach einer kurzen Eingabepause neu statt bei jedem Buchstaben
- **Listen werden nur noch bei einer Änderung neu gebaut:** Die Spieleliste wurde jede Minute komplett neu gezeichnet (alle Karten samt Bildern), die Spielregeln alle 15 Sekunden, das Protokoll alle 5 Sekunden und das Live-Protokoll alle 2 Sekunden, auch wenn sich nichts geändert hatte. Das ruckelte beim Scrollen und nahm einem halb bestätigten Knopf („Wirklich? Nochmal klicken“) die Bestätigung
- **Profilbilder:** Die Auswahl zeigt Vorschaubilder mit 132 statt 440 Pixel (rund 0,2 statt 1,3 MB, 25-mal weniger Bildpunkte zum Dekodieren); ein Klick lädt weiter die volle Datei

### Starter-Kachel
- Die Starter-Kachel (`PPSA86921`) läuft nach dem Tipp einige Sekunden als eigener Titel, bis sie die App übergeben hat. Die Spielerkennung hielt sie dabei für ein Spiel: Sie stand unter „Läuft gerade“, begann und beendete eine Sitzung der Spielzeit (ein gleichzeitig laufendes Spiel wurde dabei in zwei Sitzungen zerteilt), und ihre Sekunden bei Leerlauf-Drehzahl hätten als Mindestdrehzahl der Konsole im Spiel gelernt werden können. Sie zählt jetzt nicht mehr als Spiel; ein Spiel, das daneben läuft, bleibt in dieser Zeit das Spiel
- `PPSA86921.zip` enthält die App von v1.69.1; den Ordner in `/data/homebrew/` ersetzen (die App steckt in der `eboot.bin` der Kachel, sonst startet die Kachel die alte Fassung)

---

## v1.69.0

### Lüftersteuerung
- **Nachtmodus:** Zwischen zwei Uhrzeiten gilt ein leiseres Nachtziel (neue Karte auf der Kühlungsseite, unter „Betriebsart“). Die Uhrzeit ist die des Geräts, auf dem die Seite offen ist; eine eigene Spielregel geht vor. An der Konsole geprüft, auch im Spiel: Der Lüfter fiel auf das Minimum der Konsole, der heißeste Sensor lag etwa 3 °C höher
- **Lüfter-Selbsttest:** Der Knopf „Lüfter jetzt testen“ in der Lüfterdiagnose (Expertenansicht) prüft, ob der Lüfter auf die Schwelle antwortet, auch ohne dass die Konsole heiß wird (höchstens 30 Sekunden, nur bei kühler Konsole, jederzeit abbrechbar). An der Konsole: Drehzahl von 20 % auf 54 % in 6 Sekunden
- **Mindestdrehzahl der Konsole** wird gelernt (20 % im Leerlauf, 34 % im Spiel) und in der Lüfterdiagnose angezeigt; die Soll-Ist-Abweichung rechnet damit
- **Vorhalt bei hohem Ziel** (ab 85 °C): Bei deutlich steigender Temperatur liest die Regelung das Band etwas voraus; der Status zeigt ihn als `control.lead_c10`
- **Sauberes Beenden:** Bei SIGTERM und SIGINT gibt die App die Lüftersteuerung an die Konsole zurück, bevor sie endet (an der Konsole geprüft: danach hält sie 91 °C)
- Die wirkungslose **„Lüfterkurve (Experte)“** ist aus den Einstellungen entfernt; sie wurde gespeichert, aber nie von der Regelung benutzt (Export und Import ändern sich nicht)
- Die Lüfterdiagnose rät bei einer kühlen Konsole nicht mehr, das Ziel zu senken, nur weil die Schwelle in ihrer Ruhelage („oberer Anschlag“) steht; der Rat erscheint erst, wenn die Temperatur sich dem Ziel nähert
- Fällt nur **ein** Temperatursensor aus, gilt sein letzter Wert bis zu 10 Sekunden weiter, statt den Durchschnitt um den Abstand der beiden Sensoren zu senken; längeres Ausbleiben des Prozessor-Sensors steht im Protokoll
- **Fehler behoben:** Die Lüfterschleife rechnete alle Intervalle auf der Wanduhr; ein Zeitsprung (Zeitabgleich kurz nach dem Start) löste alle Takte auf einmal und eine falsche „Pause erkannt“-Meldung aus. Jetzt laufen die Regel-Intervalle auf der monotonen Uhr, nur die Erkennung langer Pausen (Ruhemodus) auf der Wanduhr
- **Fehler behoben:** Verließ man die Automatik während der Notfallregelung, blieben „Sicherheit“, rote Lightbar und das Warn-Kennzeichen der Spielzeit stehen
- **Fehler behoben:** Der Warn-Countdown richtete sich nach den rohen Warnwerten statt nach der Grenze, die die Warnung selbst anwendet (bei Ziel 91 stand dauerhaft „noch 0 s“)

### Oberfläche und Dokumentation
- Handbuch in allen sechs Sprachen um Nachtmodus, Lüfter-Selbsttest und Mindestdrehzahl ergänzt
- Rund 120 bisher deutsche Texte sind jetzt auch auf Englisch, Spanisch, Französisch, Italienisch und Russisch da
- Credits-Seite: Dank und Verweise für die Emulator-Bausteine (ampr_emu, pgo_stub, dlc_emu) sowie für LZ4 Compression, webhb und BlackBearReloaded
- Die Behauptung, Handbuch und FAQ lägen dem Release als PDF bei, ist aus den Anleitungen entfernt (das Release enthält keine PDFs)
- Die READMEs nennen Nachtmodus und Selbsttest in der Funktionsliste
- API: `control.night_active`, `fan.floor_pct`/`floor_idle_pct`/`floor_game_pct`, `control.lead_c10`/`lead_max_c10`, `/api/v1/fan/selftest` und die Felder `night_*` der Konfiguration (siehe `docs/API.md`)

### Stabilität und Fehler
- Der Zeitstempel im Status (Diagramm, „Zeitpunkt“) und die Spielzeit-Aufzeichnung der Temperaturen bleiben Wanduhrzeit
- Das Startprofil läuft pro Start der App einmal, nicht nach jedem erneuten Binden des Ports (Ruhemodus)
- Der Temperaturverlauf wird bei noch nicht gesetzter Uhr nicht mehr verworfen; der Auftragsverlauf überlebt einen Nullblock nach Stromausfall
- Spiele löschen und kopieren: eigene Stackgröße der Arbeitsfäden, die Verzeichnisrekursion braucht pro Ebene nur noch wenige Byte
- Bei Speichermangel schloss die LZ4-Paketprüfung zufällige Dateideskriptoren; die Übersichtsliste der Titelbilder (Zwischenspeicher) verdrängt bei voller Tabelle den ältesten Eintrag, statt neue Titel nie zu speichern
- Der Mitschnitt startet bei einem Doppelklick nicht mehr zwei Aufträge; eine nicht lesbare Konfigurationsdatei wird nicht mehr durch einen alten Stand ersetzt
- Oberfläche: korrekte Anzeige im Modus „Beobachten“ ohne gesetzte Schwelle, Seitenwechsel nach dem Sprachwechsel lädt wieder seine Daten, Bestätigungsklicks mit 800 ms Sperre gegen Doppelklick, Abbrechen-Knöpfe bleiben nach einem Fehler bedienbar, Paketliste bleibt bei einer fehlgeschlagenen Nebenabfrage stehen, ungespeicherte Haken der Zusatzabfragen springen nicht zurück

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.69.0; den Ordner in `/data/homebrew/` ersetzen

---

## v1.68.1

### Design „Standard“
- Das Design **„Aktuell“** heißt jetzt **„Standard“**; wer es gewählt hatte, behält es automatisch
- Die Kacheln sind darin wieder **deckend** (ohne Durchscheinen)

### Nie zwei PS4Mount gleichzeitig
- Läuft PS4Mount schon als eigenes Payload (`ps4mount.elf`), lädt die App ihr eingebautes PS4Mount **nicht ein zweites Mal**: Der Reiter „PS4 von USB“ zeigt „wartet“ samt Grund, und sobald das eigenständige beendet ist, übernimmt die App von selbst
- Erkannt wird es an seinem Web-Panel (Port 8770), an der Sperre auf Port 9032 oder am Prozessnamen; taucht es später doch auf (etwa nach dem Ruhemodus), hält die App ihres an, statt mit ihm um dieselben Spiele zu streiten
- Umgekehrt startet die Seite **Payloads** (auch ein Startprofil) keine `ps4mount.elf`, solange schon ein PS4Mount läuft – erkannt am Inhalt der Datei, nicht am Namen

### PS4-Spiele von USB (PS4Mount 0.5.1)
- Jede Zeile im **Protokoll von PS4Mount** beginnt jetzt mit Datum und Uhrzeit auf die Millisekunde, wie in PS4Mount 0.5.1 (z. B. `2026-10-15 18:08:35.284`)
- Eine Zeile, die in mehreren Teilen geschrieben wird, bekommt einen einzigen Stempel
- Die Korrekturen aus dem Pull Request #18 bleiben erhalten (in 0.5.1 sind sie noch nicht enthalten)

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.68.1; den Ordner in `/data/homebrew/` ersetzen

---

## v1.68.0

### Vier Designs
- Neuer Wähler **„Design“** oben neben der Sprache: **Aktuell**, **Hell**, **Dunkel** und **Optimal**. Umschalten ohne Neuladen; jedes Gerät merkt sich seine Wahl (z. B. „Optimal“ am Fernseher, „Hell“ am Handy)
- **Hell**: gedämpftes Grau-Blau statt Weiß, blendet nicht; Kacheln mit Bild bleiben dunkel, damit ihre Schrift lesbar bleibt
- **Dunkel**: fast schwarz, ohne Leuchten und ohne Durchscheinen
- **Optimal**: für Fernseher und PS5 – kräftiger Kontrast, größere Schrift, dickere Linien, ohne Durchscheinen und Schatten (am schnellsten gezeichnet)

### Neue Kachel-Optik
- Alle Kacheln mit leuchtendem Rand und einer Symbol-Kachel neben dem Titel (eigenes Symbol je Kachel); die Legende steht als Pille daneben
- Diagramme (Temperaturverlauf, Langzeitverlauf, Lüfterdrehzahl) in einem eigenen Innenfeld, Flächen mit Farbverlauf
- Im Design „Aktuell“ sind die Kacheln durchscheinender (72 % statt 86 %)

### LZ4-Asset-Packs (nach PS5 LZ4 Compression 1.5.0)
- Alle Dateien eines gepackten oder kopierten Spiels bekommen die Rechte 0777 (mit 0666 stürzten manche Spiele ab)
- Marker-Dateien (PlayGo-Platzhalter, `playgo.pgm`, Startdateien von Ghost of Yōtei) bleiben immer lose auf der Platte
- **„Pakete prüfen“ repariert** jetzt auch: das passende APR-EMU, fehlende Dateien aus den Paketen zurück auf die Platte, Rechte im ganzen Ordner
- Vier getestete Profile eingebaut (Astro Bot, UFC 6, Spiele von Insomniac, Unity-Spiele); das passende wird vorgeschlagen, ein eigenes Profil geht vor

### PS4-Spiele von USB (PS4Mount 0.5.0)
- **„Zurückverschieben“**: PKGs zurück in ihren Ursprungsordner, einzeln oder als Auswahl in Gruppen; ein laufendes Spiel wird vorher geschlossen
- Korrekturen aus dem Pull Request #18 (EzioRamesha): eine beschädigte PKG kann die App nicht mehr zum Absturz bringen, bei Speichermangel wird nichts halb geschrieben, ein beiseitegelegtes Update wird nicht überschrieben
- Fest auf der Konsole installierte Spiele werden nie entfernt; während PS4Mount arbeitet, bleibt die Seite bedienbar („arbeitet gerade …“)

### DLC-Module
- Mitgeliefert ist jetzt die Fassung **„log“** von dlc_emu 0.3: Die bisherige Fassung ohne Protokoll stürzt laut Autor bei jedem Spielstart ab
- Liegt die alte Fassung in einem Spiel, zeigt „Spiel-Plugins“ sie als **fehlerhaft**; „Aktualisieren“ ersetzt sie (die alte Datei bleibt als `.old`)

### Stabilität und Sicherheit
- Weboberfläche: zu tief verschachtelte Anfragen, falsche Größenangaben und überlange Adressen werden abgewiesen; Fehler der Konsole stehen im Protokoll
- **Spielstände löschen** nur noch, wenn vorher ein Laufwerk für die Sicherung gewählt ist
- Hochladen: ein stockender Upload wird beendet; eine Datei gilt erst als gespeichert, wenn sie auf dem Laufwerk ist
- Verschieben zwischen Laufwerken löscht im Original nur, was nachweislich kopiert ist
- Eine unlesbare `config.json` wird beiseitegelegt statt überschrieben
- Fehlt auf einer Firmware eine Systemfunktion, startet die App trotzdem; die Sammelinstallation ist unter Firmware 6.00 gesperrt
- ShadowMountPlus ab 1.7-beta4: installierte Pakete werden erkannt (Verschieben und Entpacken nur für Ordner und Abbilder)

### Fehlersuche und Bedienung
- Fehlermeldungen auf Deutsch (z. B. „Auf dem Laufwerk ist kein Platz mehr frei“), die Worte des Systems stehen in Klammern dahinter
- Fehlermeldungen bleiben länger stehen; „Keine Verbindung zur Konsole“ statt „Failed to fetch“
- Skriptfehler im Browser landen im Protokoll der App
- Ein Seitenwechsel beginnt oben; L2 / R2 rollen in einem offenen Fenster

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.68.0; den Ordner in `/data/homebrew/` ersetzen

---

## v1.67.0

### Handbuch
- Neuer Abschnitt **Starter-Kachel**: Ordner `PPSA86921` nach `/data/homebrew/` kopieren, ein Tipp auf die Kachel startet die App und öffnet die Oberfläche
- Neuer Abschnitt **Tasten am Controller**: L1 / R1 wechseln die Seite, L2 / R2 scrollen
- Startmeldung und Anzeige per Mute-Taste stehen jetzt richtig als Meldung oben links, ohne Ton
- PS4Mount ist als an der Konsole geprüft beschrieben (PS5 Pro, Firmware 12.00)
- Alles in sechs Sprachen, in der App und als `HANDBUCH.md`

### README auf GitHub
- In allen sechs Sprachen neu gegliedert: Funktionen als Kacheln, Bildraster, Schnellstart mit der Starter-Kachel, kurze Tabellen und Hinweiskästen; lange Teile (alle Funktionen im Detail, Bauen, vollständiger Rechtstext, Credits) aufklappbar
- Neue, anonyme Bilder aller Bereiche (Beispieldaten, erfundene Spiele) und eine aktuelle Handy-Ansicht
- Credits ergänzt: gasb07 (PS4Mount), V-10, drakmors Emulatoren, BlackBearReloaded, LZ4

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.67.0; den Ordner in `/data/homebrew/` ersetzen

---

## v1.66.0

### PS4-Spiele von USB (PS4Mount)
- Neuer Reiter **„PS4 von USB“** auf der Spiele-Seite: PS4-Spiele (Fake-PKG) direkt vom USB-Laufwerk spielen, ohne sie zu installieren. Das Spiel kommt mit Symbol auf den Startbildschirm, die PKG-Dateien bleiben auf dem Laufwerk
- Beruht auf PS4Mount 0.4.4 von gasb07 (GPL-3.0), dessen Quelltext in die App übernommen ist: Updates und Zusatzinhalte (DLC) werden mit eingebunden, viele Spiele in Gruppen hinzugefügt, Löschen auf der Konsole entfernt nur den Eintrag in der Bibliothek
- Liste mit Symbol, Version, Update, Zusatzinhalten und Zustand; „Jetzt suchen“, „Aus Bibliothek entfernen“ und „Hinzufügen“; Einstellungen (Ordner, Gruppen, DLC) und Protokoll
- In der Voreinstellung ausgeschaltet; läuft gleichzeitig das eigenständige `ps4mount.elf`, lässt es sich nicht einschalten
- Neue Schnittstelle `/api/v1/ps4mount`

### Bedienung mit dem Controller
- **L1 / R1** wechseln im Browser der Konsole zur vorherigen / nächsten Seite der Seitenleiste
- **L2 / R2** rollen die Seite nach oben / unten

### Meldungen
- Die Meldung beim Start der App und die Anzeige von Temperatur und Lüfter (Mute-Taste zweimal drücken) erscheinen oben links, ohne Ton

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.66.0; den Ordner in `/data/homebrew/` ersetzen

---

## v1.65.0

### Starter-Kachel
- Neuer Dump-Ordner **`PPSA86921`** für `/data/homebrew` (Datei `PPSA86921.zip`, entpackt in den Homebrew-Ordner kopieren): ShadowMountPlus meldet ihn an, auf dem Startbildschirm erscheint eine Kachel
- Ein Tipp auf die Kachel startet die App (die ELF steckt in der `eboot.bin`, es muss nichts mehr gesendet werden) und öffnet danach die Web UI im Browser der Konsole; läuft die App schon, öffnet sie nur die Web UI
- Gebaut als nativer PS5-Titel nach dem Vorbild von ps5-native-app-boilerplate; die ELF darin ist die von v1.65.0 (bei jeder neuen Version den Ordner neu laden)
- Neue Schnittstelle `POST /api/v1/open-ui` (nur von der Konsole selbst), öffnet den Browser auf der Web UI

### Oberfläche
- Seitenleiste: Kühlung oben, Profil unter System
- Alle Kacheln sind leicht durchscheinend
- Beim Konvertieren steht das Format des Ergebnisses

---

## v1.64.0

### Oberfläche
- Die ganze Web UI ist jetzt auf 85 % skaliert, es passt mehr auf den Bildschirm
- Der Schieber „Größe“ aus 1.63.0 ist wieder entfernt; der Platz unter dem FAQ-Knopf bleibt frei

---

## v1.63.0

### Oberfläche
- Neuer Schieber „Größe“ unter dem FAQ-Knopf: skaliert die ganze Web UI von 70 bis 150 %; die Einstellung wird im Browser gemerkt, ein Doppelklick stellt 100 % wieder her
- Der Text „Fortgeschrittene Kühlung & Wärmeüberwachung …“ unter dem FAQ-Knopf ist entfernt
- Alle Kacheln sind leicht durchscheinend (86 %), das Platinenmuster des Hintergrunds schimmert durch

---

## v1.62.0

### LZ4-Asset-Packs
- Spiele lassen sich im Spielordner zu LZ4-Asset-Packs packen (ein Paket mit Index und Prüfdatei, Dateien Byte für Byte mit dem Original verglichen, bei jedem Fehler wird zurückgerollt); Pakete prüfen und wieder entpacken sind dabei
- Profil „Automatisch“ (Stufe 1 bis 12) oder eigenes TOML-Profil; „Automatisch“ lässt Programme, Bibliotheken, Spielstände, schon komprimierte Medien, Konfigurations- und Datendateien sowie sehr große Dateien lose
- Die App erkennt, ob ein Spiel libSceAmpr überhaupt lädt, und sperrt das Packen sonst
- Neue Anleitung „LZ4-Asset-Packs“ im Handbuch (6 Sprachen) und klarere Beschriftung der Zeile

### APR-EMU, PlayGo und DLC
- APR-EMU in fünf Fassungen und der PlayGo-Stub 0.6 (mit und ohne Protokoll) sind in der App eingebettet und lassen sich pro Spiel auswählen
- Neue Zeile „Spiel-Plugins“: bringt PlayGo-Stub und die DLC-Module (dlc_emu 0.3) auf den neuesten Stand; alte Dateien bleiben als `.old` erhalten, fehlendes Ausführungsrecht wird gesetzt
- Optimierung per Mitschnitt: die Debug-APR-EMU schreibt beim Spielen mit, aus den gesammelten Sitzungen entsteht ein eigenes Profil für das Spiel
- Prüfen, Entpacken und Reparieren von Abbildern, AMPR-Index, Auftragsverlauf

### Spiele starten
- Aus dem Browser der Konsole schließt sich die Seite sofort beim Tippen auf „Starten“, noch bevor das Spiel lädt
- Dauert ein Start länger (zum Beispiel ein Abbild, das erst eingehängt wird), erscheint nach 1,5 Sekunden die Meldung „Bitte warten. Spiel wird geladen...“
- Absagen und Hinweise erscheinen als Meldung der Konsole
- Die Kachel des laufenden Spiels leuchtet

### Oberfläche
- Die Web UI öffnet beim nächsten Mal auf der Seite, auf der sie geschlossen wurde
- Neue Reihenfolge der Seitenleiste: Kühlung ganz oben, Profil unter System
- Beim Konvertieren steht jetzt das Format des Ergebnisses (zum Beispiel „ffpkg (UFS2-Abbild)“)
- Handbuch, FAQ und Oberfläche in sechs Sprachen aktualisiert; neue Credits für V-10 (exFAT2AMPR), nexgen999, matem6, Phoenixx1202, ArkSama und SiSTR0

---

## v1.61.0

### Kühlung
- Die Effekte auf den drei Kacheln sind wieder entfernt: kein farbiges Leuchten des Luftstroms, kein farbiger Leuchtrand und kein Lichtschein um den Tacho
- Die Hintergrundbilder bleiben und zeigen wieder ihre eigenen Farben; Tacho, Nadel und Zahl färben sich weiterhin nach der Drehzahl

### Payloads
- Größere Überschriften („Profile“, „Laufende Payloads“, „Gespeicherte Payloads“) mit größeren Symbolen und Zählern
- Größere Ordnerzeile und größere Hinweistexte

---

## v1.60.0

### Kühlung
- Die große Kachel mit der Temperatur hat jetzt auch einen leuchtenden Luftstrom und einen Leuchtrand in der Farbe der Lüfterdrehzahl, wie die Lüfter-Kachel; das Bild füllt die ganze Kachel
- Die Kachel „Läuft gerade“ leuchtet ebenso: Luftstrom und Rand in der Farbe der Lüfterdrehzahl, hellere Beschriftung
- Die bisherigen orangen und roten Bildvarianten der großen Kachel für „warm“ und „heiß“ sind entfernt; den Zustand zeigen Ring, Zeichen und Text

### Lesbarkeit
- Alle Texte der Oberfläche sind heller, vor allem graue Beschriftungen und Hinweise, und haben einen leichten Schatten
- Die Einträge der Seitenleiste haben eine dunkle, runde Fläche und einen Schatten und sind auch über dem Lüfter-Bild gut lesbar

---

## v1.59.0

### Lüfter-Kachel
- Neues Hintergrundbild mit einem Luftstrom, der in der Farbe der gemessenen Drehzahl leuchtet
- Die Farben gehen jetzt gleitend von Blau (bis 25 %) über Grün (55 %) und Gelb (75 %, neu) zu Rot (ab 95 %); Nadel, Zahl und Rand der Kachel folgen denselben Farben
- Der Tacho ist sauber entlang seiner Form freigestellt und hat einen weichen Lichtschein in derselben Farbe, damit er zum Bild gehört
- „Lüfter“, „%“ und „gemessene Drehzahl“ sind heller und auf dem Hintergrund besser lesbar
- Die Zahl zählt jetzt mit der Nadel mit, statt sofort auf den neuen Wert zu springen

### Kachel „Läuft gerade“
- Neues Hintergrundbild
- Text oben links, Controller unten rechts direkt auf dem Bild, beide mit gleichem Abstand zum Rand

### Dateimanager
- Der Knopf in der Seitenleiste heißt jetzt „Dateimanager“ statt „Dateien“
- Größerer Titel und größere Pfadzeile
- „Aktualisieren“ steht in der Werkzeugzeile neben „Hochladen“

### Fehlerbehebungen
- Nadel und Zahl blieben auf einem alten Wert stehen, wenn die Lüfter-Kachel während einer Bewegung den Bildschirm verließ

---

## v1.58.0

- „Gespeicherte Avatare“ samt Funktion entfernt (Pakete speichern, laden und löschen sowie die Schnittstellen `/api/v1/profile/avatar/library*`); schon gespeicherte Pakete in `/data/PS5-Cooling-Center/Avatars` bleiben auf der Konsole
- Größere Karte „Anzeigename“ auf der Profilseite
- Tacho und Text stehen als Paar in der Mitte der Lüfter-Kachel

## v1.57.0

- Eigene runde Dateiauswahl („Datei auswählen“, „Keine Datei ausgewählt.“) in der Sprache der App statt des englischen, eckigen Feldes des Browsers
- Runde Kästchen und runder Regler im Browser der Konsole
- Auswahl „Animation“ der Lüfter-Kachel entfernt; der Tacho läuft immer voll
- Text der Lüfter-Kachel zentriert und größer; mehr Abstand zwischen Controller und Text bei „Läuft gerade“

### Fehlerbehebungen
- Die Schnellwahl (Kühl / Ausgewogen / Leise) gilt sofort, auch wenn der Regler vorher angefasst wurde
- Laden und Löschen gespeicherter Avatare nehmen das in der Liste gewählte Paket, nicht einen im Feld stehen gebliebenen Namen

## v1.56.0

- Tacho statt drehendem Lüfter (Bild des Users); die Nadel fährt weich auf jeden neuen Wert, dazwischen läuft nichts – behebt das Ruckeln beim Zoomen und Scrollen im Browser der Konsole
- Installierte Spiele (`app.pkg`) heißen jetzt „Installiert“ statt „Abbild“; „Pfad öffnen“, „Verschieben“ und „Kopieren“ sind dort ausgeschaltet

## v1.55.0

- Warteschlange für Pakete: mehrere Pakete vormerken und nacheinander installieren, mit Pause und Abbrechen; jedes Paket wird erst an seiner Reihe geprüft
- Spiele-Seite: neuer Kopf mit Bild, „Aktualisieren“ neben der Suche, „Infos & Metadaten“ als Fenster über der Seite
- Neuer Knopf „Pfad öffnen“ auf jeder Spielkarte öffnet den Speicherort im Dateimanager
- Die Banner füllen die ganze Breite der Kacheln

---

Ältere Versionen: siehe [GitHub-Releases](https://github.com/strongt1me/ps5-cooling-system-center-pro/releases).
