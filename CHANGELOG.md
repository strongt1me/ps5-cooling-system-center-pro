# Changelog

## Unveröffentlicht

### Spielstand neu signieren
- Neuer Bereich **Spielstand neu signieren** auf der Spielstände-Seite: überträgt einen verschlüsselten Spielstand so, dass ein anderer Benutzer auf dieser Konsole ihn öffnen kann
- PS5-Spielstände: `ACCOUNT_ID` und `USER_ID` im eingebetteten `param.sfo` werden auf den Zielbenutzer gesetzt; der bisherige Spielstand des Zielbenutzers wird davor automatisch gesichert
- PS4-Spielstände: der `PARAMS`-Block (1024 Byte, enthält den per-Benutzer-HMAC) wird aus einem vorhandenen Spielstand des Zielbenutzers für dasselbe Spiel übernommen; ohne Vorlage gibt die App eine klare Fehlermeldung
- Die Konsolen-Datenbank (`savedata[_prospero].db`) wird nach dem Umschreiben aktualisiert
- Schaltfläche **Slot-Inhalt ansehen** zeigt die Dateien im Spielstand-Image (schreibgeschützt)
- Vorgang teilt den Auftrags-Slot mit der Sicherungsfunktion (kein gleichzeitiger Betrieb); Fortschritt und Ergebnis erscheinen in der Auftragskarte
- Neue Quelldateien: `src/savesfo.c`, `src/savemount.c`, `src/savedb.c`, `src/savesign.c`; SQLite 3.46.0 (Public Domain) als eingebettete Schreibbibliothek für den DB-Zugriff

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
