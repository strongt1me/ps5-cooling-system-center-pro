# Handbuch

[README](../README.de.md) · [Handbuch](HANDBUCH.md) · [API](API.md) · [Entwicklung](ENTWICKLUNG.md)

> **This document is in German.** The [README](../README.md) is in English, and the manual is also built into the app in seven languages (page "Manual").

Ausführliche Bedienung von PS5 Cooling & System Center - Pro. Die Kurzfassung
steht in der [README](../README.de.md); die Schnittstelle der App in [API.md](API.md).

## Die Seiten der Oberfläche

Nach dem Start: Browser auf **`http://<PS5-IP>:8086`**.

Der Port liegt fest auf 8086, weil die Startmenü-Kachel diese Adresse als
Deeplink enthält. Frühere Fassungen nutzten 8080 und 8770; alte
Konfigurationen ziehen beim ersten Start automatisch um.

- **Profil** — Anzeigename und Profilbild der Konsole (eigenes Bild oder eines
   der 30 eingebauten); weiter unten Offline-Konto, Fake-Anmeldung und das Sichern und Übertragen von Profilen
- **Spiele** — die Spiele des Startbildschirms: starten, kopieren, verschieben,
   konvertieren; ein zweiter Reiter zeigt die **Spielzeit**, ein dritter sichert die
   **Spielstände**, ein vierter listet die **Pakete** auf Sticks und Laufwerken, teilt
   große auf und installiert sie; dazu kommen die Reiter **PS4 PKG von USB**, **FPKGMount**, **Disc**, **Sicherungen**
   und **Verlauf** (alles siehe unten)
- **Payloads** — laufende, gespeicherte und auf USB liegende Payloads: starten,
   beenden, kopieren; dazu Profile (Abläufe aus mehreren Payloads) und „Payloads herunterladen“ (siehe unten)
- **Kühlung** — Klartext-Status, wärmster Punkt, Lüfter und laufendes Spiel, gleich
   darunter Zieltemperatur mit Schnellwahl und Betriebsart (leise/ausgewogen/kühl),
   dann Nachtmodus, Lüfterkurve, Temperaturverlauf, Sensoren und Spielprofile. Bis 1.45.x stand ein Teil davon auf
   einer eigenen Seite „Übersicht“; sie ist hier aufgegangen, ohne dass dieselbe
   Zahl zweimal erscheint. Im Expertenmodus kommen Verläufe, Rohwerte,
   Regelzustand, Diagnose und erweiterte Parameter dazu.
- **System** — ShadowMountPlus (Version und „Fakelib automatisch aktualisieren“), Konsole, Firmware, Laufzeit, Netzwerk,
   Bildschirm, QR-Code „Am Handy öffnen“, Speicherplatz (mit „Auswerfen“ für USB-Laufwerke), Kühlleistung über die Zeit,
   Zusatzabfragen, Expertenmodus mit Rohdiagnose
- **Protokoll** — Ereignisse der App, als `.log` exportierbar, und das
   Kernel-Log live (siehe unten). Seit 1.75.6 schreibt die App jedes Ereignis
   auch in `/data/PS5-Cooling-Center/app.log` (höchstens 2 s später, Warnungen
   sofort; ab 1 MB wird die alte Datei zu `app.old.log`), damit es einen
   Neustart oder Absturz der Konsole übersteht. Jeder Start der App beginnt dort
   mit einer Zeile `===== … gestartet =====`. Endet die App regulär (Ausschalten
   oder Neustart über die App, Beenden durch das System, Absturz), schreibt sie
   `===== … beendet: <Grund> =====`; fehlt diese Zeile, sagt der nächste Start
   das in einer eigenen Zeile.

Darunter öffnen **Handbuch** und **FAQ** (seit 1.49.1) das eingebaute Benutzerhandbuch und die häufigen Fragen,
am PC in einem neuen Tab; oben steht „← Zurück zur App“. Sie erscheinen in der gewählten Sprache (seit 1.50.0 sechs, inzwischen sieben mit Schwedisch; siehe
„Sprache der Oberfläche“ unten). Die eingebauten Fassungen haben keine Bildschirmfotos, damit die App klein bleibt.

In der **Kopfzeile**, auf jeder Seite, stehen die **Power-Optionen**: vier Knöpfe, mit denen
sich die Konsole steuern lässt. Ein gemeinsamer Rahmen zeigt, dass sie zusammengehören; sie sitzen
zwischen dem Seitentitel und dem Schalter „Vollbild“ und der
Verbindungsanzeige (der „Expertenmodus“ steht seit dem 11.11.2026 nur noch auf der Seite System und ist ab Werk aus): **Ruhemodus**, **Neu starten**, **Ausschalten** und
**Abgesicherter Modus** (bis 1.47.0 eine Karte auf der Systemseite). Jeder Befehl
wirkt beim ersten Klick, ohne Rückfrage (Wunsch vom 27.10.2026: nichts muss doppelt bestätigt
werden). Nicht gespeicherte Spielstände gehen beim Ausschalten und beim Neustart verloren; der
abgesicherte Modus ist ein Menü beim Hochfahren, von dort führt „PS5 neu starten“ ganz normal
zurück. Solange der Befehl läuft, ist der Knopf gesperrt, damit ein Doppelklick ihn nicht
zweimal auslöst.

**Design.** Neben der Sprache wählst du eines von vier Designs: **Standard** (dunkelblau mit leuchtenden Rändern), **Hell** (gedämpftes Grau-Blau, blendet nicht), **Dunkel** (fast schwarz, ohne Leuchten) und **Optimal** (für Fernseher und PS5: kräftiger Kontrast, größere Schrift, ohne Durchscheinen und Schatten, am schnellsten gezeichnet). Jedes Gerät merkt sich seine Wahl selbst (im Browser), etwa „Optimal“ am Fernseher und „Hell“ am Handy.

## Die Seite „Spiele“

**Doppelte Abbilder** (seit 18.10.2026, nach exFAT2AMPR 1.104 von V-10). Liegt ein Spiel in mehr als einem Abbild auf den Laufwerken, nimmt ShadowMountPlus bei jeder Suche mal das eine, mal das andere. Die App sucht beim Öffnen der Seite (und mit „Neu suchen“ oder „Aktualisieren“) in der obersten Ebene jedes Laufwerks und eine Ordnerebene darunter sowie in `homebrew` und `etaHEN/games` nach `.exfat`, `.ffpkg`, `.ffpfs` und `.ffpfsc`, liest aus jedem Abbild die Titel-ID (`sce_sys/param.json` oder `param.sfo`; bei `.ffpfs` aus dem Dateinamen) und zeigt die Karte **Doppelte Abbilder**, wenn es welche gibt. Den Ordner, den ShadowMountPlus für das Spiel nimmt, zählt sie mit. **Deaktivieren** benennt ein Abbild in `<Name>.off` um; ShadowMountPlus nimmt es danach nicht mehr. **Aktivieren** benennt es zurück. Nichts wird gelöscht. Die Karte startet eingeklappt; ein Klick auf die Kopfzeile klappt die Liste auf und zu (der Zustand bleibt innerhalb der Sitzung), „Neu suchen“ funktioniert auch ohne Aufklappen. Ein Abbild, das ShadowMountPlus gerade eingehängt hat, oder eines, dessen Spiel läuft, bleibt, wie es ist.

**Woher die Liste kommt.** Aus der App-Datenbank der Konsole
(`/system_data/priv/mms/app.db`): die sichtbaren Kacheln der Spiele-Reihe für
den angemeldeten Benutzer, mit Spielzeit und „zuletzt gespielt“, so wie die PS5
sie zählt. Gesucht wird nach Titel, Titel-ID oder Content-ID; gefiltert nach
Alle, PS5, PS4 und „Backport · AMPR · PlayGo“; sortiert wie auf dem
Startbildschirm, nach Name, zuletzt gespielt, Spielzeit, Größe oder
installiert.

**Darstellung.** Neben der Sortierung wählst du die „Darstellung“: **groß (wie bisher)** oder
**kompakt**. Bei „kompakt“ steht das Bild klein neben dem Titel und die Knöpfe sind eine Reihe von
Symbolen; fährst du mit der Maus oder dem Steuerkreuz auf ein Symbol (oder drückst „i“), steht unten, was es macht. Die Knöpfe sind in beiden Ansichten dieselben (gleiche
Funktion, gleiche Sperren und Hinweise); der Browser merkt sich die Wahl je Gerät. Vom 11. bis 17.11.2026 war „kompakt“ die Vorgabe; seit dem 17.11.2026 ist es wieder
„groß (wie bisher)“, und wer „kompakt“ gewählt hat, behält es.

**Firmware.** Unter dem Titel steht auch die Firmware, die das Spiel verlangt („FW 13.60“, bei PS4-Titeln
„PS4-FW“); verlangt es eine neuere als die der Konsole und ist auch das SDK, mit dem es gebaut ist (das der
`eboot.bin`, sonst das der `param.json`), neuer, ist die Marke gelb. Passt das SDK zur Konsole, bleibt sie grau,
denn dann startet das Spiel trotzdem (seit 23.11.2026; an der Pro verlangt Marsupilami 2 laut `param.json` 13.60,
seine `eboot.bin` ist mit SDK 12.00 gebaut und läuft auf 12.00 ohne Backport). Ist ein Backport angewendet, steht
stattdessen die Firmware, die der Backport trägt (die SDK-Fassung seiner `eboot.bin`); was die `param.json`
verlangt, steht im Hinweis. Seit 23.11.2026 auch bei FPKGMount-Spielen: Die App liest dafür den Zwischenspeicher
von Kraken Mount (`/data/krakenmount/caches/<Titel-ID>`: `eboot.bin` und `fakelib`); die Bibliotheken, die Kraken
Mount selbst beilegt (AMPR, PlayGo, DLC), zählen nicht als Backport, und nennt Kraken Mount einen Backport-Ordner,
gilt das Spiel als zurückportiert. In der Vollbild-App steht die Firmware in der Zeile des Spiels und in den
Angaben; verlangt sie eine neuere als die der Konsole, zeigen die Details zusätzlich „Gebaut mit SDK“ (etwa
„12.00 · startet auf FW 12.00“ oder „13.00 · neuer als FW 12.00“).

**Gepackt** (seit 17.11.2026): Liegt ein Spiel in AMPR-Packs, steht unter dem Titel „zstd gepackt“ oder „LZ4 gepackt“, egal in welchem Format das Spiel liegt (Ordner oder Abbild); unter „Infos & Metadaten“ gibt es die Zeile „Gepackt“. Bei Abbildern liest die App dafür beim Lesen der Abbild-Datei die beiden Index-Dateien (`ampr_zstd.index`, `ampr_assets.index`) mit, ohne etwas einzuhängen, und merkt es sich, bis sich die Datei ändert; kurz nach dem Start der App kann die Marke bei einem Abbild einen Moment fehlen. In der Vollbild-App steht die Angabe als farbige Plakette in der Zeile des Spiels (grün: zstd, blau: LZ4) und als Zeile „Gepackt“ im Infofeld.

**Format und Speicherort.** Unter „Infos & Metadaten“ meldet ShadowMountPlus 1.7,
wo das Spiel läuft: der Dump-Ordner oder die Abbild-Datei (`.exfat`, `.ffpkg`,
`.ffpfs`, `.ffpfsc`), nicht der Ordner unter `/mnt/shadowmnt`, in den es sie
einhängt. Normal installierte Spiele stehen als PKG unter `/user/app`. Das Format
steht auch als Marke unter dem Titel, neben Backport, AMPR EMU und PlayGo:
**Dump-Ordner**, **exFAT**, **ffpkg**, **ffpfs**, **ffpfsc**, **PS4 PKG** oder
**PS5 PKG**. Ob ein installiertes Paket echt oder gefälscht (fpkg) ist, lässt
sich nicht erkennen: Beide liegen als `app.pkg` unter `/user/app`. Was für ein
Spiel nicht geht (Kopieren eines installierten Pakets, zum Beispiel), bleibt
ausgegraut sichtbar, damit alle Karten gleich aufgebaut sind; der Grund steht im
Hinweis des Knopfs. **Pfad öffnen** springt im Dateimanager in den Ordner, in dem das Spiel liegt (bei einem Abbild: in den Ordner der Abbild-Datei).

**Backport, AMPR EMU und PlayGo** liest die App aus dem Spielordner eingehängter
PS5-Spiele: Ersatzbibliotheken im Ordner `fakelib` (`libSceAmpr.sprx`,
`libScePlayGo.sprx`, weitere) und die SDK-Version der `eboot.bin`, die ein
Backport unter die aus `param.json` absenkt. Spiele, die ShadowMountPlus als
Abbild einhängt, lassen sich nur prüfen, solange das Abbild eingehängt ist.

Jedes Spiel bringt diese Dateien selbst mit, im eigenen `fakelib`-Ordner. Die App
**zeigt sie hier nur an**. Bauen, Packen und Austauschen der mitgelieferten Fassungen geschieht im Fenster der Spielkarte (siehe „AMPR-Index, APR-EMU, PlayGo und der Verlauf“); eigene Dateien über diese Seite hochzuladen gibt es nicht mehr:

- **Bibliotheken ersetzen.** Ein früherer Versuch, sie über die Weboberfläche
  hochzuladen, wurde am 02.10.2026 entfernt: ShadowMountPlus' Overlay dafür
  schlägt bei Spielen im exFAT-Ordner-Format zuverlässig fehl und blockiert dann
  den Start komplett. Wer eine Bibliothek ersetzen will, legt sie direkt im
  `fakelib`-Ordner des Spiels ab (FTP oder vergleichbar). Das hat sich als
  einziger zuverlässiger Weg bestätigt.
- **AMPR-EMU-Asset-Packs.** Ein Asset Pack ersetzt Spieldateien selbst; es wird aus
  einem Mitschnitt des eigenen Spieldurchlaufs mit drakmors `ampr-pack-tools`
  gebaut. Das Hochladen fertiger Pack-Dateien über die Seite wurde am 02.10.2026
  auf Wunsch entfernt, obwohl es funktionierte. Die fertigen Dateien
  (`ampr_emu.index`, `ampr_assets.index`, `ampr_assets-NNN.pak`) gehören direkt in
  den Spielordner (FTP oder vergleichbar), ohnehin nur möglich bei Spielen aus
  einem Ordner, nicht bei einem Abbild oder einem installierten Paket.

**Covers & Metadaten.** Seit dem 11.11.2026 gibt es dafür keinen Schalter und keine Anzeige mehr: Die App
speichert sie immer. Sie legt auf der Konsole ab, was sie beim Lesen der Liste
sonst jedes Mal neu holen muss, im Ordner `/data/PS5-Cooling-Center/covers_and_more` (so heißt er seit dem 05.10.2026; ein
älterer Ordner `covers_and_moore` wird beim ersten Zugriff umbenannt): je Spiel
ein Ordner mit der Titel-ID, darin `icon0.png` (eine Kopie des Titelbilds) und `meta.json` (die
Angaben zum Spiel und das, was langsam zu ermitteln ist: die Größe des Spielordners, ob ein
Backport, AMPR EMU oder PlayGo dabei ist, samt den Merkmalen der Dateien, an denen das hängt). Beim
nächsten Lesen, auch nach einem Neustart der App und bei einem Stick, der gerade nicht steckt,
nutzt sie diese Dateien: Bilder kommen aus der Kopie, und ein Spielordner, dessen Größe vor
weniger als sechs Stunden gemessen wurde, wird nicht noch einmal durchgezählt. Was sich seither
geändert hat (eine andere `eboot.bin`, eine neue Ersatzbibliothek, ein anderes Bild), erkennt die
App und liest es neu. Kommt ein Spiel dazu, wird es mit dem nächsten Lesen mitgespeichert.
Von Hand aufräumen lässt sich der Ordner mit dem Dateimanager (nur Ordner mit einer Titel-ID als Namen und darin `meta.json`,
`icon0.png` und deren Zwischendateien gehören der App); die App legt ihn beim nächsten Lesen der Liste neu an. Ein Bild über
6 MB wird nicht kopiert, und alle Bilder zusammen bleiben unter 160 MB.

## Spiele starten und beenden

„Starten“ startet das Spiel direkt; läuft es schon im Hintergrund, holt die
App es nach vorn. Im Browser der Konsole schließt sich der Browser dabei sofort
beim Tippen, noch bevor das Spiel lädt, damit das Spiel vorn ist; sagt die Konsole
etwas dazu (eine Absage, ein Angebot „Starten“), zeigt sie es als Meldung oben rechts.

- **Läuft schon ein anderes Spiel**, auch pausiert im Hintergrund, schließt
  „Starten“ es und startet das gewählte in einem Zug (seit 27.10.2026: nichts muss
  doppelt bestätigt werden; die Seite nennt der Konsole dafür genau das laufende
  Spiel, `close` bei `POST /api/v1/library/launch`). Eine Meldung sagt „Tetris®
  Ultimate wird geschlossen, danach startet Assassin's Creed Shadows.“ Das laufende
  Spiel speichert vorher nicht – was darin nicht selbst gespeichert wurde, geht
  verloren. „Spiel beenden“ schließt ein Spiel ebenso sofort. „Beendet“ meldet
  die App erst, wenn die Konsole das alte Spiel ganz losgelassen hat: wenn das
  System seinen Bereich (`/mnt/sandbox/<ID>_000`) entfernt hat, und eine Sekunde
  später, in der ShadowMountPlus seine Abbilder aushängt. Danach startet das
  nächste Spiel sofort.
- Ob ein Spiel läuft, fragt die App zusätzlich direkt den Kernel. Ein Spiel,
  das pausiert im Hintergrund wartet oder das die Konsole hinter dem Browser
  gestartet hat, wird so auch erkannt; die PS5 würde ein pausiertes Spiel beim
  Start eines anderen sonst ohne Nachfrage selbst schließen.
- **„Spiel beenden“** steht auf der Kühlungsseite an der Kachel „Läuft gerade“
  und auf der Karte des laufenden Spiels. Ein Tipp beendet das Spiel sofort,
  ohne Rückfrage (Wunsch vom 03.10.2026).
- Beendet wird wie bei „Close App“ in Elf Arsenal: Der Spielprozess
  (`eboot.bin`) wird abgeschossen, bis er weg ist. Nur das Spiel, das die Seite
  nennt, wird geschlossen.
- **Öffnet die Kachel nur eine Webseite**, oder lehnt der Starter ab (etwa bei
  einem Abbild, das ShadowMountPlus noch nicht eingehängt hat), startet die App
  nichts selbst. Sie schickt stattdessen oben rechts eine Meldung mit der Taste
  „Starten“: Sie öffnet den Link der Kachel, und die Entscheidung bleibt bei der
  PS5 und dir.
- **Ein Spiel wird nie zweimal gestartet.** Einen laufenden Titel noch einmal zu
  starten, bringt die Konsole zum Absturz (Erfahrung aus Elf Arsenal, am
  03.10.2026 vermutlich auch hier passiert). Ein zweiter Klick auf „Starten“
  startet deshalb nichts und bietet auch keine zweite Meldung an, solange der
  erste Start läuft (etwa während er auf das Aushängen bei ShadowMountPlus oder
  das Ende der Vollbild-App wartet) und noch 30 Sekunden danach; jeder Titel hat
  seine eigene Marke (seit 23.11.2026, vorher 30 Sekunden ab dem Tippen).
- **„Device busy“ beim Start (seit 23.11.2026, `start_retrying_busy` in `src/api.c`):**
  Lehnt ShadowMountPlus einen Start kurz ab (der Starter antwortet `0x80940033`,
  im Protokoll von ShadowMountPlus `launch mount unavailable … status=16`), fragt
  die App den Starter noch zweimal im Abstand von 3 Sekunden, bevor sie die
  Meldung mit „Starten“ anbietet. Ein Titel, der inzwischen läuft, wird nie ein
  zweites Mal gestartet. Das gilt für den Start im Browser, nach dem Ende der
  Vollbild-App, beim Spielwechsel und in der Dump-Warteschlange; der Browser
  wartet auf diese Antwort bis zu 20 Sekunden.

## Ein- und Aushängen über ShadowMountPlus (seit 30.10.2026; Hinweis seit 14.11.2026)

In der Spielkarte (Vollbild-App: Aktionen) hängen „Einhängen“, „Neu einhängen“ und „Aushängen“ ein Spiel über
ShadowMountPlus (ab 1.7beta10) ein oder aus. Das hilft, wenn ein Spiel hängt oder nach dem Kopieren, Verschieben
oder Löschen neu erkannt werden soll. Nie, solange das Spiel läuft.

**Wichtig:** Ein eingehängtes Spiel, das nicht läuft, hält bei ShadowMountPlus den einen Platz für die Einhängung
besetzt (`claim_prepared_title` in `sm_shellcore_service.c`, ShadowMountPlus 1.7beta12). Solange es nicht gestartet oder
wieder ausgehängt ist, startet kein anderes Spiel und keine Kachel: ShadowMountPlus meldet im Protokoll
`launch mount unavailable: <Titel-ID> status=16 (Device busy)`, die Konsole zeigt einen Startfehler
(`LaunchFlowError`). An der Konsole nachgestellt am 14.11.2026: Mit eingehängtem Spiel scheiterten beide Kacheln
(FAKE86921 und FAKE86922); nach „Aushängen“ startete die Kachel sofort wieder. Ein Neustart von ShadowMountPlus gibt den
Platz ebenfalls frei. Darum zeigt die Spieleseite im Browser oben einen gelben Hinweis mit dem Knopf „Aushängen“,
die Vollbild-App die Marke „EINGEHÄNGT“ in der Zeile (dazu im Infobereich „Eingehängt: wird gleich ausgehängt“ und am
Menüpunkt „Aushängen“ „gibt andere Starts frei“), und auch die Meldung nach dem Einhängen sagt es. Wer ein Spiel nur
neu erkennen lassen wollte, hängt es danach wieder aus.

**Die App räumt selbst auf, ohne dass jemand etwas aushängen oder warten muss** (`src/smphold.c`, seit 14.11.2026 abends): Jedes
Spiel, das sie über ShadowMountPlus einhängt (die Knöpfe der Spielkarte, die Prüfung der Fakelibs, der AMPR-Auftrag), schreibt sie
auf (im Speicher und in `/data/PS5-Cooling-Center/smp-mounted.txt`, damit ein Neustart der App es nicht vergisst). Dann gibt sie es
von allein frei, auf drei Wegen:

1. **Sofort, wenn sie selbst ein anderes Spiel startet** (Browser „Starten“, Vollbild-App, Dump-Warteschlange): `ps5tm_library_start`
   hängt vorher aus, was sie hält und nicht dieses Spiel ist.
2. **Sofort, wenn die Konsole einen anderen Start ablehnt** (etwa vom Startbildschirm oder an einer Kachel): Das Kernel-Log nennt den
   gescheiterten Start (`launchApp(<Titel-ID>)`, `LaunchFlowError.<Titel-ID>`; so auf der Test-Konsole gesehen). Die App liest es, solange
   sie ein Spiel hält (jede Sekunde), hängt aus, schreibt `smp_hold_released` ins Protokoll und meldet auf der Konsole: „Der Start von
   … ist gescheitert. Das eingehängte … kann der Grund sein: Die App hat es ausgehängt. Bitte den Start noch einmal versuchen.“ Der
   erste Versuch kann also einen Fehlerdialog zeigen, der zweite geht. Scheitert der Start des gehaltenen Spiels selbst, bleibt es.
3. **Sonst nach 30 Sekunden** ohne Start, mit einer Meldung auf der Konsole und einer Zeile im Protokoll (zuerst waren es 2 Minuten;
   scheitert das Aushängen, `smp_hold_failed` und ein neuer Versuch nach 10 Sekunden). Die 30 Sekunden laufen nur, solange kein Spiel
   läuft und kein Auftrag der App arbeitet; hat ShadowMountPlus das Spiel inzwischen selbst freigegeben (es lief und wurde beendet, oder
   ShadowMountPlus wurde neu gestartet), streicht die App es nur aus ihrer Liste.

Was nicht die App eingehängt hat (etwa über die eigene Seite von ShadowMountPlus oder dessen dauerhafte Abbild-Einhängungen), rührt sie
nicht an. Darum erscheinen der gelbe Hinweis und die Marke nur für Spiele, die die App selbst eingehängt hat (`app_mounted` in der
Spieleliste): Das Feld `mounted` von ShadowMountPlus heißt nur, dass unter `/system_ex/app/<ID>` ein Spiel liegt – auch nach dessen
eigener Einrichtung eines neuen Ordner-Spiels oder nach der Registrierung durch Kraken Mount, und beides blockiert keinen anderen
Start (Konsolentest 14.11.2026: ein Titel mit `mounted: true` aus ShadowMountPlus' Einrichtung ließ den Start eines zweiten zu; erst
das Einhängen über den Knopf lieferte `launch mount unavailable … status=16 (Device busy)`). Die Meldung des Hinweises und die Marke in der
Vollbild-App lauten entsprechend „wird gleich ausgehängt“.

## Spiele kopieren und konvertieren (ab 1.46.0)

Auf der Karte eines Spiels, das als Dump-Ordner oder Abbild-Datei vorliegt:
**Kopieren** legt es auf ein anderes Laufwerk, **Konvertieren** macht aus dem
Ordner ein exFAT-, ffpkg- oder ffpfsc-Abbild (ein exFAT-, ffpkg- oder
ffpfs-Abbild geht auch zu ffpfsc). Das Ziel ist wahlweise der Ordner `homebrew`
des Laufwerks, wo ShadowMountPlus das Ergebnis findet und einbindet, oder
`PS5-Sicherung/Spiele`, wo es nie sucht. Nichts wird überschrieben, das Original
bleibt unberührt, und bei Abbruch oder Fehler räumt die App ihre eigenen Dateien
weg. Für ShadowMountPlus wird ein Spiel erst sichtbar, wenn es vollständig ist:
Bei einer Kopie kommt `sce_sys/param.json` zuletzt, ein Abbild trägt bis zum
Schluss einen Zwischennamen.

**Original danach (seit 18.11.2026).** Beim Konvertieren, Kopieren und Entpacken (auch bei „Zur
Warteschlange“) wählst du im Fenster unter „Original danach“, was mit dem Original geschehen soll:
**Behalten** (Vorgabe), **Löschen** oder **Verschieben**. So liegt derselbe Titel nicht zweimal auf
der Konsole; ShadowMountPlus meldet ihn sonst aus zwei Orten und hat damit Probleme. **Löschen**
fragt vorher und nennt Name, Größe und Pfad; die Spielstände bleiben. **Verschieben** macht zuerst
eine geprüfte Kopie (jede Datei zurückgelesen und verglichen, `.sha256` daneben) in einen Ordner
deiner Wahl und löscht das Original erst danach; zur Wahl stehen die interne SSD, die
M.2-Erweiterung und die USB-Laufwerke, vorgeschlagen wird `PS5-Sicherung/Spiele` auf dem gewählten
Laufwerk, und „Ordner wählen“ öffnet eine kleine Auswahl. Einen Ordner, in dem ShadowMountPlus selbst
nach Spielen sucht (`homebrew`, `etaHEN/games`, bei USB und M.2 die Wurzel bis zwei Ebenen tief;
`PS5-Sicherung/Spiele/<Name>` liegt drei Ebenen tief und wird nie gefunden), lehnt die App ab, ebenso
einen, in dem schon etwas gleich heißt, der nicht genug Platz hat oder zu dem der Weg über eine
Verknüpfung führt. Gelöscht oder verschoben wird erst, **nachdem der Vorgang gut zu Ende ging** und
sein Ergebnis da ist; scheitert er oder brichst du ihn ab, bleibt das Original unberührt. Das
Löschen geht wie die Löschfunktion (nur unter dem Einhängepunkt eines Laufwerks oder `/data`, nie in
der Ablage der App, nie bei laufendem Spiel; ShadowMountPlus lässt den Titel zuerst los, wenn es genau
diese Daten führt). Die App wartet, bis nichts anderes mehr läuft (die Warteschlange erst nach dem
Original des vorigen Eintrags); das Ende kommt als Meldung ohne Ton, die Zeile „Original danach“ im
Fenster zeigt den Stand. Beim Entpacken ersetzt das den früheren Schalter „Abbild danach löschen“.
Die Vollbild-App bietet dieselbe Auswahl. Schnittstelle: `docs/API.md`, Abschnitt „Original danach“.

**Während das Original bearbeitet wird** (Löschen oder Verschieben), startet die App keinen anderen
Vorgang, auch nicht den nächsten Eintrag der Warteschlange; eine Anfrage in dieser Zeit lehnt sie mit
einem Hinweis ab. Startet die App in dieser Zeit neu (etwa durch eine neue ELF), ist die Wahl nicht
mehr da: Beim nächsten Start steht im Verlauf und als Meldung, was offen war – war schon etwas
gelöscht, sagt die Meldung, wo der Rest liegt. Wurde das Ergebnis nur mit „Schnell“ geprüft, bleibt das Original bei „Löschen“ liegen (die Meldung sagt es: dann „Gründlich“ wählen); „Verschieben“ geht auch dann, weil seine Kopie für sich geprüft wird. Hat ShadowMountPlus ein Entpacken ohne Erfolgswert
beendet, bleibt das Abbild bei „Löschen“ liegen (bei „Verschieben“ geht es weiter: die Kopie wird auf
jeden Fall geprüft). Die Oberfläche schickt den Pfad des Originals, das sie gezeigt hat; liegt der
Titel beim Start woanders, lehnt die App ab. Ein Ordner mit einem unfertigen Rest einer früheren Kopie
dieser App am Ziel gilt nicht als belegt: Die Kopie ersetzt ihn. Liegt der Zielordner auf demselben Laufwerk wie das
Original (nicht bei der internen SSD, deren Spieleordner eigene Einhängungen sind), wird das Original nur umbenannt:
sofort, ohne Kopie und ohne freien Platz; geht das zwischen den beiden Orten nicht, wird doch kopiert.

**Verschieben** erledigt ShadowMountPlus selbst, damit es weiß, wo das Spiel danach
liegt; zur Wahl stehen nur Ordner, in denen es sucht. Ein Abbild zurück zu einem
Ordner entpackt ebenfalls ShadowMountPlus. Der Streifen über der Bibliothek zeigt
diese Aufträge und – seit ShadowMountPlus 1.7beta8 – auch Installationen, die
ShadowMountPlus selbst ausführt (ein PKG oder ein Spielordner aus seiner eigenen
Oberfläche); ohne Titel-ID steht dort der Paket- oder Dateiname.

**Konvertieren** macht die App auf der PS5, in diese Formate:

- **exFAT-Abbild** aus einem Dump-Ordner.
- **`.ffpkg`** (UFS2, unkomprimiert): im Aufbau, den ShadowMountPlus in seiner README
  für ein `.ffpkg` empfiehlt und den auch exFAT Image Builder von UFS2Tool verlangt:
  64-KiB-Blöcke und -Fragmente, kein reservierter Platz
  (`newfs -O 2 -b 65536 -f 65536 -m 0 -i 262144`). Jede Datei und jeder Ordner belegt
  volle 64-KiB-Blöcke; ein Spiel mit sehr vielen winzigen Dateien braucht dadurch mehr
  Platz als die Summe seiner Dateien. Eine einzelne Datei darf bis über 4 TB groß sein
  (bis zum 04.10.2026 scheiterten Dateien über rund 128 MB). Das Abbild ist die
  Spieldaten (jede Datei auf 64 KiB aufgerundet) plus gut 1,2 % für die Verwaltung und
  etwa 0,5 % freier Platz, mindestens 64 MB und höchstens 512 MB, wie ihn auch
  `mkufs2.sh` von ShadowMountPlus lässt; der Dialog nennt die genaue Größe, bevor es
  losgeht, und die Platzprüfung rechnet damit.
- **`.ffpfsc`** (exFAT innen, komprimiert; Warnung vor dem Packen bei Spielen mit vielen Daten oder Texture-Streaming, siehe unten): der Aufbau, den MkPFS und
  ShadowMountPlus empfehlen. Auch ein exFAT-, ffpkg- oder ffpfs-Abbild lässt sich
  so zu `.ffpfsc` machen.

**PS4-Spielordner** (nur `sce_sys/param.sfo` und `eboot.bin`, keine `param.json`) lassen sich ebenso in alle drei
Formate konvertieren: ShadowMountPlus bindet PS4-Abbilder ab 1.7beta8 ein (es sucht `param.json` oder `param.sfo` an der
Wurzel des Abbilds, für alle seine Formate gleich). Die App legt dabei (`dump_image.c`) eine
`param.json` aus der `param.sfo` und, falls nötig, eine korrigierte `npbind.dat` über den Ordner; sie
kommen mit ins Abbild, damit auch ältere ShadowMountPlus-Versionen es finden; der Ordner selbst bleibt unverändert.
Ziel ist nur ein USB- oder M.2-Laufwerk (ein PS4-Titel aus dem Speicher der Konsole bringt sie zum Absturz), einen
AMPR-Index gibt es für PS4-Spiele nicht. Der Plan meldet solche Ordner mit `"ps4": true`. Trophäen gibt es bei PS4-Spielen aus Abbildern nicht (0x80551618), und manche Titel bleiben bei „Bitte warten“ stehen: Beides verlangt eine echte Installation (Reiter „Pakete“).

**Warnung vor dem Packen zu ffpfsc** (seit 21.10.2026). Ein komprimiertes Abbild muss die Konsole beim Spielen ständig entpacken; bei Spielen mit sehr vielen Daten oder Texture-Streaming sind Ruckler möglich. Der Plan des Konvertierens (`GET /api/v1/library/convert/plan`) sieht deshalb vorab nach (Dump-Ordner, `.exfat`, `.ffpkg`, `.ffpfs`; Abbilder ohne Entpacken über den Abbild-Leser) und liefert `stream_risk`: ab 40 GB Daten (`big_data`), große Streaming-Container (`.ucas`, `.utoc`, `.pak`, `.forge`, `.streamdb`, `.resources`, `.cas`, `.rpf`, zusammen ab 4 GB) oder ein Ordner, dessen Name mit „stream“ beginnt (ab 2 GB). Trifft etwas zu, steht beim Eintrag „ffpfsc“ ein Hinweis; eine zweite Frage beim Start gibt es nicht mehr (seit 27.10.2026: nichts muss doppelt bestätigt werden). Es ist eine Schätzung an Namen und Größen, nicht an der Spiel-Engine; die Schwellen stehen als `STREAM_*` in `src/gameconvert.c`.

**Unsignierte Programmdateien** (seit 07.11.2026, nach ps5-dump-forge 0.5.4). Sind die Programmdateien eines Spiels (`eboot.bin`, `.prx`, `.sprx`, `.elf`, `.self`) einfache ELF-Dateien (`e_type` `0xFE10` oder `0xFE18`), startet die Konsole es nicht: Ihr Lader lehnt sie ab. Der Plan des Konvertierens meldet sie vorab (`unsigned_exec`), das Fenster nennt sie („Unsignierte Programmdateien“, in Browser und Vollbild-App). Beim Konvertieren eines **PS5-Dump-Ordners** schreibt die App sie als **fake-signierte SELF-Dateien** (FSELF) in einen kleinen Ordner neben dem Ziel (`<Ziel>.ps5cc-sign-teil`), der als zweite Quelle über den Spielordner gelegt wird – so, wie ein PS4-Ordner seine `param.json` bekommt –; jede Datei wird nach dem Schreiben gegen das Original zurückgelesen, der Ordner danach entfernt, der Spielordner nur gelesen. Ein `ampr_emu.index` im Ordner wird dabei auf die neuen Größen gebracht. Der Aufbau der Dateien ist der, den `make_fself.py --ptype fake` aus ampr-emu-ng (und ps5-dump-forge) schreibt; er ist byteweise gleich geprüft (`test/fsign`) und an der Konsole bestätigt (08.11.2026): Ein PS5-Ordner mit einfacher ELF als `eboot.bin` startet nicht (`0x80020008`), das mit der App konvertierte Abbild startet und läuft. Ausgenommen sind PS4-Ordner, Abbilder als Quelle (ein Abbild wird Byte für Byte gepackt) und Dateien, die schon signiert sind; Dateien mit ungewöhnlichem Aufbau bleiben, wie sie sind, und der Auftrag sagt es in seiner Notiz. Beim Packen gilt dasselbe für **„Als neuen Ordner … daneben“** (nur LZ4); **im Spielordner** packen lässt die Programmdateien, wie sie sind (das Fenster sagt es). Nur bei zstd, das immer im Spielordner packt, ersetzt der Schalter „Programmdateien als FSELF signieren“ (voreingestellt an) sie dort durch FSELF-Fassungen: Nach dem Packen und dem Entfernen der Originale wird jede Programmdatei neben dem Original geschrieben, gegen es zurückgelesen und dann darüber umbenannt (Rechte und Zeiten bleiben); der `ampr_emu.index` wird für die fertigen angepasst. Scheitert eine, bleiben die fertigen signiert und die übrigen, wie sie waren (die Notiz des Auftrags sagt es). Die Originale der Programmdateien sind danach nicht mehr einzeln da: Die FSELF-Datei trägt alle ihre Abschnitte. Unabhängig davon bleibt die Fake-Signatur beim Sichern von Disc-Spielen („Als FSELF neu signieren“, Reiter Disc) eine eigene Funktion mit eigenem Aufbau (`src/dump_fself.c`).

Der Programmcode für exFAT und ffpfsc ist aus [MkPFS](https://github.com/PSBrew/MkPFS)
(GPL-3.0) übertragen, der für ffpkg aus [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)
(BSD-2-Clause, nur als Vorlage für den eigenen C-Code, nichts davon wird
mitgeliefert). Was nicht geht, fällt schon im Plan oder vor dem Schreiben auf:
Namen, die exFAT nicht darstellen oder nicht auseinanderhalten kann, zu lange
Pfade, zu große Dateien (`.ffpkg`: über 4 TB je Datei) und überfüllte Ordner
(`.ffpkg`: bei 20-Zeichen-Namen etwa 24.000 Einträge, bei 255-Zeichen-Namen etwa 1.500; höchstens 32.765 Unterordner in einem Ordner). Der Konvertier-Dialog nennt so einen Grund schon bei der Auswahl des Formats.

**Prüfung (seit 03.10.2026).** Nach dem Schreiben liest die App das Ergebnis vom
Laufwerk zurück und prüft es ganz, nicht nur stichprobenweise:

- **Kopie:** Jede Datei wird gleich nach dem Schreiben gelesen. Ihre Prüfsumme
  (CRC-32) muss die sein, die beim Lesen des Originals entstand. Weicht eine
  Datei ab, scheitert die ganze Kopie und wird entfernt. Die Kopie dauert dadurch
  etwa doppelt so lange wie das reine Schreiben; die Anzeige zählt beides.
- **exFAT und ffpfsc:** Die Prüfsumme der Nutzdaten, beim Schreiben genommen, muss
  zum zurückgelesenen Ergebnis passen. Bei ffpfsc werden außerdem Köpfe, Inodes
  und die ganze Blocktabelle geprüft, **jeder Block entpackt** und die Bereiche,
  die leer sein müssen, auf Nullen untersucht.
- **ffpkg:** Der Aufbau wird geprüft (Superblock, Gruppenköpfe, jeder Ordner und
  jede Datei ab der Wurzel) und der Inhalt jeder Datei mit der Prüfsumme
  verglichen, die beim Lesen des Originals entstand; danach wird die Datei noch
  einmal ganz gelesen.

**Prüfstufe (seit 11.11.2026).** Beim Konvertieren wählst du unter „5. Prüfung danach“ zwischen **Gründlich**
(Vorgabe: alles wie oben beschrieben) und **Schnell**. Schnell liest das Ergebnis nicht noch einmal ganz:

- **ffpfsc:** Köpfe, Inodes, die ganze Blocktabelle und die Bereiche, die leer sein müssen, werden geprüft, dazu höchstens 2048 Blöcke
  (gleichmäßig über das ganze Abbild verteilt, der erste und der letzte sind dabei), jeder entpackt: Ein komprimierter Block trägt
  die Adler-32 seiner Daten, die beim Entpacken geprüft wird. Roh gespeicherte Blöcke haben dabei nichts, wogegen man sie prüfen
  könnte. Die Prüfsumme der Nutzdaten wird nicht verglichen.
- **exFAT:** Die Größe der Datei und rund 400 Stücke (die ersten und letzten drei und gleichmäßig verteilte dazwischen), deren
  CRC-32 beim Schreiben genommen wurde, werden vom Laufwerk zurückgelesen und verglichen.
- **ffpkg:** Aufbau und der Inhalt jeder Datei werden geprüft wie oben, aber in einem Durchgang statt in zweien, also ohne das zweite Durchlesen für die SHA-256.
- Bei Schnell entsteht **keine** Prüfsummen-Datei (`.sha256`); die Meldung am Ende sagt es. Ein beschädigter Block außerhalb der
  Stichprobe wird nicht gefunden; wer sich nicht sicher ist, nimmt Gründlich oder prüft das Abbild später mit „Prüfen“.

**Lesestücke (seit 11.11.2026).** Die Quelle und das Zurücklesen werden in 4-MiB-Stücken gelesen (vorher 1 MiB); der Lesevorlauf
zwischen zwei Laufwerken hält drei solche Stücke bereit. Blöcke, die sich nicht komprimieren lassen, speichert ffpfsc roh, wie
bisher: Ein Block wird nur gepackt, wenn er dabei mindestens 5 % spart. Einen Vorfilter, der solche Blöcke ohne Versuch überspringt,
gibt es nicht: Ein Test an der Konsole mit zwei großen Spielen brachte damit keinen Zeitgewinn.

**Debug-FPKG (seit 1.84.0).** Neben ffpfsc, ffpkg und exFAT baut das Fenster ein **Debug-FPKG** (`.pkg`), das Kraken Mount einhängt (Reiter „FPKGMount“). Geschrieben wird es von **Dump Forge** (quer3q, GPL-3.0), einem eigenen PS5-Programm: Die App startet es für den Auftrag, gibt ihm Quelle und Ziel, zeigt seine Schritte (Vermessen, Komprimieren, Prüfen, Umbenennen) wie bei den anderen Formaten und beendet es danach wieder; mit „Zur Warteschlange“ geht es ebenfalls. Dump Forge steckt in der App: Seine ELF (17,7 MiB) liegt gepackt (zstd, 8,0 MiB) in der ELF der App und damit auch in beiden Kacheln, wer nur die ELF benutzt, muss nichts laden. Eine neuere Fassung hat Vorrang vor der eingebauten (0.5.7): „Neue Fassung von GitHub laden“ im Fenster holt sie von GitHub (nur auf Knopfdruck, HTTPS, die Prüfsumme aus `SHA256SUMS` wird verglichen) nach `/data/PS5-Cooling-Center/dumpforge/`, oder man legt `ps5-dump-forge-<Version>-ps5.elf` selbst in diesen Ordner (zum Beispiel per FTP); die neueste Version zählt. Vier **Kompressionsstufen** gibt es: **Schnell** (Kraken, empfohlen), **Ausgewogen** und **Kleinste** (rund 2,6 % kleiner, aber etwa 6- und 9-mal so lange, alle Kerne rechnen durchgehend) und **Unkomprimiert** (das Paket so groß wie das Spiel, für den Fall, dass die Konsole ein komprimiertes nicht startet). „Spielbar“ legt das Paket in den Ordner `FPKG-Games` des Ziels, den Kraken Mount durchsucht, „Sicherung“ nach `PS5-Sicherung/Spiele`. Der Name der Ausgabe darf höchstens 63 Bytes (ohne „.pkg“) haben; das Fenster prüft es vor dem Start. Bei **Gründlich** liest Dump Forge jedes Byte des fertigen Pakets noch einmal, bei **Schnell** prüft es den Aufbau, kleine Dateien ganz und vom Rest eine Stichprobe; eine Prüfsummen-Datei entsteht nicht. Hat das Original dieselbe Titel-ID und hängt bei ShadowMountPlus, stören sich beide, sobald Kraken Mount das Paket anmeldet: Hänge das Original vorher aus. Dump Forge legt keine Kachel auf dem Startbildschirm an: Die App hält den Platz `/user/app/PDFG00001` mit einer leeren Datei besetzt (wie bei Kraken Mount) und entfernt eine früher angelegte Kachel beim nächsten Start. **Spiele mit AMPR-Packs** (seit 18.11.2026): Ist das Spiel mit AMPR-Packs gepackt (`ampr_zstd.index` oder `ampr_assets.index` im Ordner oder im Abbild), packt Dump Forge die Packs beim Umwandeln selbst aus (zstd-Packs mit „zstd_unpack“, LZ4-Packs mit „lz4: unpack“ in seinem Auftrag), denn Kraken Mount nimmt kein Paket mit Packs an, es stünde in seiner Liste als „Fehlgeschlagen“. Das Paket enthält dann das ungepackte Spiel und wird entsprechend groß (je nach Stufe kleiner), die Quelle bleibt unverändert, und es dauert länger als bei einem Spiel ohne Packs. Das Fenster sagt es vorher: Bei einem Ordner nennt es die Größe des ungepackten Spiels (aus dem Index der Packs) und prüft den Platz danach, bei einem Abbild kann erst Dump Forge sagen, wie groß es wird; die Prüfung vor dem Start zeigt einen gelben Hinweis. Ausgegraut bleibt „FPKG“ nur, wenn sich nicht prüfen lässt, ob das Spiel Packs enthält, oder wenn es zstd- und LZ4-Packs zugleich hat (Dump Forge löst nur eine Art auf): Dann erst eine Art entpacken („zstd entpacken“ oder „LZ4 entpacken“ bei den Asset-Packs des Spiels), dann konvertieren.

**Packen beim Konvertieren (seit 19.11.2026).** Bei **exFAT** und **ffpkg** eines PS5-Spielordners erscheint im Konvertieren-Fenster die Gruppe **Packen (AMPR-Packs)**: **Nicht packen** (Vorgabe; die App schreibt das Abbild wie bisher selbst), **LZ4-Packs** oder **zstd-Packs (nur AMPR EMU NG)**. Dazu wählst du die **Stufe** (LZ4: 9 ausgewogen, empfohlen, 3 schnell, 12 am kleinsten; zstd: 5 ausgewogen, empfohlen, 1 am schnellsten, 9 kleiner, 19 am kleinsten) und bei LZ4 die **Laufzeit im Spiel**: **AMPR EMU NG** (Vorgabe) oder **APR-EMU Original** (nur für Spiele, die libSceAmpr selbst laden; Dump Forge lehnt sie sonst ab). Das Abbild schreibt dann **Dump Forge** (mindestens Version 0.5.6; die App bringt 0.5.7 mit) und packt das Spiel **im Strom**: Die Dateien kommen beim Schreiben des Abbilds in die Packs, ohne Zwischenordner und ohne dass vorher ein gepacktes Spiel entsteht. Das Original bleibt unverändert. Die Schritte: Vermessen (die Packdateien werden einmal durchgerechnet, es wird nichts geschrieben; das Fenster sagt „Das Spiel wird für die Packs vermessen …“ und zeigt dann Prozent und Restzeit, eine Schätzung), Schreiben („wird gepackt und geschrieben“, mit Prozent und Restzeit statt Größen) und Prüfen. Dump Forge liest die Quelle dabei **zweimal**, darum dauert es länger als ohne Packen. Am Ende steht „<Größe> statt <Größe des Spiels> (n %)“.

Welche Dateien gepackt werden, bestimmt Dump Forge: Mit AMPR EMU NG (zstd, und LZ4 mit der Laufzeit NG) alles außer den Systemdateien (Programme, `sce_sys`, `sce_module`, `fakelib`, leere Dateien); mit „APR-EMU Original“ ein vorsichtigeres LZ4-Verfahren, das Medien, Konfigurationsdateien und die Dateien im Wurzelordner lose lässt. Ein Profil wie bei „Im Spielordner packen“ gibt es hier nicht. Bei zstd (für jedes Spiel) und bei LZ4 mit der Laufzeit NG legt Dump Forge die AMPR EMU NG (in Dump Forge 0.5.7: v0.4.1) selbst als `fakelib/libSceAmpr.sprx` ins Spiel; mit „APR-EMU Original“ kommt die in Dump Forge eingebaute APR-EMU hinein. Eine andere NG-Fassung lässt sich danach unter „APR-EMU im Spiel“ einbauen. „AMPR-Index vorher bauen“ entfällt (Dump Forge schreibt den Index selbst). Die Prüfung danach (**Gründlich** oder **Schnell**) bleibt und gilt für Dump Forge; eine Prüfsummen-Datei (`.sha256`) entsteht nicht. Der Name der Ausgabe darf höchstens 63 Bytes (ohne „.exfat“ oder „.ffpkg“) haben, wie beim FPKG; auf einem FAT32-Laufwerk ist die Grenze von 4 GB hier nur eine Warnung, weil das Abbild kleiner als das Spiel wird (wie viel, weiß erst Dump Forge). **Ausgegraut** ist die Wahl (mit dem Grund im Fenster) bei PS4-Spielen (sie haben keine AMPR-Packs), bei einem Spiel, das schon in LZ4- oder zstd-Packs liegt (erst bei den Asset-Packs des Spiels entpacken, dann konvertieren), und wenn sich nicht prüfen lässt, ob es Packs enthält. Für **ffpfsc** gibt es die Wahl nicht: Dort steckt die Komprimierung schon im Abbild. „Zur Warteschlange“ und „Original danach“ gehen wie bei den anderen Formaten; die Warteschlange nennt den Eintrag mit „LZ4-Packs“ oder „zstd-Packs“, der Verlauf mit „exfat + LZ4“. Probiere es zuerst an einem kleinen Spiel: Ob ein gepacktes Spiel läuft, hängt wie sonst von AMPR EMU NG (oder der APR-EMU) ab, und das Original bleibt ja unverändert.

**Dump Forge bei der reinen ELF-Version.** Wer nur die ELF benutzt (ohne Kachel), muss Dump Forge nicht mehr laden: Die App bringt Dump Forge 0.5.7 gepackt mit (seit 21.11.2026; die ELF ist dadurch rund 8 MiB größer), entpackt es für jeden Auftrag im Speicher, prüft Größe und Prüfsumme und sendet es an den Payload-Lader; auf die Konsole wird dafür nichts geschrieben. Eine neuere Fassung kommt auf zwei Wegen dazu: (1) **Laden lassen:** im Konvertieren-Fenster das Format „FPKG (Debug-.pkg)“ wählen und **Neue Fassung von GitHub laden** drücken; gibt es eine neuere Fassung, holt die App sie (rund 18 MB) von GitHub, vergleicht die Prüfsumme aus `SHA256SUMS` und legt sie ab, sonst meldet sie, dass die neueste schon da ist. (2) **Von Hand ablegen:** die Datei `ps5-dump-forge-VERSION-ps5.elf` (die Version nur aus Ziffern und Punkten und neuer als die eingebaute 0.5.7, zum Beispiel `0.5.8`) per FTP (Port 2121) oder mit dem Dateimanager nach `/data/PS5-Cooling-Center/dumpforge/` kopieren. Die App legt den Ordner bei jedem Start an. Die Datei muss 1 bis 40 MiB groß, eine ELF-Datei und vollständig sein (bei der eingebauten Version prüft die App die SHA-256-Prüfsumme, bei einer neueren, dass die Datei nicht abgeschnitten ist; eine Datei, die das nicht besteht, übergeht sie und nimmt die eingebaute Fassung); ein Neustart der App ist nicht nötig. Liegen mehrere Fassungen da, nimmt die App die höchste Version; bei gleicher Version gewinnt eine Datei im Ordner der App vor der eingebauten Fassung und vor den Ordnern der Kacheln, und eine Datei ohne Version im Namen (`ps5-dump-forge.elf`) ist älter als die eingebaute. Dump Forge startet und beendet die App für jeden Auftrag selbst.

**Prüfung vor dem Start, Name, Schritte, Warteschlange (seit 13.11.2026).** Das Fenster fragt bei jeder Wahl `POST /api/v1/library/convert/check`
und zeigt die Befunde als Liste (Rot sperrt Start und Warteschlange, Gelb warnt, ein anderer laufender Auftrag sperrt nur den Start; die
Grenzen für Namen und Pfade sind die von exFAT und der App). Seit dem 22.11.2026 steht dort gelb auch ein beschädigtes Bild des Startbildschirms
(`sce_sys/icon0.png`, `pic0.png`, `pic1.png`, `pic2.png`: kein gültiges PNG, geprüft werden Anfang, jeder Abschnitt mit seiner CRC-32 und das
Ende `IEND`; `src/pngcheck.h`), aus einem Ordner wie aus einem Abbild; die App übernimmt es unverändert, auf dem Startbildschirm kann es fehlen. Der Name der Ausgabe kommt aus einer Vorlage mit `{titel}`, `{id}`, `{version}` und
`{quelle}` (`src/cvname.c`); leer heißt wie bisher der Name des Ordners. Der Status hat `stages` (Vermessen, Schreiben, Prüfen, Umbenennen mit der
Zeit je Schritt). Die Warteschlange (`src/convqueue.c`) hält bis zu 24 Einträge im Speicher und startet sie nacheinander über
`ps5tm_gameconvert_start`; ein Eintrag, dessen Start an einem anderen Auftrag scheitert, wartet (alle zehn Sekunden ein neuer Versuch, ohne dabei
ShadowMountPlus zu fragen). Vor dem Schreiben nimmt der Auftrag einen Fingerabdruck der Quelle und vergleicht ihn vor dem Umbenennen: Hat sich eine
Datei der Quelle geändert, wird das Ergebnis nicht übernommen.

**Prüfsummen-Datei.** Neben dem Ergebnis entsteht `<Name>.sha256` (nicht bei einem Abbild mit Packs und nicht beim FPKG: dort prüft Dump Forge) mit dem SHA-256
im Format von `sha256sum` (je Zeile: Prüfsumme, zwei Leerzeichen, Name). Bei einem
Abbild steht ein Eintrag darin, bei einer Ordner-Kopie einer je Datei, mit
Pfaden ab dem Namen des Ordners. Am PC genügt in dem Ordner, der das Ergebnis
**und** die `.sha256` enthält:

```bash
sha256sum -c Spiel.ffpfsc.sha256      # Linux, WSL; auf dem Mac: shasum -a 256 -c …
```

So lässt sich eine Sicherung auch Monate später prüfen, etwa vor dem Löschen des
Originals. Die Datei ist reiner Text; jedes Werkzeug, das SHA-256 bildet, kann die
Werte vergleichen. Bei Spielbar-Kopien (`homebrew`) liegt sie im selben Ordner
wie die Kopie und stört ShadowMountPlus nicht: Es durchsucht nur Ordner und
Abbild-Dateien.

**Außerdem** (Ideen aus PS5 Game Compressor, kein Code von dort):

- **Kein Ruhemodus während eines langen Vorgangs.** Solange eine Kopie oder
  Konvertierung läuft, setzt die App alle zehn Sekunden die Uhr für den
  automatischen Ruhemodus zurück. Wer den Ruhemodus selbst wählt, bekommt ihn
  trotzdem; der Auftrag scheitert dann wie bei jeder Unterbrechung und räumt auf.
- **Lesen und Schreiben gleichzeitig, aber nur zwischen zwei Laufwerken.** Liegen
  Quelle und Ziel auf verschiedenen Laufwerken (interne SSD, M.2, ein USB-Stick),
  wird das Original schon gelesen, während das Ergebnis geschrieben wird. Auf einem
  einzigen Laufwerk würde das nur das Hin- und Herspringen verstärken; dort, und
  wenn es nicht sicher feststeht, läuft alles nacheinander wie bisher. Das Protokoll
  nennt die Wahl (`game_copy_io`, `game_convert_io`).
- **Nullen statt Lücken.** Im ffpfsc-Container ist jedes Byte geschrieben: Kopfbereich
  und Abschluss des Datenstroms ausdrücklich als Nullen, nicht als Loch in der
  Datei, das das Dateisystem erst füllen muss (auf USB-Laufwerken hat das beim
  Game Compressor zu Fehlern geführt). Die Prüfung kontrolliert diese Bereiche.
- **5-%-Regel.** Ein Block wird nur komprimiert abgelegt, wenn er mindestens 5 %
  spart. Sonst bleibt er roh: Die Konsole müsste ihn beim Spielen sonst für fast
  nichts entpacken. Das Abbild wird dadurch kaum größer.

**Wenn sich das Laufwerk abmeldet (seit 05.10.2026).** Ein USB-Laufwerk an einem schlechten
Kabel, einem wackligen Anschluss oder mit zu wenig Strom kann mitten im Schreiben vom Bus
fallen. Die Konsole hängt es dann aus, und jeder weitere Zugriff scheitert mit dem Systemfehler
„Device not configured“. Bis 1.48.0 stand nur dieser Satz in der Meldung. Jetzt heißt es beim
Kopieren, Konvertieren, Sichern und Zurückspielen von Spielständen und beim Aufteilen von Paketen:

> Der Datenträger hat sich abgemeldet (Device not configured). Meist ist das USB-Kabel, der
> Anschluss oder die Stromversorgung schuld: ein anderes, kurzes Kabel direkt an der Konsole
> probieren. Treten danach Störungen auf oder startet kein Spiel mehr, die Konsole neu starten.

Ein gewöhnlicher Lese- oder Schreibfehler („Input/output error“) bekommt einen kürzeren Satz ohne
den Rat zum Neustart. So sieht ein abgemeldetes Laufwerk meist aus: Die Anzeige bleibt bei 0 % oder
bei ein paar Prozent stehen, und nach etwa einer Minute scheitert der Auftrag (so lange wartet die
Konsole auf eine Antwort der Platte). Im Kernel-Log (Seite „Protokoll“, Reiter „Kernel-Log live“)
steht dann `umass0: USB_ERR_TIMEOUT`, das Laufwerk meldet sich ab (`detached`) und gleich darauf
wieder an. Am 05.10.2026 war genau das der Grund für vier abgebrochene Konvertierungen; mit einem
anderen Kabel liefen sie durch. Was der Auftrag geschrieben hat, räumt er weg. Nach einer
Konvertierung kann eine `….ps5cc-konv-teil`-Datei liegen bleiben (nach einem Absturz, einem Stromausfall oder wenn die
Platte beim Aufräumen fehlte, bis zur Größe des Abbilds). Ein neuer Auftrag mit **demselben Ziel** (gleicher Name, gleiches
Format, gleiches Laufwerk) ersetzt sie; bei einem anderen Namen oder Format bleibt sie zunächst liegen.
Außerdem räumt die App vor der ersten Konvertierung nach jedem Start solche Reste auf, die älter als eine Stunde sind (in den Zielordnern `homebrew`, `PS5-Sicherung/Spiele` und `FPKG-Games` jedes Laufwerks); was jünger ist oder woanders liegt, löschst du im Dateimanager.

## AMPR-Index, APR-EMU, PlayGo und der Verlauf (seit 11.10.2026)

Jede Spielkarte hat unter **Konvertieren** den Knopf **LZ4-/zstd-Asset-Packs …**. Er öffnet das Fenster „LZ4-/zstd-Asset-Packs & APR-EMU“, ein
Fenster mit Werkzeugen rund um APR-EMU; dazu gehören der **AMPR-Index**, „APR-EMU im Spiel“, „PlayGo im Spiel“, das Packen in LZ4- oder zstd-Pakete, die Optimierung per Mitschnitt, die Spiel-Plugins und bei einem Abbild Prüfen, Entpacken und Reparieren. Der **AMPR-Index**
(`ampr_emu.index`): die Liste aller Dateien des Spiels, die APR-EMU zum Finden
braucht. Das Fenster zeigt, ob es einen gibt und wie viele Dateien er kennt. Bei einem Abbild heißt der Knopf
**AMPR & APR-EMU …** und das Fenster „AMPR & APR-EMU“: LZ4 und zstd gehen nur bei
Dump-Ordnern.

- **Bauen / Neu bauen** legt ihn an. Bei einem Dump-Ordner geschieht das direkt, bei
  einem Abbild hängt ShadowMountPlus es kurz zum Schreiben ein und danach wieder aus.
  Ein alter Index bleibt als `ampr_emu.index.bak` liegen. Ein laufendes Spiel und
  jeder andere Vorgang der App sperren das Bauen.
- Beim **Konvertieren** eines Dump-Ordners gibt es das Kästchen **AMPR-Index vorher
  bauen** (bei Spielen mit APR-EMU schon angehakt). So kommt der Index mit ins
  Abbild; bei einem komprimierten Abbild (`.ffpfsc`) ist das der einzige Weg, denn
  dort lässt sich nachträglich nichts schreiben.
- Der Aufbau folgt `build_ampr_index.py` aus [ampr_emu](https://github.com/drakmor/ampr_emu)
  (drakmor, GPL-3.0) und ist mit dessen Ergebnis Byte für Byte verglichen: Kennung
  `AMPRIDX3`, Pfade klein geschrieben (nur ASCII), nach Schlüssel sortiert, mit
  FNV-1a-Tabelle. Namen, die sich nur in Groß-/Kleinschreibung unterscheiden, nimmt der
  Index einmal.

**APR-EMU im Spiel.** Die Zeile zeigt, welche `libSceAmpr.sprx` ein Spiel lädt, und lässt dich
eine Version einbauen, das Original zurückholen oder die Datei entfernen. Die Versionen liegen als
Dateien im Ordner `/data/PS5-Cooling-Center/AMPR_EMU`, benannt wie bei drakmors Veröffentlichungen
(`libSceAmpr.sprx-0.4.2.1-test-pack`). Sieben Fassungen (NG 0.5.0 und NG 0.5.0 debug, NG 0.4.1 und NG 0.4.1 debug, 0.4.2.1 test-pack, test-debug-pack,
test-nopack) sind in der App mitgeliefert: Sie stehen in der Auswahl, auch wenn sie noch nicht im Ordner liegen, und kommen
beim Einbauen oder mit „Mitgelieferte Versionen in den Ordner legen“ dorthin (Ordner `AMPR_EMU`, wird bei Bedarf angelegt).
Weitere Fassungen legt „Datei hinzufügen“ im Fenster in denselben Ordner.
Die Liste der Versionen hat drei Gruppen mit eigenem Titel: **AMPR EMU NG @quer3q** (alle NG-Fassungen, auch die später dazukommenden),
**AMPR EMU @Drakmor** (die Fassung ohne Paketunterstützung, sie heißt dort nur „0.4.2.1“) und **LZ4 Asset Pack Version** (die beiden
Fassungen nur für LZ4-Pakete, `test-pack` und `test-debug-pack`).
**AMPR EMU NG** (quer3q, eine Weiterentwicklung, die LZ4- und zstd-Pakete liest) steht mit der Marke „NG“ ganz oben
(„NG 0.5.0“, „NG 0.5.0 debug“, „NG 0.4.1“, „NG 0.4.1 debug“). Zwei Fassungen stehen zur Auswahl; mitgeliefert sind `libSceAmpr.sprx-0.5.0-ng`, `-ng-debug`, `libSceAmpr.sprx-0.4.1-ng`, `-ng-debug`. Der Knopf **Auf neue AMPR-EMU-NG-Fassung prüfen und laden** fragt GitHub nach der neuesten **stabilen** Veröffentlichung (eine Beta nimmt die App nie); ist sie neuer als alle angebotenen, lädt die App ihre beiden Dateien in den Ordner `AMPR_EMU`, prüft sie gegen die `SHA256SUMS` der Veröffentlichung und die Kennung, stellt sie ganz oben in die Liste und lässt die älteste Fassung entfallen (ihre von der App abgelegten Dateien werden gelöscht, eigene Dateien bleiben liegen). Geladen wird nur auf Knopfdruck. Dateien einer neueren NG-Veröffentlichung
(`libSceAmpr.sprx`, `libSceAmpr-debug.sprx`) benennt „Datei hinzufügen“ selbst passend um. ShadowMountPlus ersetzt die
Datei beim Spielstart aus seinem Emulator-Ordner, solange seine automatische Fakelib-Aktualisierung an ist
(`update_emulators=1`, ab 1.7beta12 `update_enabled=1` unter `[emulators]`). Deshalb **fragt die App beim Einbauen von NG**, ob sie
abgeschaltet werden soll: **Ja** setzt nur die Zeile `update_emulators` (ab 1.7beta12 `update_enabled` unter `[emulators]`) in
`/data/shadowmount/config.ini` auf 0 (die Datei wird einmal als `config.ini.ps5cc-bak` gesichert; je nach Fassung übernimmt
ShadowMountPlus die Änderung sofort oder erst beim nächsten Start) und baut NG ein, **Abbrechen** lässt alles, wie es ist.
Ab 1.7beta12 benutzt ShadowMountPlus ein neues Format der `config.ini` (Abschnitte, `[config] version = 2`) und wandelt die alten
Dateien beim ersten Start um (die alten bleiben in `config-v1-backup`); die App versteht beide Formate und lässt Kommentare,
Leerzeichen und alles andere in der Datei, wie es ist. Welche Version bei dir läuft, erkennt die App an der Datei (Standard: **Automatisch erkennen**);
festlegen lässt es sich mit **Ab 1.7beta12** oder **Vor 1.7beta12** im Browser auf der Seite **System** ganz oben in der Karte
**ShadowMountPlus** und in der Vollbild-App unter **System → ShadowMountPlus**. Welche Zeile geschrieben wird, bestimmt die Datei selbst, sobald
sie ihr Format verrät; deine Angabe gilt für eine fehlende oder leere `config.ini` und löst einen Hinweis aus, wenn sie nicht zur
Datei passt (etwa wenn ShadowMountPlus nach dem Update noch nicht gestartet wurde und die Datei noch die alte ist: es wandelt sie
beim ersten Start um und übernimmt die Zeile). Ist die Aktualisierung schon aus, kommt die Frage nicht. Dieselbe Einstellung steht auch als Schalter **Fakelib automatisch aktualisieren** in der Karte **ShadowMountPlus** auf der Seite **System** (Vollbild-App: **System → ShadowMountPlus**); dort lässt sie sich jederzeit von Hand ein- oder ausschalten. Wohin die
Datei im Spiel kommt, folgt ShadowMountPlus ab 1.7beta1 (Suchreihenfolge: Backport-Ordner
`<Suchpfad>/backports/<Titel-ID>/` (seit 1.7beta6 auch ein Ordner, dessen Name nur mit der Titel-ID beginnt, etwa „PPSA12345 Spielname“: der genau so benannte gewinnt, sonst der erste in Byte-Reihenfolge) mit `fakelib2` vor `fakelib`, dann das Spiel mit `fakelib2` vor
`fakelib`; der erste vorhandene Ordner gilt allein, auch ein leerer):

- **Spielordner, `.exfat`, `.ffpkg`:** `fakelib/libSceAmpr.sprx` im Spiel; ein Abbild hängt ShadowMountPlus
  dafür kurz zum Schreiben ein. Den Namen `fakelib` lesen alle Fassungen; ein `fakelib2` daneben würde ab
  1.7beta1 gewinnen und die Datei ungenutzt lassen, davor warnt das Fenster.
- **Komprimiertes `.ffpfsc`:** nicht beschreibbar, also ein Backport-Ordner; das Spiel bleibt unberührt.
  Weil dieser die `fakelib` des Spiels verdeckt, werden deren Dateien (außer `libSceAmpr.sprx`)
  hineinkopiert. Ein schon vorhandener Backport-Ordner gilt für jedes Format. Ein `fakelib2` im Abbild oder
  im Backport sperrt die Änderung.
- **Wahl bei einem Abbild (`.exfat`, `.ffpkg`, seit 19.11.2026):** Im Fenster „Version wählen“ steht „Wohin kommt die Datei?“:
  **Im Abbild** (Vorgabe) oder **Im Backport-Ordner** neben dem Abbild (`<Ordner des Abbilds>/backports/<Titel-ID>/fakelib`). Der
  Backport-Ordner lässt das Abbild unberührt, hängt es nicht ein und geht auch, wenn das Abbild voll ist. Ist es voll und nichts
  gewählt („No space left on device“), legt die App die Datei selbst im Backport-Ordner ab und sagt es im Ergebnis. Wo die Ablage
  feststeht (ein komprimiertes `.ffpfsc` oder ein schon vorhandener Backport-Ordner), gibt es keine Wahl. In der Vollbild-App ist es
  die Zeile „Ablage“ in der Versionsliste.
- Die Datei des Spiels wird einmal als `libSceAmpr.sprx.orig` gesichert und nie überschrieben; ein früherer
  APR-EMU (Kennung `[AMPR_EMU]`) oder PlayGo-Stub gilt nicht als Original. Ein leer werdender Backport-Ordner
  wird wieder entfernt, denn ein leerer Ordner würde die `fakelib` des Spiels verdecken.
- Hinweise: `update_emulators=1` (ab 1.7beta12 `update_enabled` unter `[emulators]`) in der `config.ini` von ShadowMountPlus ersetzt die Datei beim Spielstart aus
  `/data/shadowmount/emus` (gewinnt dann); ein Spiel in LZ4-Paketen nimmt nur Versionen mit „pack“.
- Vorbild und Regeln: der „PS5 Dump & Image Converter“ (fakelib-Manager) und die Suchreihenfolge von
  ShadowMountPlus 1.7; Versionen und Bauarten (`test-pack`, `test-nopack`, `test-debug-pack`, `test`,
  `test-debug`) laut [drakmor/ampr_emu](https://github.com/drakmor/ampr_emu) (GPL-3.0).

**LZ4-Asset-Packs (Kurzfassung).** Die Zeile packt die Spieldaten eines Spiels in seekbare LZ4-Pakete (`ampr_assets-NNN.pak`, Verzeichnis
`ampr_assets.index`, dazu `ampr_assets.index.crc` zum Prüfen), die die APR-EMU beim Spielen liest. Nur für Spiele als Ordner und
nur mit einer APR-EMU-Version, die Pakete liest („pack“): Die App legt `0.4.2.1 test-pack` nach `fakelib` (das Original als `.orig`).
Das Einstellungsfenster fragt: Stufe 3 (LZ4 Fast), 9 oder 12 (LZ4 HC); „Automatisch“ (Programme, `sce_sys`, `fakelib`, Spielstände,
komprimierte Medien und kaum packbare Riesendateien bleiben lose) oder ein eigenes Profil (TOML wie drakmors `ampr_pack.py`, im Ordner
`/data/PS5-Cooling-Center/LZ4_Profile` oder `LZ4_Profile` auf einem USB-Laufwerk); „im Spielordner packen“ (AMPR-Index neu bauen, packen,
jede Datei Byte für Byte mit dem Original vergleichen, erst dann die Originale entfernen; bei jedem Fehler werden die Pakete wieder
entfernt) oder „als neuer Ordner `<Name>-lz4`“ (Original unberührt; unsignierte Programmdateien stehen dort als FSELF, siehe „Spiele kopieren und konvertieren“). Danach: **Pakete prüfen** (jeder Block gelesen, entpackt, gegen die
CRC-Datei geprüft) und **LZ4 entpacken** (zurückschreiben, vergleichen, Pakete entfernen). Solange ein Spiel gepackt ist, ist der
AMPR-Index gesperrt. Format und Regeln nach [drakmor/ampr_emu](https://github.com/drakmor/ampr_emu) (GPL-3.0), LZ4 1.11.1 (BSD-2).

**PlayGo im Spiel.** Die zweite Zeile darunter arbeitet genauso für `libScePlayGo.sprx`, den PlayGo-Stub von
drakmor ([pgo_stub](https://github.com/drakmor/pgo_stub), GPL-3.0): Er meldet einem Spiel, dessen PlayGo-Inhalte nicht
(mehr) zu finden sind, alles als vollständig installiert; er hilft nur bei Spielen, die PlayGo-Inhalte als fehlend
behandeln. Die Versionen liegen im Ordner `/data/PS5-Cooling-Center/PlayGo` (`libScePlayGo.sprx-0.6-nolog`,
`libScePlayGo.sprx-0.6-log`); beide Fassungen 0.6 sind in der App mitgeliefert, „log“ schreibt `/app0/playlgo.log` in
das Spiel. Jede Datei wird geprüft (SELF-Kopf, Kennung `playgo_stub.dat`). Ort (Spiel oder Backport-Ordner), Sicherung
als `libScePlayGo.sprx.orig`, Warnungen und Entfernen gelten wie bei der APR-EMU-Datei; LZ4-Pakete spielen für PlayGo
keine Rolle. Beide Dateien eines Spiels lassen sich unabhängig voneinander ändern; ein Stub im Emulator-Ordner von
ShadowMountPlus (`/data/shadowmount/emus/libScePlayGo.sprx`) gewinnt auch hier.

**Spiel-Plugins.** Die Zeile **Spiel-Plugins** hält die Ersatzmodule von drakmor im Ordner `fakelib` des Spiels auf dem neuesten Stand: den PlayGo-Stub (`libScePlayGo.sprx`, pgo_stub 0.6) und die DLC-Module (`libSceAppContent.sprx`, `libSceGameUpdate.sprx`, `libSceNpEntitlementAccess.sprx`, dlc_emu 0.3, Fassung „log“: Sie schreibt `/app0/dlc_emu.log` in das Spiel; die Fassung ohne Protokoll stürzte beim Spielstart ab und wird als „fehlerhafte Fassung“ ersetzt), die DLC aus der `dlc_emu.ini` des Spiels als gekauft zeigen.

- Eine ältere Fassung (die App kennt sie an der Prüfsumme) wird ersetzt, die alte Datei bleibt als `.old`. Eine Datei in unbekannter Fassung bleibt unberührt.
- Der PlayGo-Stub wird nie zu einem Spiel hinzugefügt, das keinen hat; das geht in der Zeile „PlayGo im Spiel“. Die DLC-Module kommen nur dazu, wenn im Spielordner eine `dlc_emu.ini` liegt (ohne sie würden sie die echten DLC verdecken).
- Fehlt einer Bibliothek das Ausführungsrecht (so legte die App sie bis zum 13.10.2026 an), setzt „Aktualisieren“ es. Ohne das lädt die Konsole sie nicht („mount flag / attribute error“).
- Liegt im Spiel ein `fakelib2` oder liest ShadowMountPlus einen Backport-Ordner, ändert die App nichts.


**Abbild prüfen, entpacken, reparieren (Schritt 2).** Liegt das Spiel als Abbild vor
(`.exfat`, `.ffpkg`, `.ffpfsc`), hat dasselbe Fenster drei weitere Werkzeuge; die App
liest das Abbild selbst, ohne es einzuhängen:

- **Abbild prüfen:** Bei `.ffpfsc` wird zuerst der ganze Container gelesen (Kopf,
  Blocktabelle, jeder Block entpackt, jeder Block trägt eine Prüfsumme), dann wird
  geprüft, ob die Dateien darin dort liegen, wo das Dateisystem es sagt. Bei `.exfat`
  und `.ffpkg` wird jede Datei vollständig gelesen. Findet die Prüfung etwas, nennt das
  Fenster die betroffenen Dateien mit der Stelle, ab der es hakt.
- **In einen Ordner entpacken:** nach `homebrew/<Name des Abbilds>` auf einem
  Laufwerk deiner Wahl (das Fenster zeigt Platz und Dateisystem; FAT32 nimmt keine
  Dateien über 4 GB). Geschrieben wird in einen Ordner mit der Endung `.ps5cc-teil`;
  jede Datei wird danach vom Laufwerk zurückgelesen und per CRC-32 mit dem verglichen,
  was aus dem Abbild kam. Erst wenn alles stimmt, bekommt der Ordner seinen Namen; bei
  Fehler oder Abbruch wird er entfernt. Das Abbild bleibt unberührt.
- **Reparieren:** nur, wo es sicher geht. Stimmt die Größe einer `.ffpfsc`-Datei nicht
  mit dem Kopf überein, die Daten sind aber vollständig und die überzähligen Bytes leer,
  setzt die App die Größe richtig und liest den Container danach noch einmal ganz.
  Defekte Blöcke lassen sich ohne Original nicht herstellen. Gesperrt, solange
  ShadowMountPlus das Abbild eingehängt hat.

Das Ergebnis der letzten Prüfung jedes Abbilds bleibt bis zum Neustart der App im Fenster
stehen; jeder Vorgang steht außerdem im Verlauf.

Der Reiter **Verlauf** (Spiele-Seite) zeigt alle Vorgänge der App – Kopieren,
Konvertieren, Paket teilen, Installieren, Löschen, Dateimanager, Spielstände,
Disc-Abbilder, Dumps, AMPR-Index, APR-EMU und PlayGo, LZ4-Pakete sowie Abbild prüfen, entpacken und reparieren – mit Ergebnis, Größe, Dauer und Zeit, nach Ergebnis
filterbar. Die letzten 500 liegen auf der Konsole in
`/data/PS5-Cooling-Center/history.jsonl`; **Verlauf leeren** löscht sie nach einer Rückfrage.

### PS4-Spiele von USB (PS4Mount)

Der Reiter **PS4 PKG von USB** auf der Spiele-Seite spielt PS4-Spiele (Fake-PKG) direkt vom USB-Laufwerk, ohne sie zu installieren. Er beruht auf PS4Mount von gasb07 (GPL-3.0), dessen Quelltext in die App übernommen ist.

1. **Vorbereiten.** Auf einem exFAT-Laufwerk liegen Spiel, Update und Zusatzinhalte als `.pkg` irgendwo auf dem Laufwerk, in beliebigen Ordnern und mit beliebigen Dateinamen (in den Einstellungen lässt sich die Suche auf bestimmte Ordner beschränken). Sie gehören zusammen, wenn sie dieselbe Titel-ID haben. Die Pakete bleiben dort, wo sie liegen; die App verschiebt nichts (seit PS4Mount 0.8.0).
2. **Einschalten.** Der Schalter „Einschalten“ startet die Suche; er bleibt über einen Neustart der App hinweg an. „Jetzt suchen“ löst eine Suche sofort aus. Seit 21.11.2026 ist PS4Mount ab Werk ausgeschaltet (vom 17. bis 21.11.2026 war es eingeschaltet; bei einer neuen Installation, ohne gespeicherte Einstellung, `config.txt` in den Daten von PS4Mount); wer es einschaltet, bei dem bleibt es an, und wer es ausschaltet, bei dem bleibt es aus: Die App speichert beides in dieser Datei. Browser-Seite und Vollbild-App zeigen den Schalter dann als „an“ oder „aus“.
3. **Warten.** Die App liest das Laufwerk einmal in eine Übersicht (jede PKG nur einmal) und meldet jedes Spiel, sobald es spielbereit ist. Das Spiel erscheint mit Symbol auf dem Startbildschirm. Gibt es für ein Spiel mehrere Updates, wird das neueste benutzt („Updates“ lässt ein anderes wählen); liegt dasselbe Spiel auf zwei Laufwerken, behält das zuerst angemeldete es.
- Die PKG-Dateien bleiben auf dem Laufwerk; auf die Konsole kommen nur kleine Verwaltungsdateien. Löscht man ein Spiel auf der Konsole oder mit „Kachel entfernen“, verschwindet nur die Kachel, die PKG bleibt; „Kachel wieder anlegen“ holt sie zurück.
- **Gefunden, nicht eingebunden** (seit 29.10.2026, bis 23.11.2026 „Auf USB gefunden, …“): Unter den Spielen zeigt der Reiter die PKGs, die die Suche liegen lässt, jeweils mit dem Grund: schon auf der Konsole installiert (dann erst das installierte Spiel löschen, um es von USB zu spielen), noch nicht fertig kopiert (die Datei ist kleiner, als ihr Kopf sagt), kein PS4-Paket (etwa ein PS5-PKG, das der Reiter „Pakete“ installiert), kein Fake-PKG, Zusatzinhalt ohne PS4-Spiel oder bei ausgeschalteten Zusatzinhalten, zweite Kopie einer benutzten Datei, beschädigtes Spielpaket (seit PS4Mount 0.7.1: fehlerhafte Kennungen in der param.sfo; so ein Paket kann die Konsole ausschalten und ihren Speicher beschädigen, darum wird es nie angemeldet), auf der M.2-Erweiterung (`m2`, seit 23.11.2026: `/mnt/ext1` wird weiter durchsucht, aber nichts davon angemeldet; an der Slim meldete PS4Mount World War Z mit drei Zusatzinhalten von dort an, jeder Start endete aber in der Konsole selbst – `process_mount`, PlayGo `0x80B2150F`, Fehlermeldung am Fernseher –, dieselbe PKG vom exFAT-Stick startete samt Zusatzinhalten; `/mnt/ext0` ist nicht erprobt und bleibt wie bisher). Die Liste steht auch in der Vollbild-App.
- Updates (`patch.pkg`) und Zusatzinhalte (DLC) werden mit eingebunden; Zusatzinhalte nur, wenn der Schalter in den Einstellungen an ist.
- **Laufwerk unter anderem Namen** (seit PS4Mount 0.6.1): Kommt dasselbe Laufwerk nach einem Neustart als `usb1` statt `usb0` wieder, bleiben die Spiele angemeldet; die App schreibt nur die Verknüpfungen auf den neuen Ordner um.
- **Keine zusammengeführten PKGs:** Spiel-PKGs, in die Patches, Zusatzinhalte oder Updates schon eingebaut sind, laufen womöglich nicht. Besser das ursprüngliche Spiel-PKG nehmen und Update und Zusatzinhalte als eigene `.pkg`-Dateien daneben legen. Eine Spiel-PKG wird nur aus dem Ordner ihrer eigenen Titel-ID genommen.
- **Neue Spiele schneller anmelden** (seit PS4Mount 0.6.0, in den Einstellungen, ausgeschaltet): Die Anmeldung der Konsole (`AppInstallAll`) geht jeden Ordner in `/user/app` durch, für jedes PS4-Spiel etwa eine Dreiviertelsekunde, auch für die schon angemeldeten. Mit dem Schalter zeigt die Konsole ihr nur die neuen Spiele: Über `/user/app` liegt ein leerer Ordner, und darauf kommt der Ordner jedes neuen Spiels. Die anderen Spiele bleiben angemeldet; für höchstens zwei Minuten sind sie nur unsichtbar. Das geschieht nur, wenn kein Spiel läuft und die Firmware den Weg über `AppInstallAll` nimmt; in dieser Zeit weist die App Kachel anlegen, Kachel entfernen und „PKGs zurückverschieben“ mit „beschäftigt“ ab. Endet die App in dieser Zeit, räumt sie beim nächsten Start (`[reg] /user/app showed only … game(s) from an earlier run`) auf; ein Neustart der Konsole genügt ebenfalls, denn die Einbindung bleibt nicht bestehen.
- Die App muss laufen, damit die Spiele nach einem Neustart der Konsole sichtbar bleiben. Läuft PS4Mount schon als eigenes Payload (`ps4mount.elf`), lädt die App ihr PS4Mount nicht ein zweites Mal: Der Reiter zeigt „wartet“, und sobald das eigenständige beendet ist, übernimmt die App von selbst. Erkannt wird es an seinem Web-Panel (Port 8770), an der Sperre auf Port 9032 oder am Prozessnamen; taucht es später doch auf, etwa nach dem Ruhemodus, hält die App ihres an. Umgekehrt startet die Seite Payloads (auch ein Startprofil) keine `ps4mount.elf`, solange schon ein PS4Mount läuft.
- Firmware (seit PS4Mount 0.4.4): Ab 6.00 meldet die Konsole die Spiele auf dem üblichen Weg an (geprüft auf 13.20, gemeldet auf 6.50); unter 6.00 einzeln, und fehlt dort die nötige Funktion, werden zum Schutz der Datenbank keine Spiele hinzugefügt. Die Meldung beim Einschalten nennt den Weg. An einer PS5 Pro mit 12.00 ist der Einbau geprüft: PS4-Spiele vom USB-Laufwerk werden eingebunden und laufen. Er ist ausgeschaltet, bis man ihn einschaltet.

**PKGs zurückverschieben** (seit PS4Mount 0.5.0; seit 0.8.0 bleibt das Spiel dabei in der Bibliothek): Frühere Versionen von PS4Mount haben die PKGs
nach `ps4/CUSAxxxxx` verschoben und sich in `ps4/CUSAxxxxx/origin.txt` gemerkt, woher jede PKG kam. „PKGs zurückverschieben“ bei einem Spiel
(nur bei Spielen mit PKGs in `ps4/CUSAxxxxx`), „Ausgewählte zurückverschieben“ oder „Alle zurückverschieben“ legt Spiel, Update, ältere Updates
und Zusatzinhalte mit ihren ursprünglichen Namen an ihren alten Platz zurück. Das Spiel bleibt in der Bibliothek und wird von dort weitergespielt;
wer es nicht mehr von USB spielen will, nimmt die Kachel mit „Kachel entfernen“ heraus. Ein laufendes Spiel wird vorher geschlossen, auch ein
PS5-Spiel. Fehlt ein Ordner, wird er angelegt; eine vorhandene Datei wird nie ersetzt (dann kommt „ (1)“ an den Namen). PKGs, die ältere
Versionen verschoben haben, landen in der obersten Ebene als `CUSAxxxxx - Name - game.pkg`. Keine PKG wird gelöscht.

- **Vor dem Ruhemodus PS4Mount-Spiele schließen.** Der Ruhemodus schaltet das Laufwerk ab; ein offenes Spiel kann danach nicht
  mehr lesen und hängt.
- **Ausschalten hängt die Spiele aus** (seit PS4Mount 0.7.2): Wer vor dem Ruhemodus PS4Mount ausschaltet, ist sicher. Die App hängt dabei jedes Spiel aus, ohne Zwang; läuft gerade ein Spiel, lehnt sie ab und nennt es. Ist ein Spielordner noch in Benutzung, hängt sie alle wieder ein und bittet, es gleich noch einmal zu versuchen. Eingeschaltet hängt PS4Mount die Spiele wieder ein.
- Zwei oder mehr angeschlossene Laufwerke (auch Partitionen) können die Oberfläche der Konsole träge machen, mit oder ohne
  PS4Mount. Dann nur ein Laufwerk mit einer Partition anschließen.
- PS4Mount verwaltet seit 0.7.3 bis zu 768 Spiele je Laufwerk (vorher 256) und durchsucht bis zu 64 Ordner (vorher 8). Was
  darüber liegt, meldet die App, statt es still zu übergehen.
- Ein laufendes Spiel kann höchstens etwa 30 Pakete nutzen (Spiel, Update und rund 29 Zusatzinhalte mit Daten; reine
  Freischaltungen zählen seit PS4Mount 0.7.3 nicht mit). Das ist eine Grenze der
  Konsole, auch für installierte Spiele.

### (F)PKG-Spiele mit Kraken Mount (FPKGMount)

Der Reiter **FPKGMount** neben „PS4 PKG von USB“ spielt (F)PKG-Spiele und Spielordner (roh, zstd, LZ4) direkt von der Platte oder vom USB-Laufwerk, ohne sie zu installieren. Dafür bringt die App das Programm **Kraken Mount** von quer3q mit (GPL-3.0, Version 0.3.1; ein eigenes Programm, nicht in die App umgeschrieben), startet es und zeigt seine Spiele.

> **Kraken Mount ab Version 0.2.0:** Das Programm selbst wartet nach dem Start, bis ShadowMountPlus beendet ist, und meldet bis dahin kein Spiel an. Damit FPKGMount wie bisher neben ShadowMountPlus arbeitet, ändert die App in der eingebauten Fassung (und in einer von GitHub geladenen) nur den Namen des Prozesses, auf den es wartet (`shadowmountplus.elf`, ein Byte): Es findet dann keinen und wartet nicht. Das Warten auf kstuff bleibt. Eine ELF von Kraken Mount, die du selbst an den ELF-Lader sendest, wartet weiter auf ShadowMountPlus; die Seite zeigt es dann als „wartet auf ShadowMountPlus“.

- **Vorbereiten:** Ein unverschlüsseltes Debug-FPKG (zum Beispiel aus Dump Forge) als `.pkg` kommt in den Ordner `FPKG-Games`: auf dem internen Speicher `/data/FPKG-Games` (die App legt ihn beim ersten Start an), auf einem USB- oder M.2-Laufwerk der Ordner `FPKG-Games` im Hauptordner (den legst du selbst an). Nur diese Ordner durchsucht Kraken Mount, und ShadowMountPlus sucht dort nicht: So kommen sich die beiden nicht in die Quere, und deine Ordner-Spiele und Abbilder bleiben bei ShadowMountPlus. Die Suchordner stehen in der Konfiguration von Kraken Mount, die die App beim ersten Start anlegt, wenn es noch keine gibt; eine vorhandene bleibt, wie sie ist (die Seite nennt die Suchordner, die gerade gelten, ändern lassen sie sich auf der eigenen Seite von Kraken Mount, Port 8097). Backports kommen nach `<Suchordner>/backports/<TITEL-ID>*`.
- **Einschalten:** „Einschalten“ sendet das Programm an den ELF-Lader. Jedes Spiel bekommt eine Meldung und eine eigene Kachel. Eine Kachel für Kraken Mount selbst legt die App nicht an (es gibt nur die Seite in der App, wie bei „PS4 PKG von USB“); eine früher von Kraken Mount angelegte Kachel entfernt sie beim nächsten Start. Beenden geht über denselben Schalter und wird abgelehnt, solange ein Spiel läuft. Seit 21.11.2026 ist Kraken Mount ab Werk ausgeschaltet (vom 17. bis 21.11.2026 startete die App es von selbst). Schaltest du es ein, startet die App es bei jedem eigenen Start von selbst (nach etwa 5 Sekunden); die App merkt sich das Einschalten wie das Ausschalten (Datei kraken/zustand.txt im Datenordner der App). Läuft nach einem Update der App noch die ältere Fassung von Kraken Mount (es läuft unabhängig von der App weiter), sagt die Seite es: Aus- und wieder Einschalten startet die neue.
- **Liste:** Name, Titel-ID, Version, Größe, Laufwerk, Art (FPKG oder Ordner) und Zustand. **Neu aufbauen** baut den Zwischenspeicher eines Spiels neu (er liegt immer auf der internen SSD unter `/data/krakenmount/caches`). **Spiel löschen** fragt vorher und löscht die PKG-Datei bzw. den Spielordner, die Kachel und den Zwischenspeicher; Spielstände bleiben. **PlayGo-Stub ausschalten** bzw. **einschalten** (ab Kraken Mount 0.3.1; nur bei Spielen, die PlayGo laden) schaltet den mitgelieferten PlayGo-Stub nur für dieses Spiel ab, wenn es mit ihm auf einem schwarzen Bildschirm bleibt (Resident Evil Village und einige andere); wieder einschalten, wenn das Spiel verlangt, den Download abzuschließen. Kraken Mount merkt es sich in seiner Einstellung `playgo_off`.
- **Neue Fassung:** „Neue Fassung von GitHub laden“ holt die neueste Veröffentlichung (nur auf Knopfdruck, per HTTPS, mit der Prüfsumme aus `SHA256SUMS`); der nächste Start nimmt sie. Die Einstellungen von Kraken Mount (Suchordner, Fakelib) bleiben in seiner eigenen Seite (Port 8097, Link im Reiter).
- **Neu in Version 0.3.0 (18.11.2026):** Spielstände nutzen nicht mehr die `libSceSaveData.native.sprx` aus dem `fakelib` eines Spiels (sie ließ Spielstände verschwinden); `.pkg`-Spiele bekommen immer ampr-emu-ng; die mitgelieferten Emulatoren für DLC (dlc_emu 0.3) und PlayGo (pgo_stub 0.6) kommen in den `fakelib` eines Spiels, das sie braucht und keine neuere Kopie mitbringt; ein Spiel, das noch kopiert wird, wird übersprungen, bis sich seine Dateien 10 Sekunden lang nicht mehr ändern. **Fehlt die Quelle eines Spiels 5 Minuten lang** (zum Beispiel ein abgezogenes Laufwerk), nimmt Kraken Mount es vom Startbildschirm; der Zwischenspeicher bleibt, und das Spiel kommt mit der Quelle zurück. Auf der eigenen Seite von Kraken Mount (Port 8097) hat jedes USB-Laufwerk einen Auswerfen-Knopf, dort stehen auch die neuen Einstellungen (`add_emulators`, `stability_wait_seconds`, `auto_remove_missing`). **Beim ersten Start nach dem Wechsel** setzt Kraken Mount den `fakelib` jedes Spiels einmal neu zusammen; das dauert etwas.
- **Neu in Version 0.3.1 (20.11.2026):** Auf der Firmware 12 und neuer melden sich Spiele wieder am Startbildschirm an: Fehlt der Aufruf für ein einzelnes Spiel, bereitet jeder Suchlauf seine Spiele vor und meldet sie am Ende mit einem Aufruf an (eine Meldung für die ganze Gruppe). Neu ist der Schalter **PlayGo-Stub** je Spiel (siehe oben). Spiele in einem Ordner direkt in einem Suchordner werden jetzt auch gefunden; die Anmeldung eines Spiels geht schneller (etwa 21 statt 36 bis 44 Sekunden); jede Zeile in `kraken.log` beginnt mit der Uhrzeit; ein falscher Eintrag in einer Liste der Einstellungen wird übersprungen, die übrigen bleiben. Mitgeliefert ist AMPR EMU NG 0.5.0.
- **Marke (seit 17.11.2026):** Die Karte eines Spiels, das Kraken Mount eingehängt hat, trägt die Marke **FPKGMount** (wie **PS4Mount** bei den PS4-Spielen von USB), Browser und Vollbild-App: Es wurde nicht von einem Paket-Installer installiert, sondern läuft aus dem Debug-Paket. Die Marke gilt, solange Kraken Mount läuft und das Spiel auflistet (`fpkgmount` in `GET /api/v1/library`).
- **Hinweise:** Kraken Mount ist eine Beta; als `.pkg` läuft ein Spiel womöglich schlechter als als zstd-Ordner. Stellst du Kraken Mount auf Ordner um, die auch ShadowMountPlus durchsucht (etwa /data/homebrew), würde jedes Ordner-Spiel dort doppelt angemeldet. Nach einem Neustart der Konsole muss Kraken Mount laufen, damit die Spiele sichtbar bleiben. Die Vollbild-App hat dieselbe Seite unter Spiele → FPKGMount (Quadrat: Neu aufbauen, Löschen; Dreieck: Start/Beenden, Suchen, neue Fassung, Protokoll). Ein Spiel, dessen `param.json` „upgradable“ als `applicationDrmType` trägt (das haben viele Abbilder), zeigt die Konsole mit einem Vorhängeschloss an der Kachel, weil Kraken Mount die Datei unverändert anmeldet; ShadowMountPlus schreibt sie dabei auf „standard“ um. Starten lässt sich das Spiel trotzdem.
- **Derselbe Titel bei zwei Einhängern (seit 14.11.2026, `src/mounters.c`).** Kraken Mount, ShadowMountPlus und PS4Mount melden jedes Spiel bei der Konsole an und hängen es ein (`/user/app/<ID>`, `/system_ex/app/<ID>`). Liegt dieselbe Titel-ID bei zwei von ihnen (so am 14.11.2026 mit Alex Kidd: eine `.pkg` bei Kraken Mount, ein Abbild bei ShadowMountPlus), kommen sie sich in die Quere: Kraken Mount hängt die Einhängung von ShadowMountPlus aus (Protokoll „not ours, or stale; unmounting it“), es gibt `Device busy`, und die Kachel unterscheidet sich (Kraken Mount meldet die `param.json` unverändert an, ShadowMountPlus schreibt sie um). Die App meldet es im Reiter FPKGMount (Browser und Vollbild-App) mit einem gelben Achtung-Hinweis; in der Spieleliste gibt es dazu keine Warnung. Sie verschiebt nichts selbst; das Spiel soll nur bei einem von beiden liegen. Auch **Spielordner im Suchordner von Kraken Mount** (`FPKG-Games`) meldet der Reiter („nur .pkg gehört hierher“): Ordner-Spiele gehören zu ShadowMountPlus. Ein Schalter, der Kraken Mount auf `.pkg` beschränkt, fehlt in Kraken Mount selbst (Einstellungen: nur Suchordner, Fakelib, Backports, Intervall, Speichergrenze, Meldungen, Protokoll); die App liefert dessen ELF unverändert aus.

Lege FPKG-Spiele nur in den Ordner `FPKG-Games` (intern, M.2-SSD oder USB): Dort sucht ShadowMountPlus nicht. Beim Konvertieren in das Format FPKG ist dieser Ordner deshalb vorgewählt („A · Spielbar“). Liegt ein Spiel trotzdem bei Kraken Mount und bei ShadowMountPlus und wird beendet, hängt die App es bei ShadowMountPlus aus (wie der Knopf „Aushängen“ der Karte); Spiele, die nur bei einem der beiden liegen, und die Kacheln bleiben unberührt.

## LZ4-Asset-Packs: Schritt für Schritt (seit 12.10.2026)

Ein **Asset-Pack** besteht aus wenigen Dateien (`ampr_assets-000.pak` und weitere, dazu `ampr_assets.index`). Die App packt die
Spieldaten eines Spiels hinein und verkleinert sie mit LZ4. Die APR-EMU liest sie beim Spielen und entpackt jeden Block erst, wenn
das Spiel ihn braucht. Laut dem Entwickler (drakmor, *ampr_emu*) halbiert das den Platzbedarf und lädt oft 30 bis 40 Prozent
schneller. Die Version 0.4.2.1 ist ausdrücklich eine Testversion: **nicht jedes Spiel verträgt das.**

### Was nötig ist

- Das Spiel liegt als **Ordner** vor. Ein Abbild bringst du vorher mit **In einen Ordner entpacken** in einen Ordner.
- Das Spiel **bindet libSceAmpr ein**. Die App sucht den Namen in `eboot.bin` und in `sce_module` und zeigt **✓ lädt libSceAmpr
  selbst**. Steht dort **✗ lädt libSceAmpr nicht selbst**, ist der Knopf gesperrt (auch auf der Konsole: Fehler 409): Das Spiel
  würde seine Dateien in den Paketen nicht wiederfinden. Solche Spiele lassen sich nur zu einem `.ffpfsc`-Abbild komprimieren
  (Knopf **Konvertieren**). Lässt sich `eboot.bin` nicht lesen, entscheidet die App nicht.
- ShadowMountPlus in einer aktuellen Version, Abbilder schreibgeschützt eingehängt (der Standard).
- Im Spielordner liegen nur Dateien und Ordner. Eine Verknüpfung würde nicht mitgepackt; die App lehnt das Packen dann ab und nennt sie.
- **Dauer:** Das Packfenster nennt, wie lange Packen und Prüfen etwa dauern, bei Stufe 10 bis 12 auch die Zeit für Stufe 9. Anfangs sind es Schätzwerte; nach jedem Packen, das länger als 20 Sekunden lief, rechnet die App mit der Geschwindigkeit dieser Konsole.
- **Startprüfung:** Bei einem gepackten Spiel warnt die Zeile „LZ4-Asset-Packs“, wenn `eboot.bin` nicht ausgeführt werden darf oder ShadowMountPlus für das Spiel keine fakelib einbindet (fakelib-Modus „Deaktiviert“ oder global aus). Dann fehlt beim Start die APR-EMU, die die Pakete liest; in ShadowMountPlus den Modus auf „Emulatoren“ oder „Voll“ stellen.
- Platz: Beim Packen im Spielordner braucht die App zusätzlich etwa die Hälfte der Spieldaten, bis die Originale entfernt sind.

### Schritt für Schritt

1. **APR-EMU einbauen.** Im Fenster „LZ4-/zstd-Asset-Packs & APR-EMU“ bei **APR-EMU im Spiel** die Version `0.4.2.1 test-pack`
   wählen. Nur die Versionen mit „pack“ im Namen lesen Pakete.
2. **Ungepackt testen.** Das Spiel starten und ein Stück spielen: Menü, ein Level, einen Spielstand laden. Läuft es so nicht, läuft
   es gepackt nie. Dann hilft nur eine ältere APR-EMU (zum Beispiel 0.3.6.6; sie liegt der App seit November 2026 nicht mehr bei,
„Datei hinzufügen“ nimmt sie von drakmors Veröffentlichungsseite) oder keine.
3. **Packen.** **Mit LZ4 packen …**, zum Anfang Stufe 9 und **Automatisch**. Das Kästchen „ungepackt gestartet, und es lief“
   gehört dazu. **Im Spielordner packen** spart den meisten Platz; die Originale werden erst entfernt, wenn jede Datei Byte für
   Byte mit den Paketen verglichen ist, und bei jedem Fehler bleibt alles wie vorher. Die Wahl **Als neuen Ordner … daneben** lässt
   das Original unberührt, legt aber zwei Ordner mit derselben Titel-ID nebeneinander. ShadowMountPlus kann sich daran
   verschlucken und das Spiel im Sekundentakt umhängen; nimm das Original vorher aus dem Suchpfad. Gibt es für das Spiel ein
   **getestetes Profil** (siehe unten), ist es schon gewählt.
4. **Pakete prüfen.** Liest jeden Block, entpackt ihn und vergleicht mit der CRC-Datei. Danach richtet es, was gute Pakete
   außerdem brauchen: Fehlt die APR-EMU, die Pakete liest, oder hat etwas sie ersetzt, kommt `0.4.2.1 test-pack` wieder hinein.
   Dateien, die die PS5 auf der Platte sucht, aber nur in den Paketen liegen (von älteren Werkzeugen gepackt), holt es zurück.
   Und es setzt die Rechte im Spielordner auf 0777. Was es gerichtet hat, steht unter **Repariert**. Neu packen ist dafür nicht
   nötig. Das zeigt, dass die Pakete heil sind, nicht dass das Spiel sie richtig benutzt.
5. **Starten und spielen.** Erst jetzt zeigt sich, ob es läuft. Spiele länger als ein paar Sekunden: Menü, ein Level, Laden und
   Speichern.
6. **Läuft es nicht:** **LZ4 entpacken …** schreibt alle Dateien zurück, vergleicht sie und entfernt die Pakete.

### zstd statt LZ4 (seit 06.11.2026)

Neben den LZ4-Paketen macht die App **zstd-Pakete** (`ampr_zstd.index`, `ampr_zstd-NNN.pak`; **Mit zstd packen …**). Das Format
stammt von quer3q (ampr-emu-ng, `docs/zstd-format.md`); gelesen wird es nur von **AMPR EMU NG**. Die App legt NG beim Packen selbst
in `fakelib` (ist in ShadowMountPlus die automatische Fakelib-Aktualisierung noch an, steht eine Warnung im Ergebnis). Gepackt wird das
ganze Spiel außer den Systemdateien (Programme, `.ini`-Dateien wie `dlc_emu.ini`, `sce_sys`, `sce_module`, `fakelib*`, leere Dateien, die Pakete selbst und die Dateien
der APR-EMU, dazu die wenigen Dateien, die die PS5 auf der Platte sucht): kein Profil, kein „Automatisch“. Stufe 1 bis 22 (angeboten
1, 5, 9, 19; Vorgabe 5), jedes Stück von 256 KiB ein eigener zstd-Frame mit Prüfsumme, roh, wenn zstd es nicht verkleinert. Packen,
Prüfen (Byte für Byte gegen die Originale) und Entpacken wie bei LZ4. **Bei zstd gibt es keine Wahl mehr, ob die Originale bleiben** (seit
07.11.2026): Gepackt wird immer im Spielordner, und die Originale gehen erst, wenn jede Datei Byte für Byte mit ihrem Paket verglichen ist
(bei einem Fehler bleibt alles wie vorher; **zstd entpacken …** bringt alle Dateien zurück). Das Kästchen „ungepackt mit NG getestet“ bleibt
(NG baut die App beim Packen selbst ein). Das Packen in einen neuen Ordner `…-zstd` gibt es in den Oberflächen nicht mehr (die
Schnittstelle nimmt `kind: copy` weiter an). Ein Spiel liegt in LZ4- oder in zstd-Paketen, nie in beiden; in einem zstd-Spiel sind alle
APR-EMU-Versionen außer NG gesperrt. Gegen den unabhängigen Leser des PS5 Dump & Image Converter geprüft; an der Konsole mit Matchbox
(6 GB, Stufe 1, NG 0.3.3) gepackt, gestartet (lief mindestens zwei Minuten im Vordergrund) und wieder entpackt (08.11.2026).

### Getestete Profile

Die App bringt vier Profile mit, die danthespal geschrieben und an der Konsole getestet hat (veröffentlicht mit MkPFS.CSharp,
mitgeliefert auch von LZ4 Compression): **ASTRO BOT**, **EA Sports UFC 6**, die **Insomniac-Engine** (getestet mit Marvel's
Wolverine) und die **Unity-Engine** (getestet mit God of War Sons of Sparta). Passt eins zum Spiel, über die Titel-ID, den Namen
oder den Aufbau der Dateien, zeigt die Zeile **✓ getestetes Profil**, und beim Packen ist es schon gewählt. Ein eigenes Profil
für das Spiel (Dateiname beginnt mit der Titel-ID, etwa aus dem Mitschnitt) geht vor. Unter **Eigenes Profil** stehen die vier
auch für andere Spiele zur Wahl. Jedes lässt Dateien lose, die es nicht nennt: Eine andere Fassung des Spiels packt damit
weniger, bleibt aber sicher.

### Was immer lose bleibt

Gleich mit welchem Profil: Leere Dateien, die PlayGo-Marker (`cache_ps5/pgc_*_dummy_file`, `cache_ps5/playgo.pgm`) und die
Startdateien, die Ghost of Yōtei vor dem schwarzen Bildschirm braucht, bleiben lose, ebenso die Paketverzeichnisse von Remedys Spielen
(`*.rmdtoc`, Alan Wake 2 öffnet sie ohne APR und bricht sonst mit „No packfiles found!“ ab; ihre `.rmdblob`-Daten werden weiter gepackt).
Die PS5 sucht sie außerhalb des Spiels auf der Platte, wo die APR-EMU nicht hilft (gefunden für LZ4 Compression 1.4.3 und MkPFS.CSharp 2.5.0).
Wurden sie von einer älteren Fassung schon gepackt, holt „Pakete prüfen“ sie wie die anderen Marker-Dateien auf die Platte zurück (unter **Repariert**). Alles, was die App in einen Spielordner schreibt,
bekommt die Rechte 0777, auch beim Kopieren, beim Entpacken eines Abbilds und im Dateimanager.

### Was „Automatisch“ lose lässt

Beim Profil **Automatisch** bleiben lose: Programme und Bibliotheken (`eboot.bin`, `.prx`, `.sprx`), `sce_sys`, `sce_module`, `fakelib`, Spielstände, schon komprimierte Medien (mp4, webm, ogg, wem …), Konfigurations- und Datendateien (`.json`, `.xml`, `.ini`, `.dat`, `.bin`, `.utoc`), Archive (`.zip`, `.7z`, `.rar`) und die eigenen `.pak`-Dateien des Spiels, die Ordner `trophy2`, `uds`, `_DUPLEX_`, `decrypted` und Film-Ordner, Wwise-Soundbanks (`soundbank…`, `wem…`), Dateien direkt im Spielordner und sehr große, kaum packbare Dateien. Das ist die Liste, die sich bei Werkzeugen bewährt hat, die auf der Konsole laufen (exFAT2AMPR von V-10, Lazy_AMPR). Ein eigenes Profil (TOML) packt, was seine Regeln sagen.

### Besser als „Automatisch“: der Mitschnitt

„Automatisch“ kennt nur, was auf der Platte liegt. Zuverlässiger ist ein Profil aus einem echten Spieldurchgang. Die Zeile **Optimierung per Mitschnitt** macht das in vier Schritten:

1. **Mitschnitt starten.** Die App legt die Debug-APR-EMU (`0.4.2.1 test-debug-pack`) in einen Ordner `fakelib2` des Spiels und kopiert die anderen Bibliotheken aus `fakelib` dazu (ShadowMountPlus liest dann nur `fakelib2` und ersetzt dort nichts). Voraussetzung: Das Spiel lädt libSceAmpr, der AMPR-Index ist gebaut, es liegt nicht in Paketen.
2. **Spielen.** Starte das Spiel, spiele ein Stück (Menü, ein Level, einen Spielstand laden) und beende es normal. Das Spiel schreibt `ampr_commands.bin`. Die Debug-Version ist langsamer.
3. **Sitzung einsammeln.** Die App kopiert den Mitschnitt und den dazugehörigen Index nach `/data/PS5-Cooling-Center/traces/<Titel-ID>/<n>/`. Die Debug-APR-EMU leert den Mitschnitt bei jedem Start, also nach jeder Sitzung einsammeln. Mehrere Sitzungen (andere Level, andere Spielmodi) ergeben ein besseres Profil.
4. **Profil erzeugen.** Aus allen Sitzungen entsteht `<Titel-ID>-mitschnitt.toml` im Ordner `LZ4_Profile`. Für jede gelesene Datei legt es Blockgröße (16 KiB bis 1 MiB), Anordnung (zufällig, gemischt, streamend), „heiß“ und die Behandlung fest (Medien werden gespeichert statt gepackt) und stellt die Zwischenspeicher der APR-EMU ein. Dateien, die das Spiel nie gelesen hat, bleiben lose. Beim Packen wählst du es unter **Eigenes Profil**.

Danach **Mitschnitt beenden**: Das entfernt `fakelib2` wieder. Ein Mitschnitt zeigt nur, was in den gespielten Szenen gelesen wurde; für anderes ergänzt du eigene Regeln.

### Wenn das Spiel nicht startet oder abstürzt

- **Erst klären, wer schuld ist.** Entpacke das Spiel und starte es mit derselben APR-EMU-Version ungepackt. Beispiel Arkanoid
  (12.10.2026): Mit 0.4.2.1 stürzte es schon ungepackt ab, mit 0.3.6.6 lief es. Pakete waren dort also nicht möglich.
- **„mount flag / attribute error“** im Protokoll von ShadowMountPlus: Die Bibliothek lässt sich nicht laden, weil ihr das
  Ausführungsrecht fehlt (Modus 0644). Die App legt sie mit 0755 an. Hast du die Datei selbst kopiert, setze per FTP
  `SITE CHMOD 755`.
- **SIGBUS oder CE-108255-1 gleich beim Start:** `eboot.bin` oder Module haben kein Ausführungsrecht, typisch nach einer Kopie
  aus einem `.exfat`-Abbild (Dateien mit 0666, gesehen bei 007 First Light). **Pakete prüfen** setzt die Rechte im ganzen
  Spielordner auf 0777.
- **Es stürzt nach wenigen Sekunden ab, obwohl es ungepackt lief:** Das Spiel liest vermutlich Dateien, die die Pakete nicht
  bedienen, vor allem per **mmap** (in den Speicher eingeblendet) oder schreibend. Solche Dateien müssen lose bleiben. Benutze ein
  eigenes Profil (TOML, Ordner `LZ4_Profile`) und setze ans Ende eine `loose`-Regel für die Datei: Die letzte passende Regel
  gewinnt. Welche Dateien das Spiel nicht öffnen konnte, schreibt die Debug-Version (`0.4.2.1 test-debug-pack`) als
  `apr.pack.open.fail` in `ampr_emu.log`.
- **„Automatisch“ ist nur ein Anfang.** Es kennt nur, was auf der Platte liegt. Profile aus Mitschnitten eines echten Spiels sind
  zuverlässiger (`ampr_pack_profile.py` aus ampr_emu, MkPFS).

### Grenzen und Hinweise

- Die APR-EMU lehnt Sätze mit mehr als 2 000 000 Dateien, 16 000 000 Blöcken oder 1 024 Paketdateien ab. Bei großen Spielen nimmt
  die App größere Blöcke.
- Index, `.runtime` und Pakete eines Spiels gehören zusammen (gleiche Build-ID). Tausche nie einzelne Teile aus und ändere nichts,
  während das Spiel läuft.
- Die APR-EMU 0.4.2.1 hat laut ihrem Autor noch einen Absturz bei Spielständen. Behalte das Original, bis du lange genug gespielt
  hast.
- Solange ein Spiel gepackt ist, sind AMPR-Index und APR-EMU-Version gesperrt.
- Vorbilder und Regeln: [drakmor/ampr_emu](https://github.com/drakmor/ampr_emu) (`docs/ASSET_PACK_WORKFLOW_EN.md`, GPL-3.0),
  [theghostonline/PS5-LZ4-Compression](https://github.com/theghostonline/PS5-LZ4-Compression) und
  [danthespal/MkPFS.CSharp](https://github.com/danthespal/MkPFS.CSharp). Von LZ4 Compression 1.4.3 bis 1.5.0 (Husam Osman,
  GPL-3.0) stammen die Liste der Dateien, die lose bleiben müssen, die Rechte 0777, die Reparatur beim Prüfen und die Auswahl
  der getesteten Profile; nachgebaut, nicht kopiert.

### Passt der Index in AMPR EMU NG? (seit 14.11.2026, `src/indexlimit.c`)

AMPR EMU NG 0.4.0 baut aus dem Pack-Index Tabellen auf, die in seinen Speicher passen müssen: höchstens 48 MiB, und vom ganzen Indexspeicher (64 MiB) gehen noch die Größe der `ampr_emu.index` und 16 MiB Seitencache ab. Passt es nicht, liefert NG **keine** gepackte Datei aus, und das Spiel startet nicht (beim Start erscheint „index tables need 49 MiB, over the 48 MiB beside the page cache; packed files not served“; so bei Final Fantasy XVI und Demon's Souls, ampr-emu-ng #67, #61, #53). Der Dialog „LZ4-/zstd-Asset-Packs & APR-EMU“ zeigt bei einem Spiel mit Pack die Zeile **Passt der Index in AMPR EMU NG?** („passt“, „knapp“ = innerhalb von 2 MiB um die Grenze, „zu groß“) mit den geschätzten Tabellen in MiB; in der Vollbild-App steht es unter „INDEX-TABELLEN (AMPR EMU NG)“. Gerechnet wird aus den Köpfen von `ampr_zstd.index` (Dateien @40, Chunks @48, Volumes @20; NG nimmt sie vor dem LZ4-Paket) bzw. `ampr_assets.index` (Dateien @40, Chunks @48, Pakete @56) und der Größe der `ampr_emu.index`: zstd braucht `8·(2·Dateien + Chunks + Volumes)` (nur bei seitenweise gelesenem Index) plus `16·IDs + 4·min(Dateien, IDs)`, LZ4 `48·Dateien + 12·Chunks + 32·Pakete` plus `4·Dateien`; ein Index, der samt dieser Tabellen in den freien Indexspeicher passt, wird ganz geladen (dann zählt nur der abgeleitete Teil). Die Formeln sind mit den Betreuern von ampr-emu-ng abgestimmt; die App prüft sie am Fall Demon's Souls (LZ4, 223.180 Dateien, `ampr_emu.index` 34.373.632 Byte). Das beschreibt NG 0.4.0, das die App nicht mehr mitliefert; die Zeile in der App rechnet nach dem Modell von NG 0.4.1 und 0.5.0 (siehe nächster Absatz). Es ist eine **Schätzung für Version 0.4.0** (angenommen: die `ampr_emu.index` wird ganz geladen, vor dem Pack liegen keine weiteren Tabellen; bei einem Spiel ohne `ampr_emu.index` zählt die Runtime alle Dateien des Ordners): Eine spätere Fassung kann die Grenzen verschieben, die Meldung von NG beim Start ist verbindlich. Nach den Berichten startet ein solches Spiel mit NG 0.3.4; sonst hilft es, das Spiel mit weniger Dateien zu packen. Die Antwort von `GET /api/v1/library/ampr` trägt dazu `index_check`.

Ab **NG 0.4.1** (in der App beide Fassungen: 0.5.0 und 0.4.1) gilt ein anderes Modell als bei 0.4.0: Es gibt keine 48-MiB-Grenze und kein seitenweises Lesen mehr; die Ansicht aller Dateien der `ampr_emu.index` und die Paket-Tabellen müssen zusammen in den Indexspeicher von 80 MiB passen. Die Zeile **Passt der Index in AMPR EMU NG?** rechnet für diese Fassung; mit 0.4.1 passt in der Regel auch ein Spiel, das mit 0.4.0 „zu groß“ war (Final Fantasy XVI, Demon's Souls). NG 0.5.0 hat dieselbe Ansicht und dieselbe Größe des Indexspeichers; die App rechnet für beide mitgelieferten Fassungen gleich. Auch das ist eine Schätzung aus dem Quelltext von NG; die Meldung von NG beim Start bleibt verbindlich.

## Dateien: der Dateimanager (seit 06.10.2026)

Die Seite **Dateimanager** zeigt die Ordner der Konsole. Oben stehen die Orte (Interne SSD `/data`, jedes angeschlossene
Laufwerk, „Ganzes System“), darunter der Pfad zum Anklicken und die Liste: Ordner zuerst, mit Größe und
Datum. Ein Ordnername öffnet ihn, **↑ Hoch** geht eine Ebene zurück.

**Ansehen und Herunterladen gehen überall.** Jede Datei hat **Ansehen** (Bilder und Textdateien, siehe unten) und
**Herunterladen** (im Browser am PC; Gerätedateien wie unter `/dev` werden nicht ausgeliefert).

**Ändern geht überall (seit 1.75.3, vorher nur auf den Laufwerken und in `/data`):** **Neuer Ordner**, **Hochladen** (eine
oder mehrere Dateien, mit Fortschrittsbalken in Prozent; am PC auch per Ziehen auf die Liste), **Ordner hochladen** (ein ganzer Ordner samt Unterordnern, auch per Ziehen; Dateien, die es am Ziel schon gibt, werden übersprungen und gezählt, nie überschrieben), **Umbenennen**,
**Kopieren**, **Ausschneiden** (Verschieben) und **Löschen** – auch in Systemordnern wie `/user` (z. B. `/user/download`)
und im Ordner der App `/data/PS5-Cooling-Center`. Bereiche, die die Konsole schreibgeschützt eingehängt hat (`/system`
u. a.), lehnen eine Änderung selbst ab (Meldung „schreibgeschützt“). Das prüft die Konsole selbst, nicht nur die Seite.
Nur zum Schutz vor Unfällen gesperrt („Nur ansehen“, Knöpfe aus):

- `/` selbst, `/mnt`, `/dev` und `/proc`;
- Verknüpfungen: Sie werden nie verfolgt, wenn etwas geändert wird, und lassen sich weder umbenennen noch löschen; liegt
  eine Verknüpfung auf dem Weg, ist der ganze Pfad gesperrt;
- eingehängte Laufwerke selbst (`/mnt/usb0`, `/data`): nur ihr Inhalt. Kopieren und Löschen gehen nie auf ein anderes
  Laufwerk über. Seit 1.75.3 gilt das für jeden Ordner direkt unter `/` und direkt unter `/mnt`, auch wenn gerade nichts
  darin hängt (ein leeres `/mnt/usb2` oder `/mnt/disc` gelöscht, und der nächste Stick oder die nächste Disc findet keinen
  Platz), und für Ordner, unter denen etwas eingehängt ist (`/mnt/sandbox`);
- alles auf Gerätedateisystemen, auch die `/dev`-Ordner in den Sandboxen der Apps.

> **Vorsicht:** Unter `/system_data` und `/user` liegen Datenbanken und Einstellungen der Konsole. Wer dort das Falsche
> löscht oder verschiebt, riskiert eine Konsole, die nicht mehr startet oder neu eingerichtet werden muss. Überschrieben
> wird nie ungefragt: Gibt es einen Namen im Ziel schon, fragt ein Fenster (Ersetzen, Beide behalten, Überspringen).

USB-Laufwerke heißen seit 1.75.0 überall wie auf der Konsole: `usb0`, `usb1` … (der Ordner `/mnt/usb0` ist „usb0“; vorher
zählte die Oberfläche ab 1).

**USB-Laufwerk sicher auswerfen:** Neben jedem USB-Laufwerk (`/mnt/usb0` bis `usb7`, `/mnt/ext0`) steht in der Leiste der
Orte der Knopf **Auswerfen**. In der Vollbild-App ist es die Quadrat-Taste auf dem Laufwerk unter „Orte“; in einem Ordner
des Laufwerks gibt es △ → Mehr → **Sicher auswerfen**. Im Browser wechselt die Seite dabei auf System → Speicherplatz und wirft dort aus (wie der
Knopf dort, und damit nichts von der Dateimanager-Seite mehr das Laufwerk abfragt); in der Vollbild-App fragt die Seite das Laufwerk nicht mehr ab.
Es ist dasselbe Auswerfen wie unter System → Speicher (siehe dort): über ShadowMountPlus (ab 1.7beta10), nur wenn kein Auftrag der App läuft,
PS4Mount keine Spiele von dem Laufwerk spielt und die App selbst nichts mehr darauf offen hat (ob ein anderes Programm, etwa ein FTP-Server, Dateien darauf offen hat, fragt
sie nur, wo die Konsole es zulässt: auf der Pro mit Firmware 12.00 nicht); sonst sagt die App, was noch läuft oder welches Programm es ist. Das Laufwerk erst abziehen, wenn die Konsole „… ist ausgeworfen und kann abgezogen
werden“ meldet – solange „… wird ausgeworfen …“ dasteht (je nach Laufwerk zwei bis sieben Sekunden), noch nicht; manche Laufwerke meldet die Konsole nach einigen Sekunden von selbst wieder an, andere erst nach erneutem Einstecken. Ein Laufwerk im Betrieb einfach abzuziehen kann
das Dateisystem beschädigen. Ist die Konsole oder die App beim letzten Auswerfen abgestürzt, bleibt das Auswerfen gesperrt, bis du es auf der Seite
Dateien oder System wieder erlaubst.

**Ordnen, auswählen, ansehen (seit 07.10.2026):**

- **Sortieren:** Über der Liste stehen „Name“, „Größe“ und „Datum“; der Pfeil daneben kehrt die Richtung um. Ordner stehen
  immer oben. Der Browser merkt sich die Wahl.
- **Alle auswählen:** Das Kästchen über der Liste wählt alle Einträge des Ordners, ausgenommen Verknüpfungen und
  Sonderdateien. Halb gefüllt heißt: nur ein Teil ist gewählt.
- **Ordnergrößen:** „Größe berechnen“ im Menü „⋯“ eines Ordners zählt nach, wie viel darin liegt; „Ordnergrößen“ macht das nacheinander für alle
  Ordner der Ansicht. Die Konsole zählt höchstens etwa 8 Sekunden je Ordner; dauert es länger, steht die Größe als
  „mindestens“ da (`partial`).

**Vollbild-App (seit 13.11.2026):** Die Seite **Dateimanager** nutzt dieselben Routen und dieselben Regeln wie die Browser-Seite.
Dazu kommen: der **Auswahlmodus** (Kästchen vor jedem Eintrag, X setzt den Haken und geht eine Zeile weiter, O beendet ihn), 
**Alle Größen messen** (△ → Mehr) und das **Bearbeiten von Textdateien** zeilenweise mit der Bildschirmtastatur (Quadrat-Menü →
Bearbeiten; wie im Browser bis 64 KiB und nur, wo Ändern erlaubt ist; △ speichert über `POST /api/v1/files/write`). Hochladen und
Herunterladen bleiben im Browser.
- **Ansehen:** „Ansehen“ zeigt Bilder (PNG, JPG, GIF, WebP, BMP, ICO) und den Anfang von Textdateien (bis 256 KB) in einem
  Fenster. Die Datei kommt mit strenger Sicherheitskopfzeile (kein Skript, nur das Bild oder der Text); andere Dateien lassen
  sich nur herunterladen.
- **Bearbeiten:** Bei Textdateien bis 64 KB öffnet „Bearbeiten“ einen Editor; „Speichern“ schreibt die Datei zurück
  (`POST /api/v1/files/write`). Größere Dateien herunterladen, bearbeiten und wieder hochladen.

**Kopieren und Verschieben:** Einträge ankreuzen (oder am einzelnen Eintrag das Menü „⋯“ öffnen) und **Kopieren nach** oder
**Verschieben nach** wählen: Die Zielwahl zeigt oben die Orte der Konsole (Laufwerke, `/data` …) und lässt in Unterordner
wechseln; **Hierhin kopieren** bzw. **Hierhin verschieben** startet. Alternativ über die Ablage: **Kopieren** oder
**Ausschneiden**, in den Zielordner wechseln, **Hier einfügen** (so arbeitet die Vollbild-App). Gibt es einen Namen im Ziel
schon, fragt ein Fenster: **Ersetzen** (eine Datei wird erst neben die alte geschrieben und am Ende darübergelegt, ein
Ordner zusammengeführt), **Beide behalten** (`Name (2)`), **Überspringen** oder Abbrechen; `conflict` in der Anfrage
(`replace`, `keep`, `skip`). Die Anzeige nennt, wie viele Dateien/Ordner es sind und welcher dran ist („Kopiere 1 von 2
Dateien/Ordnern“), woher und wohin („von /mnt/usb0/neu nach /mnt/ext1“), Prozent, Bytes und die aktuelle Datei, dazu die
Übertragungsrate („82 MB/s“) und die geschätzte Restzeit („noch ca. 12 Min.“; die App misst beides einmal pro Sekunde und
glättet leicht, `rate_bps` und `eta_s` in `GET /api/v1/files/job`). **Abbrechen** hält an und entfernt im Ziel, was vom
angefangenen Eintrag schon geschrieben war. Verschieben innerhalb eines Laufwerks ist ein Umbenennen; zwischen zwei Laufwerken
wird kopiert, und das Original erst gelöscht, wenn die Kopie vollständig ist. Vorher prüft die Konsole den freien Platz.

**Löschen fragt zuerst:** Das Fenster nennt, was weggeht – die Namen mit Art und Größe, den Ordner und bei Ordnern, wie viele
Dateien mit wie viel Platz darin liegen (die Konsole zählt über den Plan nach, `POST /api/v1/files/delete/plan`; sein Kennwort
gilt einmal, zehn Minuten und genau für diese Liste). Erst **Löschen** im Fenster startet; **Abbrechen** hat den Fokus. Ein
Balken zeigt den Fortschritt. Dasselbe fragen Spiele und Sicherungen, Spielstände, Payload-Dateien und -Profile,
Disc-Abbilder, Dumps, Hintergrundbilder, Verlauf, Spielzeit-Verlauf und Kernel-Log-Dateien, im Browser wie in der
Vollbild-App (seit 06.11.2026; vorher löschte alles sofort).

Während Kopieren, Verschieben oder Löschen läuft, startet kein zweiter solcher Auftrag; der Dateimanager startet nicht,
solange ein anderer Vorgang der App läuft (Spiel kopieren, konvertieren, installieren …). Jede Änderung steht im Protokoll.

**NTFS-Laufwerke (seit 23.11.2026):** Laufwerke mit NTFS hängt die Konsole nicht ein; der Dateimanager liest und beschreibt sie
mit dem Treiber aus [PS5-NTFS-BROWSER](https://github.com/abcde6543/PS5-NTFS-BROWSER) (abcde6543, v1.4.0, GPL-3.0-or-later,
unverändert in `src/third_party/ntfsb/`, Herkunft in `ORIGIN.md`), im Browser wie in der Vollbild-App. Solange der Dateimanager
offen ist (er fragt die Orte alle 3–4 s), sucht die App höchstens alle 3 s nach ganzen Platten `/dev/daN`, von denen die Konsole
nichts eingehängt hat – nie, während ein Laufwerk ausgeworfen wird oder sich danach setzt. Jede NTFS-Partition (ohne Tabelle,
MBR samt logischen Partitionen, GPT) wird ein Ort `ntfs0` … `ntfs7` (die kleinste freie Nummer) mit Pfaden `/ntfsN/...`; die
Ortsleiste zeigt „ntfs0 (NTFS · Name)“. Ein Laufwerk ohne NTFS wird einmal angesehen und bis zum Abziehen gemerkt.

- **Geht:** Liste, Ansehen, Herunterladen, Größe, Hochladen (auch Ordner), Neuer Ordner, Kopieren und Verschieben in beide
  Richtungen (auch von NTFS zu NTFS; Verschieben ist dort Kopieren und danach Löschen dessen, was die Kopie wirklich hat) und
  Löschen (mit Plan und Nachfrage wie überall). Einträge mit dem Windows-Merkmal „System“ (etwa „System Volume Information“,
  „$RECYCLE.BIN“) sind `writable: false`.
- **Geht nicht** (der Treiber ändert oder benennt eine vorhandene Datei nie um): Umbenennen (403 `ntfs_no_rename`), Bearbeiten
  (403 `ntfs_no_change`) und Ersetzen (409 `no_replace`; das Konfliktfenster bietet auf NTFS nur „Beide behalten“, „Überspringen“
  und Abbrechen). Namen, die Windows nicht erlaubt (`\ : * ? " < > |`, Steuerzeichen, Punkt oder Leerzeichen am Ende, `CON`,
  `NUL`, `COM1` …), schreibt die App mit `_` an dieser Stelle. Spiele lassen sich von NTFS weder starten noch einhängen, weil
  die Konsole das Laufwerk nicht sieht.
- **Nur lesbar:** Schreiben wird beim Einlesen nur für saubere Laufwerke erlaubt; zum Schreiben geöffnet wird das Gerät erst beim
  ersten Schreiben. Sonst tragen Orte und Liste `read_only_why` (Browser: „Nur lesbar · Grund“ über der Liste, Vollbild:
  Hinweiskarte rechts). Der Grund ist aus Sicht der PS5 gesagt: was mit dem Laufwerk ist (nicht sauber getrennt: am PC ohne „Sicher
  entfernen“ oder an der PS5 mitten im Schreiben abgezogen – die App setzt dafür beim Schreiben das Dirty-Flag und löscht es nach dem
  Ausschreiben wieder,
  Journal von Windows offen, chkdsk nicht fertig, Systemplatte eines Windows-PCs im Ruhezustand, NTFS älter als 3.x, `$UpCase`
  fehlt, Gerät nicht beschreibbar, Schreibfehler) und dann „Damit die PS5 darauf schreiben kann: …“ mit den Schritten am
  Windows-PC und „wieder an die PS5 stecken“ (`src/ntfsvol.c`, `why_words`).
- **Auswerfen:** Knopf „Auswerfen“ am Ort (Vollbild: Quadrat-Taste auf dem Laufwerk oder △ → Mehr → Sicher auswerfen) →
  `POST /api/v1/files/eject` – ohne ShadowMountPlus: schreibt Offenes aus, schließt das Gerät und meldet „ntfs0 kann jetzt
  abgezogen werden.“. 409, solange die App das Laufwerk benutzt (Kopieren, Herunterladen, eine Liste); 500 mit chkdsk-Hinweis,
  wenn das letzte Schreiben scheiterte (das Laufwerk ist trotzdem frei). Ein ausgeworfenes Laufwerk bleibt bis zum Abziehen weg.
- Der Treiber hält beim Einlesen eines Ordners seine Sperre; die App liest deshalb jeden Ordner erst ganz in eine Liste und
  arbeitet dann. Host-Test `test/ntfsfm` (48 Prüfungen auf mkntfs-Abbildern, auch mit MBR-Partition und nur lesbar; Gegenprüfung
  mit ntfs-3g: `ntfscat`, `ntfsls`, `ntfsresize --check`).

## Sprache der Oberfläche (seit 07.10.2026)

Oben rechts in der Kopfzeile steht die Sprachwahl: **Auto (System), Deutsch, English, Italiano, Español, Français, Русский, Svenska**. Ab Werk steht
sie auf **Auto**: Die App übernimmt die Systemsprache, im Browser der PS5 die der Konsole, am PC oder Handy die des Browsers (in der
Reihenfolge seiner Sprachen); hat die App die Sprache nicht, gilt Englisch (seit 12.11.2026). Die Vollbild-App folgt ebenso der Sprache der
Konsole (Einstellung „Sprache“: Systemsprache), Englisch, wenn sie diese nicht hat. Eine eigene Wahl merkt sich der Browser
(`localStorage`, Schlüssel `lang`), die Seite lädt dann neu und bleibt auf der Seite, auf der man war. Zahlen, Datum und
Uhrzeit folgen der Sprache (`de-DE`, `en-GB`, `it-IT`, `es-ES`, `fr-FR`, `ru-RU`, `sv-SE`).

- Die Quelltexte der App sind deutsch. Für jede andere Sprache lädt die Seite ein Wörterbuch (`/lang/<xx>.json`), das jeden
  deutschen Text auf den übersetzten abbildet. Deutsch braucht keins.
- Texte, die die Konsole selbst auf dem Fernseher einblendet (beim Start, bei einer Warnung, bei der Mikrofon-Taste), sind
  deutsch. Auch Meldungen aus der Konsole in der Oberfläche (z. B. Fehlertexte eines Auftrags) werden übersetzt, soweit das
  Wörterbuch sie kennt; seltene Texte können deutsch bleiben. Wer eine Lücke findet, kann sie als Issue melden
  („Translation“) – Näheres zum Ergänzen steht in [ENTWICKLUNG.md](ENTWICKLUNG.md#übersetzungen).
- **Handbuch und FAQ** gibt es in allen sieben Sprachen: in der App (Seitenleiste, unter „Credits“; ohne Bildschirmfotos).

## Credits (seit 06.10.2026)

Die Seite **Credits** in der Seitenleiste dankt den Entwicklern, deren Arbeit in der App steckt, und weiteren
bekannten Entwicklern der Community: je eine Karte mit dem Profilbild ihres GitHub-Kontos, dem Namen, einem Dank und
den Links zu ihren Arbeiten. Die Bilder sind in der App eingebaut (kein Laden aus dem Internet). Die Links zu GitHub
sind nur anklickbar, wenn der Browser Internet hat: Beim Öffnen der Seite lädt er dafür einmal das kleine Symbolbild
von github.com. Ohne Internet (auch wenn die PS5 im Heimnetz ist) stehen die Links als grauer Text da, und ein Satz
unter den Karten sagt, warum.

## Spiele und Sicherungen löschen (seit 06.10.2026)

Jede Spielkarte hat den Knopf **Löschen**, und der Reiter **Sicherungen** auf der Seite „Spiele“ listet die Kopien
und Konvertierungen, die die App angelegt hat (Ordner `PS5-Sicherung/Spiele` auf der internen SSD und jedem Laufwerk,
mit Laufwerk, Größe, Datum und den Hinweisen „unfertig“ und „mit Prüfsumme“), jede ebenfalls mit **Löschen**.

**Was gelöscht wird:**

- **Installierte Spiele** (auch Fake-PKGs): über die Deinstallation der Konsole selbst, so wie im Menü „Löschen“ der
  PS5. Danach bittet die App die Konsole, auch die Updates und Zusatzinhalte des Spiels zu entfernen, und wartet, bis das
  Spiel aus der Bibliothek und von der SSD verschwunden ist. Den Aufruf macht das Installations-Hilfsprogramm, wie beim
  Installieren.
- **Spiele von ShadowMountPlus:** Zuerst meldet ShadowMountPlus das Spiel ab (es hängt es dabei aus); erst dann löscht die
  App das Abbild bzw. den Ordner und eine Prüfsummen-Datei daneben. Ohne das Abmelden würde ShadowMountPlus die Datei
  beim nächsten Durchsuchen wieder einbinden. Dazu muss die laufende ShadowMountPlus-Fassung das Abmelden anbieten.
- **PS4-Spiele von PS4Mount (seit 22.11.2026):** PS4Mount nimmt das Spiel aus der Bibliothek, wie mit „Entfernen“ auf der
  Seite PS4Mount: Es hängt das Spiel aus, dann deinstalliert es die Konsole, und PS4Mount nimmt es nicht von selbst wieder
  auf. Die Deinstallation allein lehnte die Konsole ab, solange das Spiel eingehängt war (Fehler 0x80AF5002). Die
  PKG-Dateien auf dem USB-Laufwerk bleiben liegen; löschen lassen sie sich im Dateimanager.
- **Sicherungen der App:** die Datei bzw. der Ordner und die Prüfsummen-Datei daneben.

**Die Spielstände bleiben immer erhalten.** Keiner dieser Wege fasst sie an.

**Mit Rückfrage (seit 06.11.2026):** Ein Klick auf „Löschen“ löscht noch nichts. Die
Seite holt den Plan still und zeigt im Fenster, was gelöscht wird (Name, Art, Ort, Größe); erst „Löschen“ im Fenster löscht, „Abbrechen“ (dort liegt der Fokus) lässt alles, wie es war. Lehnt die
Konsole ab (etwa weil das Spiel läuft), bleibt der Dialog mit dem Grund stehen. Die Konsole löscht weiter nur mit
einem Kennwort, das sie zum Plan ausgegeben hat (einmal gültig, zehn Minuten lang, nur für genau dieses Spiel oder diese Sicherung).

**Fortschritt:** Während des Löschens zeigt der Dialog den Schritt, einen Fortschrittsbalken mit Prozent und „x von y
gelöscht“. Bei Abbildern und Sicherungen zählen die gelöschten Bytes; eine große Datei wird dafür in Schritten von
512 MB von hinten gekürzt und dann entfernt, damit der Balken auch bei einem einzelnen Abbild vorankommt. Bei einem
installierten Spiel misst die App, wie weit die Konsole seine Ordner schon geleert hat; lässt sich das nicht messen,
steht der Balken bei 5 % und nach der Zusage der Konsole bei 60 %. 100 % zeigt er erst, wenn alles fertig ist.

**Nicht gelöscht wird:**

- ein Spiel, das gerade läuft (auch pausiert): erst beenden;
- irgendetwas, solange ein anderer Vorgang der App läuft (Kopieren, Konvertieren, Verschieben, Spielstände, Paket teilen
  oder installieren);
- ein Spiel, das über eine Verknüpfung eingebunden ist, ohne dass ShadowMountPlus es führt: welche Dateien dazugehören,
  weiß die App nicht sicher;
- alles außerhalb der Laufwerke (`/mnt/usb…`, `/mnt/ext0`, `/mnt/ext1`) und von `/data`, ein Laufwerk selbst, und der
  Ordner der App (`/data/PS5-Cooling-Center`).

Beim Löschen folgt die App keiner Verknüpfung (eine Verknüpfung in einem Spielordner wird als Verknüpfung entfernt, ihr
Ziel bleibt) und betritt kein anderes Laufwerk. Das Protokoll nennt jeden Löschvorgang (`game_delete_start`,
`game_delete_done`, `game_delete_failed`; bei installierten Spielen `pkg_uninstall_called` mit den Antworten der
Konsole).

## Spielzeit

Auf der Seite **Spiele** zeigt der zweite Reiter, **Spielzeit**, wann gespielt wurde,
wie lange und wie warm die Konsole dabei wurde. Der erste Reiter, **Bibliothek**, bleibt
die Liste der Spiele.

- **Woher die Zahlen kommen.** Die App schreibt selbst mit: Einmal pro Sekunde sieht sie
  nach, welches Spiel läuft und ob es vorn ist (dieselbe Spielerkennung wie auf der
  Kühlungsseite; sie muss unter *System → Zusatzabfragen* eingeschaltet sein, sonst
  zeigt die Seite einen Hinweis). Gezählt wird, solange das Spiel vorn ist. Geht man zum
  Startbildschirm, steht die Uhr; kehrt man zurück, läuft sie weiter. Das ist nicht die
  Spielzeit auf den Karten der Bibliothek: Die zählt die Konsole selbst, von Anfang an.
  Die der App beginnt mit dem ersten Start einer Fassung, die das kann.
- **Sitzung.** Ein Spiel vom Start bis zum Beenden ist eine Sitzung, auch wenn es
  zwischendurch pausiert war. Sie endet, wenn das Spiel 45 Sekunden nicht mehr da war
  (manche Spiele starten sich dazwischen selbst neu), wenn ein anderes Spiel startet oder
  wenn die Konsole zwischendurch schlief; dann gilt der letzte Moment, an dem die App das
  Spiel noch sah, nicht der, an dem sie es vermisste. Als Ende zeigt die Liste den
  letzten Augenblick, in dem das Spiel vorn war: Ein Spiel, das danach noch lange
  pausiert auf dem Startbildschirm lag, verlängert die Sitzung nicht. Sitzungen mit
  weniger als 30 Sekunden vorn werden nicht gespeichert: Ein Spiel, das beim Start
  abstürzt, oder ein kurzer Blick hinein ist kein Spielen.
- **Was die Seite zeigt.** Oben das Spiel, das gerade läuft, und die Summen für *Heute*,
  die *letzten 7* und *30 Tage* und *Insgesamt*. Darunter die letzten 14 Tage als
  Balken, die Rangfolge der Spiele (*Meistgespielt*, mit dem Anteil an der Gesamtzeit
  und der höchsten Temperatur, die je dabei erreicht wurde) und die letzten Sitzungen:
  wann, wie lange, CPU- und SoC-Höchstwert und die mittlere Lüfterdrehzahl. Die Marke
  **Notfallmodus** oder **Warnung** steht an einer Sitzung, in der die Regelung in diesen
  Zustand kam. **Ende geschätzt** heißt: Die App wurde beendet oder die Konsole
  ausgeschaltet, während das Spiel lief. Als Ende gilt der letzte Zwischenstand der App
  (alle 30 Sekunden aufgefrischt); wie lange das Spiel danach noch lief, weiß sie nicht.
  Die Temperaturen und die Lüfterdrehzahl zählen nur für die Zeit, in der das Spiel vorn
  war.
- **Tage und Uhr.** Welche Sitzung auf welchen Tag fällt, richtet sich nach der Ortszeit
  des Browsers; eine Sitzung über Mitternacht verteilt sich nach Zeit auf beide Tage (eine,
  die dazwischen lange pausiert war, gleichmäßig statt genau) und zählt in beiden. Als
  „jetzt“ gilt die Uhr der Konsole: Mit ihr stimmen Sitzungen und Tage in sich, auch wenn
  sie falsch geht. Weicht sie um mehr als zehn Minuten von der Uhr des Geräts ab, an dem
  die Seite offen ist, steht ein Hinweis oben.
- **Speichern.** Die App legt beendete Sitzungen in `/data/PS5-Cooling-Center/sessions.csv`
  ab, eine Zeile je Sitzung (Beginn und Ende in Sekunden seit 1970, Spielzeit in Sekunden,
  Temperaturen, Lüfter, Marken, Titel-ID, Name); es bleiben die letzten 3000. Die
  laufende Sitzung steht in `session-open.csv` und wird alle 30 Sekunden aufgefrischt.
  Ersetzt man die App, während ein Spiel läuft, führt die neue die Sitzung fort, wenn das
  keine drei Minuten dauert. Wird die Konsole mitten im Spiel ausgeschaltet (oder dauert
  der Austausch länger), schließt die App die Sitzung beim nächsten Start am letzten
  bekannten Zeitpunkt ab (*Ende geschätzt*). Nach einem Neustart der App kann die
  Spielerkennung oft nicht sagen, ob das Spiel vorn ist (die Kernel-Meldung dazu ist aus
  dem Puffer gerollt); die App nimmt dann an, dass es so ist wie beim letzten Zwischenstand,
  bis die Erkennung wieder etwas Eindeutiges meldet.
- **Als Tabelle speichern und Löschen.** „Als Tabelle speichern“ legt alle Sitzungen als
  `ps5-spielzeit.csv` ab (Semikolon als Trenner, mit der Ortszeit des Browsers, für Excel
  und Calc). „Verlauf löschen“ fragt zuerst und löscht erst nach „Löschen“ im Fenster die gespeicherten
  Sitzungen; ein Spiel, das gerade läuft, zählt weiter und wird wie jedes andere
  gespeichert, wenn es endet.

## Spielstände sichern und zurückspielen

Auf der Seite **Spiele** zeigt der dritte Reiter, **Spielstände**, was die Konsole an
Spielständen hat, kopiert sie auf einen USB-Stick, eine Platte oder in den Speicher der
Konsole und spielt auf Wunsch einen einzelnen Titel wieder zurück. Die Spiele selbst und
ihre Trophäen gehören nicht dazu.

- **Was kopiert wird.** Die Dateien, in denen die Konsole die Spielstände eines Benutzers
  ablegt, so wie sie sind: verschlüsselt, die App öffnet und ändert nichts daran. Das sind
  die PS4-Spielstände (`savedata`), die PS5-Spielstände (`savedata_prospero`, mit der
  Sicherungsdatei, die die Konsole selbst dazu anlegt) und die kleinen Begleitdateien
  (Bild, Beschreibung). Bei einer Sicherung kommt noch die Spielstand-Datenbank der
  gewählten Benutzer dazu (`savedata.db`, `game_setting.dat`). Sie wird nur aufbewahrt und
  nie zurückgespielt.
- **Ordnung der Liste (ab 1.51.0).** Je Benutzer getrennt nach **PS5-Spielen und PS4-Spielen**. **Apps** (YouTube und
  andere) stehen seit 28.10.2026 nicht mehr in der Liste und werden weder gesichert noch zurückgespielt noch gelöscht; auch
  in älteren Sicherungen werden sie nicht mehr angeboten, und die Schnittstelle lehnt sie mit 403 ab. Als **App** gilt, was die Konsole selbst so beschreibt: bei PS5-Titeln `applicationCategoryType` in `param.json`
  ungleich 0 (YouTube: 65536, Spiele: 0), bei PS4-Titeln eine `CATEGORY` in `param.sfo`, die nicht mit „g“ beginnt, und
  die System-Anwendungen `NPXS…`. Fehlt jede Beschreibung, gilt der Titel als Spiel. Neben der Titel-ID steht der Name,
  auch bei nicht mehr installierten Titeln, soweit ihn die Konsole noch kennt.
- **Löschen (ab 1.51.0).** Jeder Titel hat einen Knopf „Löschen“; ein Fenster nennt zuerst den Titel (und das Laufwerk, auf das vorher gesichert wird), erst „Löschen“ im Fenster startet. Die App sichert den
  jetzigen Stand zuerst auf dem gewählten Ziel (vorgewählt ist das erste passende Laufwerk; gibt es keines, wird ohne
  Sicherung gelöscht, und ein Hinweis sagt das) (als „Stand vor dem Löschen“, derselbe Weg wie beim Zurückspielen; er lässt sich
  zurückspielen) und entfernt dann die Spielstand-Ordner dieses Titels für diesen Benutzer. Das geht nur, wenn
  kein Spiel läuft, auch kein pausiertes. Die Spielstand-Datenbank der Konsole (`savedata.db`) fasst die App nicht an.
- **Sichern.** Die Seite listet je Benutzer (bis zu 16, so viele Konten hat eine PS5) die Titel
  mit Größe und Datum der letzten Änderung; alle sind vorgewählt, ein Häkchen am Benutzer wählt
  alle oder keinen. Dann das
  Ziel wählen (vorgewählt ist ein Stick oder eine Platte, sonst der Speicher der Konsole;
  außerhalb der Konsole ist die Sicherung besser aufgehoben, falls diese einmal ausfällt)
  und „Spielstände sichern“. Die Sicherung liegt im Ordner
  `PS5-Sicherung/Spielstaende/<Datum_Uhrzeit>` des Ziels, beim Konsolenspeicher unter
  `/data/`; Datum und Uhrzeit im Ordnernamen sind Weltzeit (UTC), die Seite zeigt sie in deiner
  Ortszeit. Ordner und Dateien bekommen dieselben strengen Rechte wie die Spielstände auf
  der Konsole (nur der Besitzer; ein exFAT-Stick kennt keine Rechte). Auf dem Ziel müssen
  mindestens 64 MB mehr frei bleiben, als die Sicherung braucht.
- **Geprüft wird jede Datei.** Nach dem Schreiben liest die App sie vom Ziel noch einmal und
  vergleicht sie mit dem, was sie gelesen hat; die SHA-256 kommt in die Liste
  `manifest.json` und in `pruefsummen.sha256`. Die Datei lässt sich am PC mit
  `sha256sum -c pruefsummen.sha256` im Ordner der Sicherung prüfen. Ein Fehler (Stecker
  gezogen, Stick voll, Datei schlecht geschrieben) bricht ab und räumt auf: Es bleibt nie
  eine halbe Sicherung liegen. Solange eine geschrieben wird, trägt der Ordner die Marke
  `.ps5cc-unfertig`; Sicherungen mit der Marke zeigt die Seite nicht.
- **Läuft ein Spiel**, auch pausiert im Hintergrund, wird weder gesichert noch
  zurückgespielt: Das Spiel kann seine Spielstände offen halten, und eine Kopie wäre dann
  vielleicht halb geschrieben. Die Seite sagt es oben; zuerst das Spiel beenden („Spiel
  beenden“ auf der Karte in der Bibliothek). Eine Sicherung prüfen geht trotzdem. Es läuft
  immer nur ein Vorgang, auch nicht gleichzeitig mit Kopieren, Konvertieren oder Verschieben
  von Spielen.
- **Prüfen.** „Prüfen“ an einer Sicherung liest jede ihrer Dateien neu und vergleicht sie
  mit der SHA-256 aus der Liste: nach dem Umstecken eines Sticks, oder wenn man ihr länger
  nicht getraut hat.
- **Zurückspielen.** Unter „Titel anzeigen“ hat jeder Titel einer Sicherung den Knopf
  „Zurückspielen“; er wirkt beim ersten Klick. Die App geht dann so vor:
  1. Die Sicherung wird ganz geprüft. Ist eine Datei des Titels beschädigt, wird nichts
     geändert.
  2. Der jetzige Stand des Titels wird als eigene Sicherung abgelegt („Stand vor dem
     Zurückspielen“, der Ordnername endet auf `_vor-Zurueckspielen_<Titel-ID>`). Sie lässt
     sich genauso zurückspielen. Hat die Konsole für den Titel gerade nichts, steht ein
     Hinweis da.
  3. Ein Spiel darf in der Zwischenzeit nicht gestartet worden sein; sonst bricht der Vorgang
     ab, ohne etwas zu ersetzen. Auf der Konsole muss Platz sein, um die neuen Dateien neben
     die jetzigen zu schreiben (ihre Größe plus 64 MB), sonst bricht er ebenfalls ab. Reste
     eines früheren, abgebrochenen Zurückspielens (Dateien mit der Endung `.ps5cc-neu`) werden
     dabei weggeräumt; eine Sicherung nimmt sie nie mit.
  4. **Alle** neuen Dateien werden zuerst neben die alten geschrieben (mit der Endung
     `.ps5cc-neu`) und geprüft. Bis hierher ist auf der Konsole nichts ersetzt: Ein Fehler,
     ein gezogener Stecker oder „Abbrechen“ lassen alles beim Alten und räumen auf.
  5. Das Schreiben kann von einem Stick Minuten dauern. Deshalb schaut die App unmittelbar
     davor noch einmal nach, ob inzwischen ein Spiel gestartet wurde; dann ersetzt sie nichts
     und räumt auf.
  6. Dann werden die neuen Dateien umbenannt, mit Modus und Besitzer der alten Datei, in einem
     Zug. Das dauert Millisekunden und ist der Punkt ohne Rückkehr: Der Knopf „Abbrechen“
     verschwindet, bis der Vorgang zu Ende ist.
  7. Zum Schluss wird alles, was auf der Konsole liegt, gelesen und mit der Sicherung
     verglichen.
  Geht dabei etwas schief, steht der Stand davor in der Sicherung aus Schritt 2. Dateien, die
  nur auf der Konsole liegen (ein neuerer Spielstand-Platz, den die Sicherung nicht hat),
  bleiben unberührt; die Datenbank wird nie zurückgespielt. Ein Ordner, der leer ist, gilt
  als „kein Stand“ (es gibt dann keine Sicherung davor, und der Hinweis sagt es); ein
  Zurückspielen, das an der ersten Datei scheitert, hinterlässt keine leeren Ordner.
- **Nur zur selben Konsole.** Spielstände sind an ihre Konsole gebunden: Auf einer anderen
  ließen sie sich nicht öffnen. Eine Sicherung von einer anderen Konsole trägt die Marke
  *andere Konsole* und lässt sich prüfen, aber nicht zurückspielen. Zurückgespielt wird auch nur
  zu Benutzern, die es auf der Konsole gibt.
- **Auf eine andere Konsole übertragen (ab 1.87.0).** Eine Sicherung passt nur zu ihrer Konsole. Bei jedem PS5-Titel gibt
  es deshalb den Knopf **„Als ZIP“** (er liest alle Spielstände des Titels aus, eine ZIP-Datei je Spielstand, alle mit derselben Zeit im Namen; welche ein Spiel anlegt, ob `SaveData`, `sce_sdmemory` oder beides, muss man nicht wissen): Die Konsole liest jeden Spielstand mit
  ihren eigenen Mitteln im Klartext aus – sie hängt dafür eine **Kopie** des Abbilds ein, die App entschlüsselt nichts selbst
  und das Original wird nie eingehängt oder verändert –, und die App packt die Dateien in eine ZIP-Datei (gespeichert, nicht
  komprimiert, mit dem echten `sce_sys/param.sfo`, das die PS5 im Kopf des Abbilds versteckt). Dazu kommt `chives.json`:
  Größe und Aufbau des Abbilds und Besitzer und Rechte jeder Datei, damit sich daraus später wieder ein Abbild bauen lässt, das
  die andere Konsole annimmt (Dateiname `<Titel-ID>_<Spielstand>_<Zeit>.zip`, Ordner `/data/PS5-Cooling-Center/savexport`).
  Die Karte **Auf eine andere Konsole übertragen** listet die fertigen Dateien zum Herunterladen und zum Löschen (mit Rückfrage).
  Die ZIP-Datei ist **nicht verschlüsselt**: Sie enthält den Spielstand im Klartext; gib sie nur weiter, wenn du es willst. Ausgelesen
  wird nur, wenn **kein Spiel läuft**; es läuft immer nur ein Vorgang, und „Abbrechen“ räumt alles auf (auch die Kopie des Abbilds).
  Auf der internen Platte müssen etwa zweimal die Größe des Spielstands plus 64 MB frei sein. PS5- und PS4-Spielstände, höchstens 5000
  Dateien und Ordner, keine Datei ab 4 GiB.
- **Einspielen auf der anderen Konsole (ab 1.87.0).** Unter „Spielstand einspielen“ wählst du die ZIP-Datei am Rechner (von dieser App oder von
  Garlic SaveMgr, mit `chives.json`). Sie geht über den Upload des Dateimanagers nach `/data/PS5-Cooling-Center/savexport/import`; die App liest sie
  durch und zeigt Titel, Dateien und Größe, **ohne etwas zu ändern**. Dann wählst du den Benutzer dieser Konsole. Eingespielt wird so:
  1. Hat der Benutzer schon Spielstände dieses Titels, werden sie zuerst als „Stand vor dem Einspielen“ auf das gewählte Laufwerk gesichert (samt
     Spielstand-Datenbank; derselbe Weg wie beim Zurückspielen). Ohne Laufwerk wird nicht eingespielt.
  2. Die Konsole legt mit ihrem eigenen Formatierer ein **neues Abbild** an (Größe und Aufbau aus `chives.json`; das Ergebnis muss genau dem
     entsprechen). Die App hängt es ein und schreibt die Dateien hinein, mit den Zugriffsrechten der Quelle. Im `param.sfo` werden das Konto
     (`ACCOUNT_ID`, aus dem Register der Konsole) und die Benutzer-Kennung dieser Konsole eingetragen: nur dort steht, wem der Spielstand gehört.
  3. Das Abbild wird noch einmal eingehängt, **jede Datei zurückgelesen** und mit der ZIP-Datei verglichen (Größe, Prüfsumme).
  4. Erst dann ändert sich etwas an der Konsole (bis hierher räumt „Abbrechen“ alles auf, auch bei einem Fehler): Das Abbild kommt nach
     `/user/home/<Benutzer>/savedata_prospero/<Titel>/sdimg_<Spielstand>` (ein vorhandenes liegt bis zum Ende als `.ps5cc-alt` daneben), das Bild
     des Spielstands nach `savedata_prospero_meta/user/<Titel>/<Spielstand>_icon0.png`, und der Eintrag in
     `/system_data/savedata_prospero/<Benutzer>/db/user/savedata.db` wird angelegt oder aktualisiert, nachdem die Datenbank nach
     `/data/PS5-Cooling-Center/savexport/db-backup` kopiert wurde. Scheitert dabei etwas, geht alles auf den Stand davor zurück.
  Die ZIP-Datei wird danach gelöscht. Dann das Spiel starten und den Spielstand prüfen. **Neueres Abbildformat:** Ein PS5-Spielstand mit `fs_ext` ungleich 0 (Systemblöcke hinter den Datenblöcken; `SAVEDATA_BLOCKS` = Blöcke − `fs_ext`, `SYSTEM_BLOCKS` = `fs_ext`) wird als
  klassisches Abbild aus seinen Datenblöcken angelegt (`SYSTEM_BLOCKS` 0; bei Bedarf wachsen die Blöcke etwas, damit die Dateien hineinpassen). Die Karte sagt das vor dem Einspielen,
  und ob das Spiel ihn annimmt, zeigt erst ein Start des Spiels. **Nicht unterstützt:** andere Abbildformate neuerer Firmware (zum Beispiel `param.sfo` nur im Abbildkopf),
  ZIP-Dateien ohne `chives.json` und solche mit mehreren Spielständen: Die App nennt den Grund,
  ohne etwas zu ändern. Es läuft immer nur ein Vorgang; eingespielt wird nur, wenn kein Spiel läuft.
- **Sicherheitsprüfungen beim Übertragen (ab 1.88.0).** Kurz vor dem Punkt ohne Rückkehr (und vor jeder weiteren ZIP-Datei) schaut die App noch einmal, ob
  inzwischen ein Spiel gestartet wurde, und ändert dann nichts. **Apps** (zum Beispiel YouTube) werden nie eingespielt, und ihre Spielstände dienen auch nicht als Vorlage für die
  Besitzerangaben. Sind mehrere ZIP-Dateien gewählt, müssen sie zum selben Titel gehören und verschiedene Spielstände enthalten; die Datei wird vor jedem Spielstand neu
  geprüft. Ein Abbild kommt, wo es geht, per Umbenennen an seinen Platz, sonst als Kopie, die mit dem geprüften Abbild Byte für Byte verglichen wird. Die Datenbank wird nur
  aus ihrer Kopie zurückgesetzt, wenn sie geschrieben wurde und dann nicht mehr in Ordnung war; scheitert schon das Schreiben (zum Beispiel weil die Konsole sie gerade sperrt),
  bleibt die Datei, wie sie ist. Die Kopie trägt Datum, Uhrzeit und Spielstand im Namen (`savedata-<Benutzer>-<Zeit>-<Spielstand>.db`). Reste eines unterbrochenen Einspielens
  (`sdimg_<Spielstand>.ps5cc-alt` und `.ps5cc-neu`) erscheinen nicht als Spielstand; fehlt der Spielstand selbst und nur die alte Fassung ist da, kommt sie beim nächsten Einspielen
  zurück. ZIP-Dateien mit Ordner-Einträgen (zum Beispiel aus 7-Zip) werden angenommen; gepackte Dateien über 64 MB und Einträge, deren gepackte Größe nicht zur Größe passt,
  lehnt die App vor dem Start ab. Hat ein Titel mehr als 16 Spielstände, liest „Als ZIP“ nichts aus und sagt es. Die Listen zeigen immer die **neuesten** 60 ZIP-Dateien.
  **Datenschutz:** Die ZIP-Datei enthält im `param.sfo` **keine Konto-Kennung** (`ACCOUNT_ID` steht dort als Null; das Einspielen trägt das Konto des Benutzers ein, für den es
  gedacht ist). Die übrigen Besitzerangaben (`PARAMS`: Benutzer-Kennung und der Wert, den alle Spielstände eines Benutzers teilen) bleiben darin, weil das Einspielen sie braucht,
  wenn die andere Konsole keinen Spielstand dieses Benutzers hat; die ZIP-Datei nur an Menschen weitergeben, denen man das zutraut.
  **Unterbrochenes Einspielen:** Von der ersten Umbenennung an führt die App eine kleine Aufzeichnung (`/data/PS5-Cooling-Center/savexport/import.journal`: Pfade, was vorher da
  war, der Eintrag der Datenbank, die Stufe). Fällt der Strom aus, wird die App ersetzt oder stürzt sie ab, **führt der nächste Start der App das Einspielen zu Ende oder nimmt es
  zurück**: Stand „Tausch“ → die alten Dateien kommen zurück; Stand „Datenbank“ (die Abbilder liegen schon richtig) → der Eintrag wird nachgetragen und die alten Dateien gehen.
  Ist die Datenbank dann gesperrt, bleibt die Aufzeichnung, und das nächste Einspielen versucht es zuerst noch einmal. Das Ergebnis steht im Protokoll und im Hinweis des
  nächsten Auftrags.
  **Aufräumen:** Von den Kopien der Spielstand-Datenbank (`/data/PS5-Cooling-Center/savexport/db-backup`) bleiben je Benutzer die **10 neuesten**; hochgeladene ZIP-Dateien im Eingang, die
  einen Tag lang niemand eingespielt hat, werden entfernt. Das geschieht beim Start der App und zu Beginn jedes Auslesens oder Einspielens und steht im Protokoll.
  **Lüfter:** Die Identität des Spielstand-Dienstes und die Sperre der Systemschnittstelle hält die App nur noch für das Ein- und Aushängen; während die Dateien eines eingehängten Spielstands
  gelesen oder geschrieben werden, regelt der Lüfter weiter und die Power-Optionen warten nicht.
- **PS4-Spielstände (ab 1.87.0).** „Als ZIP“ und „Spielstand einspielen“ gelten auch für PS4-Titel (`CUSA…`, Ordner `savedata`, Datenbank
  `/system_data/savedata/<Benutzer>/db/user/savedata.db`), mit diesen Unterschieden: Ein PS4-Abbild trägt seinen versiegelten Schlüssel nicht in sich, er liegt in
  der Begleitdatei `<Spielstand>.bin` neben `sdimg_<Spielstand>` (96 Byte); ohne sie lässt sich ein PS4-Spielstand nicht auslesen. Die ZIP-Datei enthält nur die
  Dateien, **kein** `chives.json` (auch Garlic SaveMgr schreibt für PS4 keines); sie wird als PS4-Spielstand an der Titel-Kennung `CUSA…` im `param.sfo` erkannt.
  Beim Einspielen berechnet die App die Größe des Abbilds aus den Dateien (Dateien plus ein Viertel plus 4 MB, mindestens 32 MB, in Schritten von 32 KiB), die
  Konsole legt es mit ihrem älteren Formatierer (`sceFsCreatePfsSaveDataImage`) und einem **neuen, für diese Konsole versiegelten Schlüssel** an (der Schlüssel kommt
  in `<Spielstand>.bin`), und die Dateien erhalten die Zugriffsrechte der ZIP-Einträge. Die Besitzerangaben im `param.sfo` reichen nicht aus (bei PS5 und PS4 gleichermaßen), das Feld
  `PARAMS` (1024 Byte: Benutzer-Kennung und ein 32-Byte-Wert, der in allen Spielständen eines Benutzers gleich ist) muss stimmen. Die App kann ihn nicht erfinden und
  nimmt ihn aus einem Spielstand desselben Benutzers an dieser Konsole: Sie legt dessen Abbild als **Kopie** in den Arbeitsordner, hängt es ein, liest sein `param.sfo`
  und hängt es wieder aus. Zuerst gilt ein Spielstand mit gleichem Titel und gleicher Kennung (der ganze Eintrag wird übernommen), dann einer mit gleichem Titel, dann
  irgendeiner (nur Benutzer-Kennung und Wert, Byte 4 bis 40), zuletzt einer der anderen Art (bei PS4 ein PS5-Spielstand); verwendet wird nur ein Spielstand, dessen `param.sfo` das Konto und die Kennung dieses Benutzers zeigt (höchstens
  acht Versuche). Gibt es keinen, wird trotzdem eingespielt, und der Auftrag sagt, dass das Spiel den Spielstand vielleicht nicht annimmt. Alles andere (Sicherung davor,
  Zurücklesen jeder Datei, Datenbank-Kopie, Zurückrollen bei einem Fehler) ist wie bei PS5.
- **Vollbild-App (ab 1.88.0).** Dasselbe geht am Controller, mit einem USB-Laufwerk als Weg: Im Menü eines Spiels (Quadrat-Taste) schreibt „ZIP auslesen auf …“ die ZIP-Datei in den Ordner `PS5-Sicherung/Spielstand-ZIP` des gewählten Laufwerks (das Ziel wie bei den Sicherungen; alle Spielstände des Spiels, eine ZIP-Datei je Spielstand). An der anderen Konsole zeigt die Liste „ZIP-DATEIEN“ alles, was in diesem Ordner auf den Laufwerken liegt; die X-Taste sieht eine Datei an (die App prüft sie und schreibt nichts), danach „Für <Benutzer> einspielen“ (ersetzt / ergänzt / neu); gehören mehrere ZIP-Dateien zum selben Lauf (gleicher Titel, gleiche Zeit im Namen), steht davor „Für <Benutzer> alle N Spielstände einspielen“. Im Browser lassen sich ebenso mehrere ZIP-Dateien auf einmal wählen. Die App legt jeden Spielstand in den Platz, den seine ZIP-Datei nennt, und sichert den Stand davor einmal für das ganze Spiel. Der Stand davor wird wie im Browser zuerst gesichert; die Datei auf dem Laufwerk bleibt liegen. Ihr Name darf nur Buchstaben, Ziffern, `_`, `-` und `.` enthalten. Eine ZIP-Datei vom PC (zum Beispiel von Garlic SaveMgr) kommt in denselben Ordner.
- **Aufräumen.** Die App löscht keine Sicherung, auch nicht den Stand vor dem Zurückspielen:
  Das geht am PC oder per FTP, indem man den Ordner unter `PS5-Sicherung/Spielstaende` entfernt.
  Die Seite listet die 40 neuesten Sicherungen (nach dem Zeitstempel im Ordnernamen), die
  neueste zuerst. **Unfertige** Sicherungen (zum Beispiel nach einem Stromausfall mitten im
  Sichern; sie tragen noch die Datei `.ps5cc-unfertig`) sind unbrauchbar und belegen Platz: Die
  Seite zählt sie in einem Hinweis, zeigt sie aber nicht an; sie lassen sich ebenfalls am PC
  oder per FTP entfernen. Das Ergebnis eines Vorgangs bleibt eine Viertelstunde sichtbar, auch
  wenn man die Seite neu öffnet; „Ausblenden“ schließt es.
- **Was die Prüfungen noch verlangen.** Eine Sicherung zählt nur, wenn sich beim Sichern keine
  Quelldatei geändert hat (Größe und Zeit werden vor und nach dem Kopieren verglichen) und
  kein Spiel gestartet wurde, während sie entstand. „Prüfen“ scheitert bei einer Sicherung ohne
  Dateien und bei einer Liste, deren Zahl nicht zu dem passt, was in ihrem Kopf steht. Eine
  Sicherung, deren Liste keine Konsole nennt, gilt nicht als die dieser Konsole. Ein Eintrag mit
  einer Größe, die keine Datei haben kann (negativ, riesig, keine Zahl), macht die Liste
  unzulässig: Das Zurückspielen weigert sich, bevor etwas geändert wird, und „Prüfen“ scheitert.

## Pakete finden und aufteilen

Auf der Seite **Spiele** zeigt der vierte Reiter, **Pakete**, die Spiel-Pakete (`.pkg`) auf
angesteckten USB-Laufwerken, Platten, in einem Disc-Laufwerk und im Konsolenspeicher. Er ist der Anfang
einer Paket-Verwaltung nach dem Vorbild des PS5 PKG Managers von itsPLK (siehe die Credits in der
[README](../README.de.md#credits-und-danksagung)): finden, aufteilen und [installieren](#pakete-installieren).

- **Wo gesucht wird.** Auf jedem Laufwerk unter `/mnt` im Hauptordner (dort nur die Dateien, die
  direkt darin liegen) und im Ordner `pkg` mit seinen Unterordnern, bis zu drei Ebenen tief
  (`pkg` oder `PKG`, die Schreibweise ist gleich); im Konsolenspeicher nur im Ordner
  `/data/pkg`. Andere Ordner werden nicht durchsucht, Verknüpfungen nicht verfolgt. Dateien unter 4 KB
  zählen nicht, und aus einer Suche bleiben höchstens 512 Pakete.
- **Was gelesen wird.** Von jedem Paket nur ein paar Stellen im Kopf, einige Kilobyte einer Datei, die
  zig Gigabyte groß sein kann: Titel-ID, Name (der deutsche Titel zuerst, sonst die Standardsprache
  des Pakets), Version, Content-ID, die Art (Spiel, Update, Zusatzinhalt), PS4 oder PS5 und das Bild des
  Pakets; bei einem PS5-Paket außerdem das **SDK-Ziel** (die Zahl, mit der das Programm gebaut wurde, als „SDK 5.xx“ im Kopf der Karte, aus den
  Prozessparametern des Hauptprogramms; nur wenn die Datei sie unverschlüsselt und eindeutig zeigt, sonst steht nichts da). Ein **Rückportier-Patch**
  (Kopf `LIH`, wie ihn manche Quellen für ältere Spiele auf neuer Firmware bieten) gilt als Update, ebenso ein Paket, dessen Inhaltsmarke
  (`0x40000000` oder `0x00100000`) sagt, dass es ein Patch ist. Es wird nichts geschrieben, und kein Paket wird ganz gelesen. Eine Datei, die kein Paket ist
  oder beschädigt, wird übersprungen (ihre Zahl steht als `failed` in der Antwort der Konsole). Alles, was
  eine Datei über sich sagt, wird gegen ihre Größe geprüft, ehe es benutzt wird.
- **Suchen.** Beim ersten Öffnen sucht die Seite von selbst, danach mit „Neu suchen“; die Konsole sucht
  im Hintergrund, und ein Paket, das sich seit der letzten Suche nicht verändert hat (Pfad, Größe, Zeit),
  wird nicht noch einmal gelesen. Die Liste steht nach Laufwerk geordnet; das Suchfeld und die Schnellwahl
  (Alle, Spiele, Updates, Zusatzinhalte) filtern sie.
- **Geteilte Pakete** (siehe unten) erscheinen als ein Eintrag: „geteilt, vollständig“, oder „Teile: 2 von 3“
  mit der Nummer des fehlenden Teils. Er kann auf einem anderen Laufwerk oder einer anderen Disc liegen.
- **Aufteilen.** Ein Paket ab 128 MB hat den Knopf „Aufteilen …“. Gewählt werden das Ziel und die Größe eines
  Teils: **FAT32-Stick** (Teile bis 4095 MB, denn dort darf eine Datei nicht größer als 4 GB sein),
  **Blu-ray, 25 GB** (23 GB je Teil), **Blu-ray, 50 GB** (46 GB) oder eine eigene Größe (mindestens 64 MB,
  höchstens 256 Teile). Die Seite zählt die Teile und sagt, wie viel Platz auf dem Ziel frei sein muss. Die Teile
  heißen `<Name>.pkg.part1`, `.part2`, … und liegen im Ordner `pkg` des Ziels. Ein Teil ist ein 4096 Byte
  großer Kopf (welches Paket, der wievielte Teil, wo das Stück in der ganzen Datei liegt, Titel, Content-ID, Version,
  im ersten Teil das Bild) und danach ein Stück des Pakets, so wie es ist. Dasselbe Format liest der PS5 PKG
  Manager.
- **Was das Aufteilen absichert.** Das Paket wird einmal der Reihe nach gelesen und nie verändert. Jeder Teil
  entsteht unter einem Zwischennamen (`….ps5cc-teil`), wird danach vom Laufwerk zurückgelesen (der Kopf wie
  geschrieben, das Stück mit derselben SHA-256 wie das, was aus dem Paket gelesen wurde) und bekommt seinen
  richtigen Namen erst, wenn **alle** Teile gut sind: Niemand sieht je einen halben Teil. Ein Fehler, ein gezogener
  Stecker oder „Abbrechen“ entfernt, was geschrieben wurde. Es wird nichts überschrieben: Gibt es im Ziel schon
  Teile dieses Namens, bricht die App vor dem Start ab. Ändert sich das Paket zwischen Suche und Start, wird
  nicht geteilt. Es läuft immer nur ein Vorgang, auch nicht neben Kopieren, Konvertieren, Verschieben oder den
  Spielständen, und der Ruhemodus wird währenddessen verhindert. Die SHA-256 des Pakets steht in der Meldung am Ende.
- **Das Bild und die Kennung.** Das Bild kommt aus dem Paket selbst und wird nur ausgeliefert, wenn es wirklich
  ein PNG ist. Der Browser nennt der Konsole nie einen Pfad: Jedes Paket hat eine Kennung aus der Liste der letzten
  Suche, und nur danach fragt die Seite.

## Pakete installieren

An den Karten des Reiters **Pakete** steht der Knopf **„Installieren“** (bei einem großen, noch nicht geteilten Paket neben „Aufteilen …“).
Er fehlt bei einem geteilten Paket, dem Teile fehlen, und er verschwindet, solange ein Vorgang dieser Seite läuft (Aufteilen oder eine Installation). Die Konsole
installiert das Paket selbst, mit der Installation, die auch das Menü benutzt (die Systembibliothek AppInstUtil). Die App
stellt es ihr nur bereit und zeigt den Fortschritt.

- **Der Klick startet die Installation (seit 28.10.2026).** „Installieren“ beginnt sofort, ohne zweite Bestätigung; die Konsole prüft beim Start alles selbst.
  Lehnt sie das Paket ab (oder lässt sich die Installation nicht beginnen), klappt unter der Karte ein Plan auf, den die Konsole für genau dieses
  Paket erstellt: Name, Art (Spiel, Update, Zusatzinhalt), Version, PS4 oder PS5, Größe (bei einem geteilten Paket die des
  ganzen Pakets und die Zahl der Teile), der freie Platz im internen Speicher und, wenn es schon installiert ist, welche Version.
  Rot steht, was die Installation verhindert, gelb, was man wissen sollte; „Jetzt installieren“ im Plan ist nur dann zu drücken, wenn nichts
  verhindert.
- **Was verhindert.**
  - *Das Spiel ist schon da:* ein Spiel, das die Konsole schon kennt (die App überschreibt nichts; zum Neuinstallieren erst an der
    Konsole löschen); ein Update, wenn das Spiel fehlt oder dieselbe oder eine neuere Version schon installiert ist; ein Zusatzinhalt, der
    schon da ist. Kann die App die Liste der Konsole nicht lesen, installiert sie ein **Spiel** aus Vorsicht nicht (sie könnte etwas
    überschreiben, ohne es zu merken); ein Update oder Zusatzinhalt geht dann mit einer Warnung weiter.
  - *Es läuft etwas:* das Spiel selbst (ein Zusatzinhalt darf trotzdem installiert werden); ein Kopieren, Konvertieren, Verschieben,
    Sichern, Teilen oder schon eine Installation, denn das alles will dieselben Laufwerke. Das gilt auch, wenn zwei dieser Vorgänge im
    selben Augenblick beginnen: Einer gibt nach.
  - *Das Paket stimmt nicht:* Die Datei hat sich seit der Suche verändert (Größe oder Zeit) oder ist ein Link; Plattform, Titel-ID oder
    Content-ID sind unbekannt. Bei einem **geteilten** Paket liest die App auch das Paket in den Teilen und vergleicht es mit dem Kopf der
    Teile: Nennt der Kopf eine andere Titel- oder Content-ID, sind die Teile „falsch beschriftet“ und es wird nichts installiert (die Regeln
    gelten dem Paket, das wirklich installiert würde, nicht dem, was ein Kopf behauptet). Ebenso, wenn sich in den Teilen kein Paket
    lesen lässt, wenn Teile komprimiert gespeichert sind, wenn es von einem Teil zwei verschiedene Dateien gibt, wenn ein Teil zu einem
    Paket anderer Größe gehört, wenn der Inhalt eines Teils nicht in seiner Datei liegt oder wenn die Teile nicht lückenlos aneinanderpassen und
    zusammen nicht die Größe des Pakets ergeben. Nennt der Kopf nur die falsche *Art* (Spiel, Update, Zusatzinhalt), gilt die aus dem Paket,
    und der Plan sagt es.
  - *Warnungen* (gelb, ohne zu verhindern): wenig oder unbekannter freier Platz (weniger als die Größe des Pakets plus 1 GB; je nach
    Einstellung installiert die Konsole auf der M.2-Erweiterung, sonst meldet sie bei Platzmangel selbst einen Fehler); ob ein Update-Spiel
    oder ein Zusatzinhalt schon installiert ist, ließ sich nicht feststellen; die Version des Spiels oder des Updates ist unbekannt (dann
    lässt sich das Ergebnis nachher nicht bestätigen).
- **Wie das Paket zur Konsole kommt.** Die App öffnet für diese eine Installation einen kleinen Server, der nur auf der Konsole selbst
  erreichbar ist (`127.0.0.1`, Port 18851), und liefert darüber genau diese Datei unter einem Namen, den es nur einmal gibt. Ein geteiltes
  Paket (`PS5MPKG1`) wird als eine Datei geliefert: Die Teile folgen einander, ohne dass etwas zusammengesetzt oder kopiert wird. Der
  Server beantwortet Bereichsanfragen (`Range`), wie sie die Installation stellt, bis zu 16 gleichzeitig, und er kennt keinen anderen
  Pfad, keine Ordnerliste und keine fremde Datei. Eine Anfrage, die länger als 10 Sekunden zum Ankommen braucht, wird abgebrochen (damit
  kein langsamer Fremder alle Plätze hält). Jede Paketdatei öffnet er ohne Links zu folgen und nur, wenn sie noch so groß und so alt ist wie
  bei der Suche. Er läuft nur, solange die Installation läuft; beim Beenden weckt er wartende Verbindungen auf, statt ihr Zeitlimit
  abzuwarten. **Die Konsole muss dazu mit einem Netzwerk
  verbunden sein** (LAN oder WLAN; Internet ist nicht nötig): Ihre Installation liest über das Netzwerk, auch von der eigenen Adresse.
  Ohne Netzwerk meldet sie den Fehler `0x80B21121`, und die App sagt das in Worten. Hängt eine Verbindung des Servers fest (ein
  Laufwerk antwortet nicht mehr), steht das im Protokoll; eine neue Installation startet, sobald sie zu Ende ist.
- **Ein Hilfsprogramm macht den Aufruf.** Den Aufruf in die Systembibliothek macht nicht die App selbst, sondern ein kleines Programm
  (rund 80 KB, in die App eingebaut; in der Prozessliste heißt es „ps5cc-inst.elf“), das sie über den Payload-Lader (Port 9021) startet.
  Was die Systembibliothek nach einem Fehler in ihrem Prozess zurückbehält (einen Platz, den sie nicht freigibt, eine halb offene
  Sitzung), und ein Aufruf, der nie zurückkehrt, bleiben in diesem Programm; die App, die den Lüfter zu betreuen hat, hängt nie daran.
  Beide sprechen über eine Verbindung auf der eigenen Adresse (Port 18853). Das Hilfsprogramm kennt ein Kennwort, das die App bei jedem
  Start zufällig in sein Abbild schreibt; wer sich mit einem falschen meldet oder nichts sagt, wird abgewiesen und hält niemanden auf
  (die App hört auf bis zu acht Verbindungen zugleich). Es wird immer nur eine Frage zugleich gestellt, damit ein Hilfsprogramm, das in
  einem Aufruf der Systembibliothek steckt, das Ende der App noch bemerkt. Ein Hilfsprogramm macht höchstens einen
  Installationsaufruf, auch ein Fehlschlag verbraucht es. Verschwindet die App, beendet es sich selbst. **Es beendet die
  Systembibliothek nie** und steigt mit `_exit` aus: Das Beenden der Bibliothek, kurz nach dem Aufruf, ließ das erste Hilfsprogramm an
  der Konsole abstürzen (ein Hilfsfaden der Bibliothek sprang dabei in die Adresse 0); was die Konsole für ein Programm vorhält, räumt sie
  selbst auf, wenn dessen Prozess endet. Seine Zeilen (mit Uhrzeit, auch welche Dateien es offen hat und warum es aussteigt) stehen in
  `/data/PS5-Cooling-Center/pkginst-helper.log`; die App leert die Datei vor der Installation und kopiert nach einem Fehler ihr Ende ins Protokoll.
- **Die Installation gehört der Konsole, nicht dem Hilfsprogramm.** Mit dem Aufruf trägt die Konsole die Installation bei sich ein und
  liest das Paket dann auf eigene Rechnung vom Server der App; sie las weiter, als das erste Hilfsprogramm schon tot war. Das
  Hilfsprogramm wird danach nur noch gebraucht, um zu fragen, wie weit sie ist, und gefragt wird mit der Inhalts-ID, die die Konsole beim
  Aufruf genannt hat: Das kann jedes Hilfsprogramm. Stirbt oder hängt das erste (es antwortet 40 Sekunden lang nicht), **bleibt der
  Server an, die Installation läuft weiter, und ein neues Hilfsprogramm übernimmt die Fragen** (bis zu sechsmal; die Karte sagt, dass es neu
  gestartet wird, das Protokoll nennt jedes Mal den Grund, und die Zeilen des alten stehen darin). Ist keines mehr zu bekommen, liefert die App
  das Paket trotzdem fertig aus: Der Fortschritt zeigt dann, was die Konsole geholt hat, und am Ende entscheidet die Liste der Konsole (bis
  zu zehn Minuten lang). Das Ende heißt in diesem Fall **„Paket geliefert, Ergebnis nicht bestätigt“** (gelb), nie „installiert“: Die Konsole hat
  dann nichts gemeldet. So lief am 05.10.2026 die erste echte Installation (LEGO Batman, 31,8 GB, gut 15 Minuten, danach in der Spieleliste):
  Jedes Hilfsprogramm verlor bald nach dem Start die Verbindung. Der Grund: Die Frage nach dem Fortschritt schreibt 712 Bytes zurück, das
  bisher bekannte Antwortfeld hat aber nur 600; der Rest überschrieb Werte des Hilfsprogramms, darunter die Nummer seiner Verbindung.
  Seit dem 06.10.2026 bekommt die Frage einen eigenen, großen Speicherbereich, und eine Installation endet wieder mit „Paket installiert“
  (an der Konsole bestätigt mit Moorhuhn). Außerdem liegt die Verbindung auf beiden Seiten auf einer hohen Nummer (Hilfsprogramm ab 100,
  App ab 200), und reißt sie doch ab, schreiben beide ins Protokoll, was sie gesehen haben (App: `pkg_install_ipc`; Hilfsprogramm: seine
  offenen Dateien rund um die ersten Fragen und wie viel die Systembibliothek geschrieben hat).
  Die Konsole zählt ein Spiel ohne Kopf und Signatur des Pakets, also etwas kleiner als die Datei, und meldet am Ende oft nur „spielbar“.
  Das gilt als fertig, sobald die App alles geliefert hat und die Konsole ihre eigene Größe erreicht hat.
- **Fortschritt.** Die Karte „Paket wird installiert“ steht oben auf der Seite, auch nach einem Neuladen, und zeigt die Phase, die Prozent,
  die Bytes (von der Konsole gemeldet, nie mehr, als die App gesendet hat), den Zustand, den die Konsole nennt (auf Deutsch: läuft, spielbar,
  abgeschlossen …), den Versuch, die Dauer und die geschätzte Restzeit. Die Seite fragt einmal in der Sekunde, solange man sie sieht. Bleibt
  die Antwort der App dreimal aus, sagt die Karte, dass die Anzeige veraltet sein kann (die Installation läuft auf der Konsole weiter). Es
  läuft nie mehr als eine Installation, und der Ruhemodus wird währenddessen verhindert.
- **Wann sie fertig ist.** Die Konsole muss „abgeschlossen“ melden und alle Bytes bekommen haben, oder „spielbar“ melden, während sie selbst
  zählt, dass sie das ganze Paket geholt hat (ein Titel ist oft „spielbar“, lange bevor das letzte Stück geholt ist; die Zahl, die der Server
  über das Gesendete führt, entscheidet allein nie). Danach schaut die App bis zu 90 Sekunden in der Datenbank der Konsole nach, ob das
  Ergebnis dort steht (der Titel, bei einem Update die neue Version, bei einem Zusatzinhalt dieser Inhalt). Das Ende hat darum **vier
  Gesichter:** *„Paket installiert“* (grün: in der Liste gefunden), *„Von der Konsole als fertig gemeldet“* (gelb: die Konsole sagt „fertig“,
  aber das Ergebnis steht nicht in ihrer Liste oder die Liste ließ sich nicht prüfen; bitte an der Konsole nachsehen), *„Paket geliefert, Ergebnis
  nicht bestätigt“* (gelb: die App hat alles ausgeliefert, aber kein Hilfsprogramm war mehr zu bekommen und die Konsole hat nichts gemeldet; bitte
  an der Konsole nachsehen) und *„Installieren fehlgeschlagen“*. Danach liest die App ihre Spieleliste neu. Vergisst die App die Installation
  (sie wurde neu gestartet), steht auf der Karte „Installieren unterbrochen“; was die Konsole dann tut, steht in ihrer Download-Liste.
- **Wenn etwas hakt.** Meldet die Konsole einen Fehler, der von selbst vergeht (ein Platz ist noch nicht freigegeben, die Systembibliothek ist
  noch nicht bereit, eine Zeitüberschreitung: `0x80B2116F`, `0x80B2100D`, `0x80B2100E`), versucht die App es bis zu dreimal, mit einem neuen
  Hilfsprogramm und einem neuen Namen, nach 2 und nach 5 Sekunden. Alle anderen Fehler stehen im Klartext auf der Karte, mit dem Code (zum Beispiel
  `0x80A30002`: nicht genug Speicherplatz auf der Konsole) und der Meldung der Konsole; ein Code erscheint nur, wenn er von der Konsole kommt. Eine
  langsame Antwort auf die Frage nach dem Fortschritt (bis 3 mal 10 Sekunden) beendet die Installation nicht, und eine, die ganz ausbleibt, auch nicht (das Hilfsprogramm wird dann ersetzt, siehe oben); ein Zustand „none“ in den ersten
  15 Sekunden gilt als „noch nicht angemeldet“. Als Fortschritt zählt, was die Konsole wirklich tut: Bytes, die sie als geholt meldet, Bytes, die
  der Server zum ersten Mal gesendet hat, und die Schritte nach dem letzten Byte (Verarbeiten, Kopieren), die bei einem großen Titel lange
  dauern; ein Stück, das die Konsole immer wieder liest, ist keiner. Tut sich drei Minuten nichts, steht ein Hinweis; nach zwanzig Minuten ohne
  Fortschritt endet die Installation mit einem Fehler (je nachdem „holt keine Daten mehr“ oder „alles übertragen, aber nicht als fertig
  gemeldet“). Ändert sich die Paketdatei während der Installation oder wird das Laufwerk abgezogen, endet sie ebenfalls mit einem Fehler.
- **Was die App nie tut.** Sie löscht oder deinstalliert nichts, überschreibt nichts, startet kein Spiel und verändert das Paket nicht (es
  wird nur gelesen). Eine Installation beginnt nur mit dem Klick auf „Installieren“ (oder „Jetzt installieren“ im Plan), und wenn die Konsole sie ablehnt, gibt es keinen
  Rückfall auf einen anderen Weg. Außer ihrem Protokoll schreibt sie bei einer Installation nur in `pkginst-helper.log` (das Protokoll des
  Hilfsprogramms, das vor jedem Start geleert wird).
- **Abbrechen.** „Abbrechen“ stoppt die Installation: Die Karte sagt sofort „Wird abgebrochen …“, das Hilfsprogramm und der Server werden
  beendet. Was die Konsole bis dahin schon angelegt hat (halb installierte Dateien, einen Eintrag), lässt sie unter Umständen liegen, und die App
  räumt es nicht selbst auf. Für ein großes Spiel reserviert die Konsole gleich dessen ganzen Platz (ein abgebrochener Versuch mit einem Spiel von 32 GB nahm so
  rund 34 GB); das gibt man an der Konsole in der Download-Liste mit „Löschen“ wieder frei. Die Karte sagt das, und bei einem Fehler, bevor die Konsole das Paket angenommen hat, sagt sie, dass nichts
  angelegt wurde.
- **Im Protokoll** (Seite „Protokoll“, und im Kernel-Log) steht, was bei einer Installation geschah, damit sich ein Fehler an der Konsole
  nachvollziehen lässt: `pkg_install_start`, `pkg_install_helper` (das Hilfsprogramm ist bereit), `pkg_install_called` (der Aufruf der
  Konsole mit Ergebnis und Inhalts-ID), `pkg_install_status` (bei jeder Änderung und alle 30 Sekunden), `pkg_install_retry`,
  `pkg_install_slow`, `pkg_install_helper_lost` (das Hilfsprogramm ist weg oder antwortet nicht mehr, mit dem Grund), `pkg_install_helper_restarted` und
  `pkg_install_helper_restart_failed` (ein neues übernimmt, oder es ließ sich nicht starten), `pkg_install_blind` (kein Hilfsprogramm mehr), `pkg_install_native_error` (die Meldung der Konsole), `pkg_stream_up`, `pkg_stream_request` (die ersten Anfragen der
  Konsole an den Server), `pkg_stream_odd` (Abgewiesenes: unbekannter Pfad, unmöglicher Bereich), `pkg_stream_read_error`,
  `pkg_stream_down` (Anfragen, gelieferte Bytes), und am Ende `pkg_install_done`, `pkg_install_unverified`, `pkg_install_failed` mit dem
  Grund und `pkg_install_detail` mit Phase, Versuch, Code, Status und den Zahlen des Servers, dazu `pkg_helper_log` (die Zeilen des
  Hilfsprogramms) und `pkg_loader_output` (was der Lader gesagt hat). Ein Zeilenumbruch in einem Paketnamen kann im Protokoll keine zweite
  Zeile erzeugen.

## Disc im Laufwerk (seit 21.10.2026, nach ps-disc-detect)

Hat die Konsole kein Laufwerk (eine Digital Edition, eine Pro ohne angestecktes Laufwerk), gibt es den Reiter „Disc“ nicht. Ob eines da ist, sieht die App auch bei ausgeschalteter Erkennung nach, ohne dem Laufwerk einen Befehl zu schicken. (`has_drive` in `GET /api/v1/disc`: nur das Durchreichgerät von `cd0` wird gesucht, höchstens alle 30 s.)

Der Reiter **Disc** auf der Spiele-Seite zeigt, welche Disc im Laufwerk liegt, und legt von einer PS2-DVD oder einer PS3-Disc auf Wunsch ein Abbild (ISO) an. Er beruht auf ps-disc-detect (GPL-3.0); die App hat dessen Vorgehen neu geschrieben (`src/disc.c`).

- **Einschalten:** Erst wenn der Schalter an ist, fragt die App das Laufwerk (alle anderthalb Sekunden: `/dev/xpt0` nennt das Durchreichgerät von `cd0`, dann GET CONFIGURATION für das Profil und READ DISC STRUCTURE für den DVD-Typ). Aus ab Werk, bleibt über einen Neustart der App hinweg an.
- **Erkennung:** Profil wie bei der Konsole (PS2 DVD-ROM `0xFF61`, PS3 DVD/BD `0xFF70`/`0xFF71`, PS4 BD `0xFF80`, PS5 BD `0xFF90`; PS1/PS2-CDs `0xFF50`/`0xFF60` kommen an der PS5 nicht vor). Titelkennung: PS1/PS2 aus `SYSTEM.CNF` (`BOOT`/`BOOT2`), PS3 aus `PS3_GAME/PARAM.SFO`, PS4 aus `bd/param.sfo`, PS5 aus `bd/param.json` (erster Eintrag `PS5GD`, Bytes 7 bis 15 der Content-ID), sonst der erste neunstellige Ordner unter `app/`; nichts lesbar ergibt `GENERIC_PSn`. Gelesen wird die eingehängte Disc unter `/mnt/disc`.
- **Abbild:** nur PS2-DVD und PS3-Disc (PS4/PS5 werden nicht kopiert, CDs kann das Laufwerk nicht lesen). `/dev/cd0` wird in Stücken zu 128 KiB in eine `.iso.part` gelesen (vorher: höchste Lesegeschwindigkeit per SET STREAMING), dabei CRC-32, MD5 und SHA-1; danach `fsync` und Umbenennen. Ziel `/data/PS5-Cooling-Center/disc/`, mit Platzprüfung (Größe der Disc plus 512 MiB). Ein schon vorhandenes Abbild gleicher Größe, auch eines von ps-disc-detect in `/data`, wird nicht noch einmal gelesen. Während des Auftrags hält die App die Ruhemodus-Uhr zurück, die Beobachtung pausiert, und ein Datei-Auftrag (Kopie, Konvertierung, Dateimanager) lässt sich nicht gleichzeitig starten. Eine Zeile im Verlauf entsteht am Ende. Neben dem Abbild liegt `<Name>.hash.json` (Titelkennung, Prüfsummen, Redump-Treffer).
- **Neue Disc automatisch sichern:** zweiter Schalter, aus ab Werk.
- **Redump:** `/data/redump.db` (mit `tools/build_redump_db.py` aus ps-disc-detect gebaut, Tabellen `serials`, `images`, `tracks`, bis 64 MB) liefert den Namen, wenn die Titelkennung genau einen hat, und die Prüfung des fertigen Abbilds (Größe, CRC-32, MD5, SHA-1 gegen `images`, Art `iso`). Bei einem Treffer wird nach dem Redump-Namen benannt, sonst nach der Titelkennung. Die Datei wird nur für diese Abfragen kurz in den Speicher gelesen. Nicht übernommen: CD-Abbilder (`.bin`/`.cue`), der IRD für PS3 (das Laufwerk gibt den Schlüssel nicht her), die eingebettete Liste (die Nutzlast der App ist ohnehin an ihrer Größengrenze).
- Schnittstelle: `GET /api/v1/disc`, `POST /api/v1/disc` (siehe API.md).

### Disc-Spiele dumpen: „Dumpen starten“ dumpt gleich mit

Im Reiter Disc steht unter „Disc-Spiele dumpen“ jedes Spiel, das von Disc installiert ist. **„Dumpen starten“** (so heißt der Knopf, solange nichts dagegen spricht; sonst heißt er „Starten“) startet das Spiel und dumpt es nach der Ladezeit gleich von selbst (die Dump-Warteschlange mit einem Titel; Ladezeit in den Einstellungen der Warteschlange, Vorgabe 30 s). Die App muss danach nicht wieder geöffnet werden, denn das Spiel liegt vor ihr. Die Konsole meldet **„Dump gestartet“**, **„Dump fertig“** oder **„Dump gescheitert“**. Voraussetzungen: Die Disc liegt im Laufwerk, im Dump ist ein Ziel gewählt, es läuft kein anderes Spiel, das Spiel hat noch keinen Dump, und keine Warteschlange läuft oder ist vorbereitet; sonst startet „Starten“ nur das Spiel, und „Dumpen“ folgt von Hand.

## Kernel-Log live

Auf der Seite **Protokoll** zeigt der zweite Reiter, **Kernel-Log live**, die
Meldungen der Konsole selbst, wie sie entstehen: Start und Ende von Spielen,
Abstürze, Fehlercodes des Systems und die Zeilen dieser App. Das ersetzt den
Umweg über `klogsrv` und `nc` am PC (Port 3232); es braucht kein weiteres
Programm.

- **Woher.** Aus dem Meldungspuffer des Kernels (`kern.msgbuf`), demselben, aus dem
  die App auch die Spielerkennung und die Mikrofon-Taste liest. Der Puffer ist ein
  Ring von rund einem halben Megabyte; die Seite holt im Sekundentakt, was seit der
  letzten Abfrage dazukam. Gelesen wird nur, solange der Reiter offen und die Seite
  vorn ist. `klogsrv` bleibt unberührt: Es liest `/dev/klog`, nach BSD-Art einen Strom,
  den das Lesen verbraucht; wer am Port 3232 mitliest, soll nichts an die Seite verlieren.
- **Uhrzeit.** Die Kernelzeilen selbst tragen keine Zeit. Die Spalte zeigt, wann die
  App die Zeile zum ersten Mal sah, auf etwa eine Sekunde genau, nach der Uhr der
  Konsole (die vom PC um Sekunden abweichen kann).
- **Filter.** Das Feld nimmt Wörter: Eine Zeile erscheint, wenn alle vorkommen, ohne
  auf Groß- und Kleinschreibung zu achten; ein `-` davor schließt ein Wort aus
  (`sce -fmem`). Dazu fünf Schnellwahlen: *Alles*; *Ohne Rauschen* (blendet aus, was
  die Konsole alle paar Sekunden von selbst schreibt: Speicherberichte, Bildwechsel,
  Oberfläche, Ressourcenverwaltung); *Diese App* (nur `[ps5tm.elf]`); *Spiele* (Starten,
  Beenden, Abstürze) und *Fehler*. Zeilen mit Fehlerwörtern stehen rot, Warnungen gelb,
  die der App grün.
- **Pause, Leeren, Mitlaufen.** „Pause“ hält das Fragen an; mit „Weiter“
  kommt nach, was in der Zwischenzeit geschah. „Leeren“ räumt nur das Fenster, die
  Zeilen, die danach kommen, sind neu. „Mitlaufen“ hält das Fenster am Ende; wer
  hochscrollt, um zu lesen, schaltet es damit aus, und es geht wieder an, wenn er ans
  Ende zurückscrollt.
- **Speichern, Herunterladen, Aufnahme.** Drei Wege, das Log aufzuheben:
  - „Speichern“ legt die Zeilen, die der Filter durchlässt, **auf der Konsole** ab, im Ordner
    `/data/PS5-Cooling-Center/klog-live-log`, als `klog-<Datum>_<Uhrzeit>.log` (der erste Satz
    der Datei nennt die Zeitzone des Browsers; ein Name, den es schon gibt, wird nie
    überschrieben, er bekommt eine Nummer).
  - „Herunterladen“ tut dasselbe wie das frühere „Speichern“: Der Browser lädt die Zeilen als
    `ps5-kernel-log.log` herunter, auf das Gerät, an dem die Seite offen ist. Auf der Konsole
    bleibt davon nichts.
  - „Aufnahme“ lässt die **Konsole selbst mitschreiben**: Sie liest das Kernel-Log einmal pro
    Sekunde und hängt die neuen Zeilen an eine Datei `aufnahme-<Datum>_<Uhrzeit>.log` im selben
    Ordner an, auch bei geschlossener oder neu geladener Seite, ohne dass der Browser etwas
    behalten muss, und mit allem, was bis zu einem Absturz geschrieben wurde (die Datei wird
    alle fünf Sekunden auf den Datenträger gebracht). Am Anfang stehen die letzten 200 Zeilen
    als Rückblick; der Filter der Seite gilt nicht, es wird alles aufgenommen. Die Aufnahme endet
    mit „Aufnahme beenden“, bei 64 MB, wenn auf der Konsole weniger als 128 MB frei sind oder
    wenn ein Schreiben fehlschlägt; sie beginnt nicht, wenn weniger als 512 MB frei sind. Das
    Ende und der Grund stehen am Ende der Datei, und die Seite meldet es.
- **Die Dateien auf der Konsole.** Unter dem Log listet eine Karte alles, was im Ordner liegt (neueste
  zuerst, mit Art, Datum und Größe) und lässt jede Datei herunterladen. **„Ordner leeren“** löscht
  nach einer Rückfrage die gespeicherten Auszüge und Aufnahmen; eine Aufnahme, die gerade läuft, bleibt.
  Gelöscht werden nur Dateien, die diese App angelegt hat (Namen `klog-….log` und `aufnahme-….log`);
  Unterordner und Dateien mit anderen Namen bleiben, und Verknüpfungen werden nie verfolgt.
- **Grenzen.** Die App hält die letzten 3000 Zeilen, die Seite bis zu 5000, im Fenster
  stehen höchstens 1500. Schreibt die Konsole schneller, als abgefragt wird, oder war
  die Seite lange zu, fehlen Zeilen; eine Hinweiszeile „Lücke“ steht dann an der
  Stelle. Die älteste Zeile jeder Abfrage und eine noch halb geschriebene letzte
  zählen nicht mit. Wiederholt die Konsole genau dieselben Zeilen in Folge, können
  einzelne Wiederholungen fehlen, denn das Ende der letzten Abfrage wird an den
  letzten sechs Zeilen erkannt.
- **Vorsicht beim Weitergeben.** Das Kernel-Log nennt Konsolenname, Benutzerkennung,
  Titel und Pfade. Wer es speichert und weitergibt, sollte es vorher durchsehen.
- **Zeiten.** In den Dateien auf der Konsole stehen die Zeiten nach der Zeitzone des Browsers,
  der „Speichern“ oder „Aufnahme“ gedrückt hat (in der ersten Zeile genannt; ohne Angabe, etwa
  mit `curl`, gilt UTC). Die Konsole selbst kennt ihre Zeitzone nicht verlässlich genug, um Uhrzeiten
  daraus zu drucken.

## Firmware

Die App ist mit dem PS5-Payload-SDK **v0.50** gebaut. Dessen Startcode kennt jede
Firmware **von 1.00 bis einschließlich 14.22** (14.00 und 14.10 seit v0.45, 14.20 und 14.22 seit v0.50); v0.41, mit dem die Versionen bis 1.46.0
gebaut waren, endete bei 13.40. Eine Firmware, die der Startcode nicht kennt,
kommt nicht bis `main()`: Das Programm startet dann nicht und schreibt nichts ins
Protokoll. Getestet ist nur die **Firmware 12.00** auf einer PS5 Pro; mehrere Nutzer melden, dass die App auch auf **4.xx bis 10.xx** und auf **13.60** läuft. Ob
Lüftersteuerung und Sensoren auf anderen Ständen so arbeiten wie dort, ist nicht geprüft.
Was die Konsole meldet, steht auf der Systemseite („Firmware“) und im Protokoll
(`firmware`).

## Payloads starten (ab 1.46.0)

Die Seite „Payloads“ zeigt drei Listen: was gerade läuft (mit „Beenden“), was im
Ordner `/data/PS5-Cooling-Center/payloads` liegt, und die `.elf`-Dateien auf
angesteckten USB-Sticks — im Hauptverzeichnis des Sticks und im Ordner `payloads`
darin. „Starten“ schickt die Datei an den Payload-Lader der Konsole (elfldr,
Port 9021), genau das, was sonst vom PC aus geschieht, nur ohne PC. Von einem
Stick lässt sich eine Datei außerdem in den internen Ordner kopieren (das
Original bleibt); aus dem internen Ordner lässt sie sich löschen (ein Fenster fragt zuerst und nennt den Namen).

- Der Ordner wird beim Start der App angelegt, **wenn es ihn nicht gibt**. Ein
  vorhandener bleibt unberührt: kein erneutes Anlegen, keine geänderten Rechte.
- Die Seite nennt der Konsole nie einen Pfad, nur Ort, Ordner und Dateiname aus
  deren eigener Liste; geprüft wird dort noch einmal. Gestartet wird nur, was
  auf `.elf` oder `.bin` endet, eine gewöhnliche Datei ist (kein Link) und mit dem Kopf eines
  64-Bit-x86-64-ELF beginnt. Andere Dateien erscheinen markiert und bleiben
  gesperrt.
- Ein Start nach dem anderen: Ein zweiter Klick während eines laufenden Starts
  oder kurz nach dem ersten auf dieselbe Datei wird abgewiesen.
- Startet ein Payload diese App selbst neu, reißt die Verbindung kurz ab; die
  Seite findet die neue Ausführung von allein wieder.

### Payloads herunterladen (seit 19.10.2026)

Der Bereich **Payloads herunterladen** auf der Payloads-Seite zeigt dieselbe Liste wie der
Payload Manager von itsPLK: `https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json`. Ein
Ablauf dort holt jeden Tag die neueste Fassung jedes Payloads (zurzeit 34, darunter
ShadowMountPlus, etaHEN, kstuff-lite, elfldr, ftpsrv, ps5upload, PKG-Manager) und trägt Name,
Version, Kategorie, Beschreibung, Datei und SHA-256 ein.

**Zweite Liste (seit 21.11.2026):** In derselben Liste stehen auch die Payloads von strongt1me, aus
`https://raw.githubusercontent.com/strongt1me/ps5-payload-source/main/payloads.json` (dieselbe Form:
Name, Datei, URL, Version, Kategorie, Beschreibung, SHA-256). Beide Listen werden zusammen geladen
und je in einer eigenen Datei gespeichert (`/data/PS5-Cooling-Center/payload-store/catalog.json` und
`catalog-strongt1me.json`). Ist eine nicht erreichbar, bleibt von ihr die gespeicherte Kopie in der
Anzeige, und der Grund steht unter der Überschrift; die andere wird trotzdem aktualisiert. Nennen
beide dieselbe Kennung oder dieselbe Datei, behält sie die erste Liste (itsPLK). Fehlt bei einem
Eintrag die Quellseite (`source`), steht der GitHub-Name des Autors (`author`) für sie, damit das
Entwicklerbild erscheint. Jede Datei wird, woher sie auch kommt, vor dem Speichern mit der SHA-256-Prüfsumme
aus ihrer Liste verglichen.

- **Liste laden:** Erst dieser Knopf geht ins Internet. Danach steht die Liste auch ohne Internet
  da; beim Öffnen der Seite wird sie höchstens alle drei Stunden erneuert. Suche und Kategorien
  (Werkzeuge, Netzwerk & Server, System & Jailbreak, Lader) filtern, „Geladen“ und „Mit Update“
  zeigen, was schon da ist.
- **Herunterladen:** Die Datei kommt in den Payload-Ordner (`/data/PS5-Cooling-Center/payloads`),
  zuerst unter einem versteckten Namen; erst wenn ihre SHA-256 zur Liste passt, bekommt sie ihren
  Namen. Eine Datei, die nicht passt, wird verworfen. Ein Download auf einmal, mit Fortschritt und
  „Abbrechen“.
- **Aktualisieren:** Steht in der Liste eine neuere Version (Vergleich wie im Payload Manager),
  zeigt die Karte „alt → neu“; „Aktualisieren“ ersetzt die alte Datei.
- **Starten** schickt die Datei wie gewohnt an den ELF-Lader (Port 9021); die Datei taucht auch
  unter „Gespeicherte Payloads“ auf und lässt sich in Profile aufnehmen.
- **Löschen** entfernt nur Dateien, die die App selbst heruntergeladen hat (`payload-store/installed.json`). Hat sich die Datei seit dem Laden geändert (Prüfsumme weicht ab), bleibt sie liegen und nur der Eintrag verschwindet. Ein zweites „Aktualisieren“, während die Liste noch lädt, meldet „Die Liste wird gerade geladen.“
  Liegt im Ordner schon eine fremde Datei gleichen Namens, fragt die App zuerst und nennt sie; ersetzt wird sie erst nach „Ersetzen“ (in der Vollbild-App nach einem zweiten Druck, der Hinweis nennt die Datei).
- Verbunden wird nur mit GitHub (https, nur über die App-eigene Namensauflösung und feste
  Wurzelzertifikate). Die Payloads stammen von ihren Autoren; die App prüft nur, dass die Datei
  die aus der Liste ist.

### Profile: Abläufe aus mehreren Payloads (ab 1.51.0)

Auf der Seite „Payloads“ steht über den gespeicherten Payloads der Abschnitt
„Profile“. Ein Profil ist eine **benannte Abfolge** aus Payloads des internen
Ordners und Pausen dazwischen (zum Beispiel erst kstuff, 3 Sekunden Pause, dann
ShadowMountPlus). „Ausführen“ startet die Abfolge Schritt für Schritt; „Anhalten“
beendet sie nach dem laufenden Schritt. Angezeigt werden Schritt, Gesamtzahl und
das Ergebnis jedes Schritts.

- **Startprofil:** Mit „Als Startprofil“ führt die App das Profil nach jedem
  eigenen Start von selbst aus (rund 25 Sekunden danach, wenn die Konsole mit
  ihrem Start fertig ist). Es gibt höchstens ein Startprofil.
- **Schutz vor Endlosschleifen:** Solange ein Ablauf läuft, liegt eine Markierung
  im Datenordner. Findet die App sie beim Start, wurde der letzte Ablauf nicht
  beendet (ein Payload darin hat die App oder die Konsole neu gestartet); dann
  läuft das Startprofil diesmal **nicht**, und das Protokoll sagt es.
- **Bearbeiten:** Name, Einträge hinzufügen (aus dem Ordner), Pausen einfügen
  (1 ms bis 10 Minuten), Reihenfolge ändern, Einträge entfernen, Profil löschen.
  Höchstens 24 Profile mit je 64 Einträgen.
- **Importieren:** „Vom PC importieren“ (bei den gespeicherten Payloads, und im
  Profil mit „Vom PC importieren und hinzufügen“) lädt `.elf`-Dateien vom Rechner
  in den Ordner der Konsole; eine vorhandene Datei wird nie überschrieben.
  „Profile sichern“ speichert alle Profile als Datei, „Profile laden“ fügt die
  Profile einer solchen Datei hinzu.
- Gespeichert wird in `/data/PS5-Cooling-Center/payload-profiles.json`.

Die Idee (Profile, Pausen, Startprofil) stammt vom ps5-payload-manager von itsPLK
(GPL-3.0); hier ist sie für den eigenen Payload-Ordner neu gebaut, ohne dessen
Code zu übernehmen.

### Backport-Quellen: fakelib2 und Backport-Ordner (ab 1.51.0)

Die Marken „Backport“, „AMPR EMU“ und „PlayGo“ folgen jetzt der Wahl von
ShadowMountPlus: Ein Ordner `fakelib2` ersetzt `fakelib` (er ersetzt, er ergänzt
nicht), und ein Ordner `<Scanpfad>/backports/<TITEL-ID>/` (ab ShadowMountPlus 1.7beta6
auch `<TITEL-ID> Spielname`: der genau so benannte gewinnt, sonst der erste in
Byte-Reihenfolge) mit `fakelib2` oder `fakelib` hat Vorrang vor den Bibliotheken im Spiel. Die Detailansicht des Spiels
nennt, aus welchem Ordner die Bibliotheken kommen. Gesucht wird neben dem Spiel
(bzw. seinem Abbild) und in `/data/homebrew`, `/data/etaHEN/games`, auf `ext0`/`ext1`
und auf USB unter `etaHEN/games`. Die globale Bibliothek unter
`/data/shadowmount/fakelib` zeigt die App nicht je Spiel an: Sie gälte für alle. (ShadowMountPlus ab
1.7beta6 kann sie je Spiel ausschließen, `ignore_global_fakelib` in seiner Schnittstelle; die App
liest und ändert das nicht.)

## Der Lüfter in der Oberfläche: der Ring (seit 19.11.2026; vorher ein Tacho ab 1.56.0, davor ein drehender Lüfter ab 1.53.0)

Die Lüfter-Kachel auf der Seite **Kühlung** zeigt die **gemessene** Drehzahl als **Ring**, wie die Temperatur daneben:

- **Bogen:** Er füllt sich mit der Drehzahl von 0 % (leer) bis 100 % (voller Ring) und zieht weich auf jeden neuen Wert nach. Dazwischen läuft nichts im Browser.
  Die Zahl (mit einer Nachkommastelle) steht in der Mitte.
- **Farbe:** Sie geht stufenlos von Blau (bis 25 %) über Grün (um 55 %) und Gelb (um 75 %) zu Rot (ab 95 %). Die Farbe stellt die Lüfterlast dar, nicht die
  Temperatur. Sie färbt den Bogen und den Rand der Kachel.
- **Kein Wert:** Ohne gültigen Messwert (oder ohne Verbindung zur App) ist der Ring grau und leer, die Zahl zeigt „--“.

Den gezeichneten Tacho (Komponente `<ps5-cooling-fan>` in `web/ps5-fan.js`) zeigt die Kachel nicht mehr; `ps5-fan.js` liefert nur noch die Farbstufen.

## Betriebsarten

| Modus | Verhalten |
| --- | --- |
| `automatic` | Komfortregelung: hält die Zieltemperatur, sanft und träge. |
| `observe` | Nur Beobachtung. Der Lüfter wird nur angefasst, wenn zuvor per Schnellwahl oder „Direkt setzen“ eine feste Schwelle gesetzt wurde. |

## Komfortregelung

Das Ziel ist **nicht** die niedrigste Temperatur, sondern ein ruhiger,
unauffälliger Lüfter — angelehnt an die Temperaturregelung von Xbox 360
DashLaunch.

| Parameter | Standard | Bedeutung |
| --- | --- | --- |
| `target_temp_c` | 66 °C | zu haltende Temperatur (50–91, bis 72 empfohlen) |
| `deadband_c` | 1 °C | Ruhezone nur beim Runterregeln: Die Drehzahl sinkt erst, wenn der Wunsch um 1 °C des Regelbands darunter liegt |
| `control_interval_s` | 1 s | Regelschritt jede Sekunde (Messung ebenfalls 1 s) |
| `average_window_s` | 4 s | gleitender Mittelwert (3 s bei Kühl) |
| `max_step_pct` | 1 % | Drehzahländerung pro Zyklus (2 % bei Ausgewogen und Kühl) |
| `safety_temp_c` | 78 °C | darüber wird der Lüfter hochgefahren (72–95; mindestens Ziel + 4) |
| `profile` | comfort | comfort / balanced / cool (= Leise / Ausgewogen / Kühl); legt Regelband, Ruhezone, Intervall, Mittelwert und Schrittweite fest |

Wie es arbeitet:

- Die Regelung nutzt **nie** den Rohwert, sondern den gleitenden Mittelwert.
- Die Drehzahl folgt der **weichen Lüfterkurve** (Abschnitt „Lüfterkurve, Obergrenze, Regelaufzeichnung“): Sie steigt schon 10 °C unter dem Ziel sanft an, nicht erst am Ziel. Nur das Runterregeln hat eine kleine Ruhezone (`deadband_c`).
- Der **Trend** zählt mehr als der Einzelwert: Fällt die Temperatur bereits,
  wird nicht weiter hochgeregelt.
- Hochregeln in 1-%-Schritten; Runterregeln nur 1 % und erst nach 20 s
  stabil niedriger Temperatur — bei deutlich untertemperierter Konsole
  entsprechend schneller.
- Direkter Eingriff ist weiter möglich: die **Schnellwahl** auf der Karte „Zieltemperatur“ bietet Presets
   (`Kühl`, `Ausgewogen`, `Leise`) und setzt je nach Modus entweder die
   Zieltemperatur (`automatic`: 62, 77 und 91 °C; bis 07.10.2026 waren es 62, 70 und 78 °C, bis
   03.10.2026 62, 66 und 70 °C, als der Regler noch nur bis 72 °C ging) oder eine feste Schwelle (`observe`:
   58, 65 und 74 °C).
- Netzbindung ist als Option verfügbar: `bind_address` kann in der UI direkt
   auf `0.0.0.0` (LAN) oder `127.0.0.1` (nur lokal auf der Konsole) gesetzt
   und gespeichert werden.
- Der Expertenmodus zeigt zusätzlich Rohfelder aus der Quelle, u. a.
   `firmware_raw`, `firmware_group`, `cpu_mode` (roh) und erweiterte
   Controller-Rohdaten.
- **Komfortgrenze** (65 % im Komfortprofil): verhindert, dass ein Thermostat,
  der sein Ziel unter Dauerlast nie erreicht, bis auf 100 % hochdreht.
- Ab `safety_temp_c` werden Kurve, Trend und Komfortgrenze ignoriert.
- **Ziele über 72 °C** (seit dem 03.10.2026 bis 78 °C, seit dem 07.10.2026 bis 91 °C einstellbar): Die Konsole läuft
  dann deutlich wärmer, der Lüfter bleibt länger auf seiner leisen Grundkurve. Die
  Notfallgrenze rückt dabei von selbst auf Ziel + 4 °C (bei 78 °C auf 82 °C, bei 91 °C auf 95 °C), sonst
  würde die Regelung mitten in ihrer Arbeit in den Notfallmodus kippen. Die
  Warnschwelle der App (`warning_cpu_c`) geht seit dem 07.10.2026 mit: Die wirksame Schwelle ist nie niedriger als
  Ziel + 2 °C (beim Spielprofil: das Ziel des laufenden Spiels), die Hauptchip-Schwelle liegt weiter 5 °C darüber. Der
  eingestellte Wert bleibt gespeichert und gilt, solange das Ziel niedrig genug ist. Die Firmware der
  Konsole schützt sich unabhängig davon weiter selbst.
- **91 °C** ist der Wert, den die Firmware der Konsole bei jedem Zustandswechsel selbst einstellt
  (gemessen am 24.09.2026, Firmware 12.00) und der höchste, den auch ShadowMountPlus anbietet. Ein Ziel von 91 °C
  heißt deshalb: so leise wie ohne diese App; erst über 91 °C greift die Regelung ein, und ab der Notfallgrenze
  (95 °C) geht der Lüfter zügig hoch.

### Wichtig: Schwelle statt Drehzahl

Der ICC-Controller kennt **keine** direkte Drehzahlvorgabe. Der einzige
Stellhebel ist eine **Schwellentemperatur** — sie wirkt wie ein Sollwert: Liegt
sie über der Temperatur, bleibt der Lüfter auf seiner leisen Grundkurve; liegt
sie darunter, dreht die Firmware proportional zum Abstand auf.

Ein Grad Schwelle entspricht etwa 5 % Drehzahl. Feine 1-%-Schritte sind damit
nicht direkt möglich, deshalb arbeitet die App zweistufig: Der Komfortregler
rechnet intern in Prozent, ein nachgelagerter Servo überträgt das auf die
Schwelle und greift erst ab 7 Prozentpunkten Abweichung ein (im Notfall ab 4). Er liest die **tatsächliche**
Drehzahl zurück und kalibriert sich damit selbst.

Die Schwelle wird mindestens alle 15 Sekunden neu gesetzt, weil die Firmware
den ICC-Zustand bei jedem Spiel-/App-Start zurücksetzt.

### Ruhelage der Schwelle (seit 07.10.2026)

Hat der Lüfter nichts zu tun (die Temperatur liegt am oder unter dem Ziel), parkt die App die Schwelle: bei Zielen bis
70 °C wie immer auf 80 °C, darüber auf **Ziel + 10 °C, höchstens 91 °C**. Der Grund: Die Firmware dreht den Lüfter
proportional zum Abstand über der Schwelle hoch und gibt schon innerhalb von etwa 20 °C darunter ein wenig Drehzahl dazu
(das Messmodell der Firmware 12.00 steht in `test/stubs_comfort.c`). Läge die Ruhelage fest bei 80 °C, könnte eine Konsole,
die bei 85 °C gehalten werden soll, nie so leise laufen, wie das Ziel verspricht. Bei einem Ziel von 91 °C ist die
Ruhelage die 91 °C, die die Konsole selbst einstellt. Überschreitet die Temperatur das Ziel, senkt der Servo die Schwelle
in Schritten von 1 bis höchstens 3 °C alle 3 Sekunden (im Notfall alle 2 Sekunden), bis die Drehzahl zum Bedarf passt.

### Schnellere Reaktion bei hohem Ziel (seit 17.10.2026)

Bei einem Ziel ab **85 °C** liegen nur wenige Grad zwischen Ziel und Notfallgrenze (Ziel + 4 °C): Das Band, in dem die
Drehzahl mit der Temperatur wächst, kann sich gar nicht entwickeln, bevor die Notfallregelung übernimmt. Die Simulation
mit einem Ziel von 91 °C ließ die Konsole bis 98 °C steigen. Steigt die Temperatur deutlich schneller als Messrauschen
(mehr als 6 °C pro Minute, gemessen am Mittelwert über 15 Sekunden), liest die App das Band deshalb bei der Temperatur,
die in etwa 20 Sekunden zu erwarten ist, höchstens 6 °C voraus. Bei gleichbleibender oder fallender Temperatur und bei
Zielen unter 85 °C ändert sich nichts: Für sie gilt das Verhalten, das bisher getestet wurde.

### Nachtmodus (seit 17.10.2026)

Auf der Kühlungsseite, unter „Zieltemperatur“ und „Betriebsart“, gibt es die Karte **Nachtmodus**: Zwischen zwei
Uhrzeiten (zum Beispiel 23:00 bis 07:00, das Fenster darf über Mitternacht gehen) gilt statt der Zieltemperatur das
**Nachtziel**, meist ein wärmeres und damit leiseres. Eine eigene **Spielregel** geht vor: Läuft ein Spiel mit Regel, gilt
deren Ziel. Gleiche Uhrzeiten bedeuten „nie“.

- Die Konsole kennt keine Zeitzone. Die Seite schickt deshalb beim Speichern den Zeitversatz des Geräts mit, auf dem sie
  geöffnet ist (aus dem Browser). Ändert sich dieser (Sommer-/Winterzeit), zieht die Seite ihn beim nächsten Öffnen
  still nach.
- Das Nachtziel liegt wie jedes Ziel zwischen 50 und 91 °C; die Notfallgrenze bleibt mindestens 4 °C darüber.
- Im Protokoll stehen `night_mode_on` und `night_mode_off`; im Status zeigt `control.night_active`, ob er gerade gilt, und
  `control.effective_target_c` das Ziel, nach dem geregelt wird.

### Lüfter-Selbsttest (seit 17.10.2026)

In der Expertenansicht, in der Karte „Lüfterdiagnose“, prüft der Knopf **Lüfter jetzt testen**, ob der Lüfter überhaupt
auf die Schwelle antwortet – unabhängig von der Temperatur. Die Regelung zeigt das sonst nur, wenn die Konsole heiß wird.
Der Test setzt die Schwelle 8 °C unter die aktuelle Temperatur (nicht unter 30 °C) und liest die Drehzahl jede Sekunde.
Steigt sie um mindestens 8 Prozentpunkte (frühestens nach 6 Sekunden), gilt der Lüfter als **reagierend**; passiert das
binnen 30 Sekunden nicht, steht dort **reagiert nicht**, mit den möglichen Ursachen (die Firmware beachtet die Schwelle
nicht, kstuff ist nicht geladen, die Lüftersteuerung ist nicht verfügbar). Danach übernimmt die Regelung wieder, ohne
Umweg über die Testschwelle; im Modus „Beobachten“ bekommt die Konsole ihre eigene Einstellung zurück.

- Er läuft nur bei kühler Konsole (höchstens 72 °C, und im Modus Automatik unter dem Ziel), nicht während der
  Notfallregelung und nur, wenn die Drehzahl lesbar ist; sonst wird er mit einem Grund abgelehnt.
- Der Lüfter wird dabei bis zu 30 Sekunden hörbar lauter. Der Knopf bricht einen laufenden Test ab.
- Das Ergebnis (Drehzahl vorher und höchstens, Dauer, Schwelle) bleibt bis zum nächsten Neustart der App stehen.

### Mindestdrehzahl der Konsole (seit 17.10.2026)

Die Konsole dreht den Lüfter nie unter einen eigenen Mindestwert, ganz gleich, wie niedrig die App ihn verlangt. Gemessen am
17.10.2026: **20 %** im Leerlauf, **34 %** in Spielen (in allen 18 aufgezeichneten Sitzungen bei 41 bis 66 °C). Die App
lernt diesen Wert während des Betriebs (das Minimum der letzten zehn Minuten, solange die Schwelle in der Ruhelage steht
und die Temperatur deutlich unter Ziel und Schwelle liegt, getrennt für Leerlauf und Spiel) und zeigt ihn in der
Lüfterdiagnose als „Mindestdrehzahl der Konsole“. Die Soll-Ist-Abweichung rechnet mit ihm: Im Leerlauf steht dort nicht mehr
dauerhaft „−7 %“, weil das Modell 13 % annahm und die Konsole 20 % hält.

### Ein einzelner ausgefallener Sensor (seit 17.10.2026)

Antwortet nur einer der beiden Temperatursensoren (Prozessor, Hauptchip) nicht, gilt dessen letzter Wert bis zu 10
Sekunden weiter, damit der Durchschnitt nicht um die Differenz der beiden fällt (im Spiel liegt der Hauptchip etwa 10 °C
unter dem Prozessor). Bleibt der Prozessor-Sensor länger weg, regelt die App nach dem Hauptchip allein und sagt es einmal im
Protokoll (`sensor_single`). Fallen beide aus, bleibt es bei der bisherigen Regel: nach 10 Sekunden hält die App den
Lüfter auf mindestens 70 %.

### Beenden der App

Beendet jemand die App mit SIGTERM oder SIGINT, gibt sie die Lüftersteuerung zuerst an die Konsole zurück (Protokoll
`fan_stop`) und endet innerhalb einer Sekunde; klemmt der Lüfterfaden, beendet ein Alarm sie nach 5 Sekunden. **SIGKILL**
lässt sich nicht abfangen: So beenden Werkzeuge wie
der Prozess-Manager Programme (die App selbst beendet eine ältere Ausführung beim Neustart zuerst mit SIGTERM, damit die Lüftersteuerung zurückgegeben wird, und erst nach etwa sechs Sekunden mit SIGKILL, wenn der Port dann noch belegt ist). Dann bleibt, wie bisher, die letzte Schwelle stehen, bis die Konsole sie beim nächsten
Zustandswechsel (Spielstart oder -ende, Ruhemodus) selbst auf 91 °C setzt. Das ist nie mehr als das, was die Konsole ohne
die App einstellt; die App kann den Lüfter nur schneller machen, nicht langsamer als im Auslieferungszustand.

### Vergleich mit ShadowMountPlus (`fan_target_temperature`)

ShadowMountPlus (SMP, auch über SMPlusGUI einstellbar) kennt `fan_target_temperature=system|50..91` in der `config.ini`.
Beide Programme stellen am Ende **dieselbe Größe** ein: die Schwelle, die die Konsole an ihrem Lüfterbaustein
(`/dev/icc_fan`, Byte 5) hat. Der Unterschied ist, wie:

| | SMP | diese App |
| --- | --- | --- |
| Was | schreibt einen **festen Wert** (50–91 °C) als Schwelle | hält eine **Temperatur** (50–91 °C) und stellt die Schwelle dafür laufend nach |
| Wann | einmal, wenn ein Spiel den Fokus bekommt; ab Werk `system` = schreibt nichts | Sekundentakt; mindestens alle 15 s neu gesetzt, sofort zurückgeholt, wenn die Firmware sie zurücksetzt |
| Rückmeldung | keine: der Wert gilt, ganz gleich, wie warm es wird | liest Temperatur und Drehzahl zurück, Mittelwert, Trend, Totzone, Notfallgrenze |
| Schutz nach oben | nur die Firmware der Konsole | zusätzlich die Notfallgrenze (Ziel + 4 °C, höchstens 95 °C) |
| Pro Spiel | ein Wert für alle Spiele | Spielprofile mit eigenem Ziel |

Ein hoher Wert in SMP und ein hohes Ziel in dieser App führen zum selben Ruhezustand des Lüfters; die App hält die
Konsole darüber hinaus nahe am Ziel, statt sie ungebremst steigen zu lassen. **Nicht beides gleichzeitig:** Steht in SMP eine Zahl und
läuft hier die Automatik, setzt die App ihre Schwelle innerhalb von höchstens 15 Sekunden wieder durch (das Protokoll
vermerkt es als Überschreibung); SMPs Wert hält dann nur kurz. Wer SMPs Wert will, lässt die Automatik aus (Betriebsart
„Beobachten“, dort gilt eine feste Schwelle bis 91 °C) oder lässt SMP auf `system`.

## Lüfterwächter (seit 1.75.0)

`src/fanguard.c` ist ein eigener Faden, unabhängig von der Regelung; er liest die Temperaturen jede Sekunde selbst.

- **Totmann:** Meldet sich die Regelung 20 Sekunden nicht, gibt er den Lüfter an die Konsole zurück – ist es heiß, stellt
  er volle Kühlung.
- **Übertemperatur:** Bleibt die Temperatur 10 Sekunden über der Sicherheitsgrenze + 6 °C, obwohl der Lüfter nicht nahe
  am Maximum läuft, hält er volle Kühlung, bis es 8 °C kühler ist.
- **Absturz:** Signal-Behandlung auf eigenem Stapel (jeder Faden bekommt 32 KiB `sigaltstack`); die Konsole bekommt den
  Lüfter zurück.
- Solange er hält, zeigt der Status „Wächter“, eine feste Schwelle von Hand wird mit `409 fanguard_holding` abgelehnt; die
  Sensorwerte im Status laufen weiter. Geprüft mit `test/sim` (Szenarien Totmann, Übertemperatur, Absturz).

## Lüfterkurve, Obergrenze, Regelaufzeichnung (seit 1.86.5)

- **Die Kurve (`band_duty` in `src/fan.c`):** Von 10 °C unter dem Ziel steigt der Wunsch der Automatik langsam von der üblichen
  Mindestdrehzahl (23 %) auf **28 % (Leise), 33 % (Ausgewogen), 40 % (Kühl)** am Ziel und von dort bis zur vollen Drehzahl
  (nach dem 1,75-fachen des Bandes der Betriebsart: Leise rund 24 K, Ausgewogen rund 17 K, Kühl 14 K über dem Ziel). Das
  Profil Kühl hat Schritt 2 %/s und Band 8 K. Ein Wunsch unter der Mindestdrehzahl der Konsole ändert nichts.
- **Aufholen:** Der Sprung um die halbe Lücke je Sekunde gilt nur noch nahe der Sicherheitsgrenze (innerhalb der Hälfte des
  Abstands zwischen Ziel und Grenze, 3 bis 10 K); bei weniger als 10 K Platz zwischen Ziel und Grenze bleibt es immer schnell.
  Der Servo (Schwellen-Nachführung) lässt 7 Prozentpunkte Abweichung zu und prüft alle 3 s. Der erste Sprung der Schwelle
  beim Spielstart wird um 3 Prozentpunkte gedämpft (`FF_JUMP_DAMP_PCT`).
- **Karte „Lüfterkurve“** (Browser, unter dem Nachtmodus) und **Reiter „Lüfterkurve“** der Vollbild-App (Kühlung): die Kurve
  als Bild mit der Temperatur von jetzt. Daten: `GET /api/v1/fan/curve`.
- **Obergrenze der Lüfterdrehzahl (`fan_cap_pct`, 30–100, ab Werk 100):** senkt nur die Grenze des Normalbetriebs
  (`comfort_ceiling`); Sicherheitspfad und Lüfterwächter ignorieren sie.
- **Warnschwelle folgt dem Ziel (`warning_follows_target`, ab Werk 1):** in der Automatik gilt max(`warning_cpu_c`,
  Ziel + 2); aus = genau `warning_cpu_c` (`ps5tm_config_warning_cpu_c`).
- **Regelaufzeichnung (`src/fantrace.c`):** Ring der letzten 30 Minuten, eine Zeile je Sekunde (`zeit_utc;cpu_c;soc_c;mittel_c;
  wunsch_pct;luefter_pct;schwelle_c;ziel_c;profil;spiel;sicherheit;automatik`), nur im Speicher.
  `GET /api/v1/fan/trace` liefert die Tabelle, `POST /api/v1/fan/trace/save` legt sie als `regelaufzeichnung-<Datum>-<Zeit>.csv`
  im Datenordner ab. Wechsel von Profil oder Ziel stehen im Protokoll (`profile_changed`, `target_temp_changed`).
- **Simulation:** `test/sim/fansim.c` kennt erzeugte Läufe (`gen:<Art>:<Ziel>:<c|b|k>:<Seed>:<Sicherheit>:<Modell>`), die
  Maße `fanmax`, `rise10`, `rise30`, `chg/min`, `above_safety` und Modelle mit den gemessenen Mindestdrehzahlen (Slim 13/23).

## Voraussetzung kstuff

`/dev/icc_fan` ist nur mit geladenem **kstuff** erreichbar. Fehlt es, laufen
Sensorik und Web-UI weiter, die Lüfterregelung meldet aber „nicht verfügbar"
— mit genauem `errno` im Protokoll.

## Zugriff und Sicherheit im Heimnetz

Die Web-Oberfläche ist im Heimnetz offen erreichbar — jedes Gerät im selben
Netz kann lesen **und** ändern. Das ist bewusst so: die Konsole steht im
eigenen LAN, und eine Kennwortabfrage brachte dort keinen Nutzen, sondern
nur eine Hürde. Wer das anders braucht, setzt `bind_address` in der
Konfiguration auf `127.0.0.1`; dann antwortet der Server nur noch der
Konsole selbst.

ShadowMountPlus nimmt seit 1.7beta8 ebenfalls standardmäßig Anfragen aus dem
ganzen Heimnetz an (Port 10101, ohne Anmeldung). Wer das nicht möchte, setzt
in dessen `config.ini` `api_bind_address=127.0.0.1`; die App spricht
ShadowMountPlus ohnehin über `127.0.0.1` an.

**Kraken Mount** (Port 8097, nur wenn du es im Reiter FPKGMount eingeschaltet hast; ab Werk aus) und **Dump Forge** (Port 8095, nur während einer Konvertierung) sind Programme anderer Autoren, die die App startet. Sie lauschen im ganzen Heimnetz und prüfen weder `Host` noch `Origin`; `bind_address` und der Schutz vor fremden Webseiten gelten für sie nicht. Wer das nicht will, lässt Kraken Mount ausgeschaltet.

Offen heißt nicht „für jede Webseite". Eine Seite, die im Browser eines
Geräts im Heimnetz geöffnet ist, kann dessen Verbindungen zur Konsole
mitbenutzen — ohne Kennwort hätte sie damit ausschalten, Spiele verschieben,
Zusatzprogramme beenden oder die Einstellungen ändern können. Seit 1.46.0
weist der Server deshalb ab, was erkennbar von einer fremden Webseite kommt:

- Der `Host` einer Anfrage muss eine IP-Adresse, `localhost` oder ein Name mit einer
  Heimnetz-Endung sein, die es im öffentlichen DNS nicht gibt (`.local`, `.lan`, `.home`,
  `.home.arpa`, `.internal`, `.localdomain`, `.intranet`). Seit 21.11.2026 nicht mehr: Namen
  ohne Punkt und `.fritz.box` – `.box` ist eine öffentliche Endung, und wer den DNS-Eintrag eines
  solchen Namens kontrolliert, könnte die App über eine eigene Webseite fernsteuern (DNS-Rebinding).
  Wer die Oberfläche bisher über `ps5.fritz.box` aufgerufen hat, nimmt die IP-Adresse der Konsole.
- Fragt der Browser im Namen einer fremden Webseite an (Kopfzeile `Sec-Fetch-Site` mit
  `cross-site` oder `same-site`), lehnt der Server jede API-Anfrage ab, auch eine lesende.
- Bei allem außer Lesen muss ein mitgeschickter `Origin` dieselbe Adresse nennen.
- Werkzeuge ohne `Origin` (curl, Skripte, die eigene Oberfläche über die
  Kachel) funktionieren wie bisher. Die Oberfläche am besten über die
  IP-Adresse der Konsole öffnen; ein eigener Name mit anderer Endung wird
  mit Fehler 403 und einem Hinweis abgewiesen.
- Höchstens 24 gleichzeitige Verbindungen; ein Browser braucht etwa sechs.
  Wer seine Anfrage nicht in 15 Sekunden (Kopfzeilen) beziehungsweise 20
  Sekunden (Inhalt) schickt, wird getrennt.
- Die Seite selbst kommt mit einer Content-Security-Policy: kein Skript von
  außen, kein Einbetten in fremde Seiten.

## Pakete in der Warteschlange installieren (ab 1.54.1)

Mehrere Pakete lassen sich nacheinander installieren, ohne dass man jedes einzeln anstößt.

- **Hineinlegen.** An jeder Paketkarte steht **„In die Warteschlange“**. Bei einem Titel mit mehreren Paketen legt
  **„Alle … Pakete dieses Titels in die Warteschlange“** Spiel, Updates (die älteren zuerst) und Zusatzinhalte in der richtigen
  Reihenfolge hinein. Die Karte **Warteschlange** oben auf dem Reiter zeigt eine Zeile je Paket mit Zustand (wartet, läuft, fertig,
  übersprungen, Fehler, abgebrochen).
- **Starten.** Nichts läuft von allein: Erst **„Starten“** setzt die Warteschlange in Gang. Jedes Paket wird mit der normalen
  Installation der Konsole installiert, mit Balken, Menge, Tempo und Restzeit in seiner Zeile.
- **Geprüft wird, wenn ein Paket dran ist.** Es gelten dieselben Regeln wie bei „Installieren …“. Ein Update kommt also erst dran,
  wenn das Spiel davor installiert ist. Was die Konsole ablehnt (Spiel oder Update schon installiert, Spiel fehlt, Datei verändert),
  wird **übersprungen**, der Grund steht in der Zeile, und die Warteschlange geht weiter.
- **Ein Fehler hält an.** Scheitert ein Paket (zum Beispiel zu wenig Platz, loses USB-Kabel), hält die Warteschlange an, statt alle
  weiteren der Reihe nach mit hineinzureißen. **„Weiter“** macht beim nächsten Paket weiter, **„Wiederholen“** versucht das
  fehlgeschlagene noch einmal. Ein Spiel, das gerade läuft, hält sein Update ebenso an, bis man es beendet hat und **„Weiter“** drückt.
- **Anhalten und Abbrechen.** **„Nach diesem Paket anhalten“** lässt das laufende fertig werden. **„Abbrechen“** in der Zeile stoppt die
  laufende Installation (wie sonst auch; was die Konsole schon angelegt hat, kann liegen bleiben) und hält die Warteschlange an.
- **Aufräumen.** **„Entfernen“** nimmt ein wartendes Paket heraus, **„Erledigte entfernen“** die fertigen, übersprungenen und
  abgebrochenen, **„Alle entfernen“** alles, solange nichts läuft.
- Die Warteschlange lebt im Arbeitsspeicher der App (höchstens 64 Pakete): Nach einem Neustart der App ist sie leer. „Vom PC
  installieren“ bleibt eine eigene Sache und läuft nicht gleichzeitig. Die Warteschlange löscht und überschreibt nichts.

## Pakete direkt vom PC installieren (ab 1.52.0)

Auf dem Reiter **Pakete** steht oben die Karte **„Vom PC installieren“**: Eine `.pkg`-Datei von dem Rechner, an dem der Browser läuft, wird
installiert, ohne sie vorher auf ein Laufwerk der Konsole zu kopieren. Datei wählen oder auf die Karte ziehen, dann den Plan lesen und
„Jetzt installieren“ drücken.

- **Es wird nichts auf der Konsole gespeichert.** Die Seite schickt die Datei in Stücken zu einem Megabyte, *während* die Konsole sie
  installiert. Die Installation braucht darum nur den Platz des installierten Spiels, nicht noch einmal die Größe der Datei.
- **Was die Konsole braucht, bekommt sie zuerst.** Die Konsole liest das Paket nicht der Reihe nach und nicht nur einmal: den Anfang
  mehrfach, dann zwei Ströme in großen Bereichen, bei einem zweiten Versuch wieder von vorn. Die App hält dafür einen Ring von 64
  Stücken (64 MB) im Arbeitsspeicher, das erste Stück bleibt für immer darin. Die Konsole meldet, welches Stück sie gerade braucht; die
  Seite schickt genau das, dahinter die nächsten, soweit im Ring Platz ist. Ein Stück, das gelesen wurde, wird als erstes ersetzt.
- **Der Plan** ist derselbe wie bei Paketen auf einem Laufwerk (Name, Art, Version, PS4/PS5, Größe, freier Platz, was verhindert). Die
  Angaben liest die Konsole aus dem Anfang der Datei; was keine PS4- oder PS5-Paketdatei ist, wird mit einem Satz abgewiesen. Geteilte
  Pakete (`.part1` …) gehen auf diesem Weg nicht.
- **Die Seite muss offen bleiben**, bis die Installation fertig ist (beim Schließen warnt der Browser). Die Seite arbeitet auch dann
  weiter, wenn der Reiter im Hintergrund liegt: Sie lebt von den Antworten der Konsole, nicht von Zeitgebern. Meldet sich der Browser länger
  als 40 Sekunden nicht, oder kommt ein gebrauchtes Stück zwei Minuten lang nicht, bricht die Konsole die Installation mit einem Satz
  ab, statt zu hängen; was sie schon angelegt hat, kann liegen bleiben (wie bei jedem Abbruch).
- **Ein Vorgang zugleich.** Eine Übertragung läuft nicht neben einem Kopieren, Konvertieren, Verschieben, Sichern, Löschen, Teilen oder einer
  anderen Installation. Eine Übertragung, an der niemand mehr teilnimmt, endet nach drei Minuten; ein Plan, den man offen lässt, nach einer
  halben Stunde.
- **Netzwerk wie bei jeder Installation:** Die Konsole liest das Paket über die eigene Adresse (siehe oben); sie muss mit einem Netzwerk
  verbunden sein.

## Profilbilder

- Auf der Seite „Profil“ stehen **30 fertige Profilbilder** zur Auswahl. Sie
   stecken in der App selbst (`web/avatars/`, 440 × 440 JPEG, gebaut mit
   `tools/build_avatars.py` aus den Originalen), man braucht also weder eine
   Datei noch einen PC: Bild antippen, Vorschau ansehen, „Profilbild
   übernehmen“. Sonst läuft alles wie bei einem eigenen Bild.
- Bis 1.57.0 ließ sich ein Bild als Paket speichern und später wieder laden; diese Funktion gibt es nicht mehr.
- **Eigene Bilder in der Vollbild-App (seit 1.75.0):** JPG oder PNG bis 16 MB in `/data/PS5-Cooling-Center/Profilbilder`
  (legt die App beim Start an) oder auf einem USB-Stick ins Hauptverzeichnis bzw. in einen Ordner `Profilbilder`. Sie
  stehen vorn im Raster mit der Marke „EIGENES“. Die Vollbild-App rechnet sie auf der Konsole um (Quadrat, DDS und PNG wie
  die Weboberfläche). X-Taste übernimmt, Quadrat-Taste holt das vorherige Bild zurück, Dreieck-Taste ändert den Namen.
  Der Ordner `avatar` im Datenordner ist der Arbeitsordner der App und wird nicht nach Bildern durchsucht.

## Homescreen-Kachel (veraltet)

> **Nur für ELF ohne Kachel:** Die Oberfläche bietet „Kachel installieren“ seit 1.74.1 nicht mehr an. Seit 1.76.1 legt die
> App die Kachel wieder selbst an, aber nur dort, wo sie sonst nichts zum Starten hätte: etwa eine Minute nach dem Start,
> wenn weder `PSCC69690` noch ein Ordner `FAKE86921`/`FAKE86922` in `/data/homebrew` da ist und gerade kein Spiel läuft.
> Das geschieht **einmal**: Die Notiz `tile-auto` im Datenordner hält `ok` (nie wieder) oder `fail <Version>` (erst mit der
> nächsten Version wieder) fest. `POST /api/v1/tile/ensure` legt sie weiter auf Wunsch an. Wer `FAKE86921` oder
> `FAKE86922` nutzt, bekommt sie nicht, denn diese Kacheln starten die App selbst (nächster Abschnitt).

Die Kachel (Title-ID `PSCC69690`, Bereich **Medien**) enthält selbst kein
Programm: `eboot.bin` ist 0 Byte groß, und `deeplinkUri` in `sce_sys/param.json`
zeigt auf `http://127.0.0.1:8086`. `applicationCategoryType: 65536` sortiert sie
in die Medienreihe. Sie öffnet nur die Oberfläche; nach jedem Neustart der
Konsole muss das Hauptprogramm trotzdem erst gesendet werden.

**Seit 1.47.0 legt das Hauptprogramm die Kachel selbst an**, ein Installer ist
dafür nicht mehr nötig:

- Die wenigen Dateien einer Kachel (`param.json`, `icon0.png` und die Bilder, rund
  1,7 MB) stecken in der ELF. Fehlt die Kachel beim Start, schreibt die App sie nach
  `/user/app/PSCC69690/sce_sys` und bittet das System, den Ordner anzumelden:
  mit `sceAppInstUtilAppInstallTitleDir` oder, wo eine Firmware sie nicht kennt (FW
  12.00 kennt sie nicht), mit der Sammelanmeldung `sceAppInstUtilAppInstallAll`. So
  sind auch die anderen Starter-Kacheln auf die Konsole gekommen. Dazu nimmt der
  Prozess kurz die Identität von ShellCore (`0x3800000000000010`) an und kehrt vor
  der Lüftersteuerung auf deren Identität (`0x4801000000000013`) zurück. Ist die
  Kachel da, passiert nichts.
- **Das geschieht einmal.** Wer die Kachel in der Konsole löscht, hat das mit Absicht
  getan; sie kehrt nicht bei jedem Start zurück. Mit `POST /api/v1/tile/ensure` lässt sie sich jederzeit wieder
  anlegen (die Oberfläche bietet „Kachel installieren“ seit 1.74.1 nicht mehr an). Eine neue Version versucht es nach einem
  Fehlschlag einmal neu. Zwei kleine Notizen im Datenordner halten den Stand fest:
  `tile-auto` (was aus dem automatischen Versuch wurde) und `tile-staging` (die
  Dateien liegen, sind aber noch nicht angemeldet; eine so halbfertige Kachel gilt
  nicht als installiert).
- **Vorsicht bei der Sammelanmeldung:** `sceAppInstUtilAppInstallAll` meldet alles
  an, was unter `/user/app` bereitliegt. Die App verweigert sie deshalb, wenn dort
  noch andere Titel liegen, die nicht installiert sind (Abgleich mit der
  App-Datenbank). Dann bleibt der Weg über das Paket.

**Der Weg über ein Paket** ist der Rückfall (so ging es bis 1.46.0).
`POST /api/v1/tile/ensure` versucht zuerst den Ordner und dann ein PKG über
`sceAppInstUtilAppInstallPkg`. Gesucht wird in dieser Reihenfolge:

```text
/data/PS5-Cooling-Center/tile.pkg
/data/PS5_Cooling_Center.pkg
/mnt/usb0/PS5_Cooling_Center.pkg
/mnt/usb1/PS5_Cooling_Center.pkg
```

Seit 19.10.2026 liegen der Veröffentlichung kein Kachel-Paket und kein eigener
Installer (`cooling-center-launcher-installer_v<Version>.elf`) mehr bei: Der Ordnerweg
oben reicht. Wer noch ein Paket aus einer älteren Veröffentlichung hat
(`IV9999-PSCC69690_00-PS5COOLINGCNTR01-A0100-V0100.pkg`), kann es weiter verwenden. Im Hauptprogramm ist das Paket nicht
eingebaut: Es kostete dort 10,5 MB Speicher, und die Konsole konnte dann keine
Anfrage mehr bedienen (siehe [ENTWICKLUNG.md](ENTWICKLUNG.md#bauen)). Mit `POST /api/v1/tile/upload` lässt sich ein Paket auch hochladen (die Oberfläche bietet das nicht mehr an).

Wie die Kachel-Dateien eingebettet und das Paket gebaut werden, steht in
[ENTWICKLUNG.md](ENTWICKLUNG.md#kachel-dateien-und-kachel-paket-bauen).

## Kacheln `FAKE86921` (Vollbild) und `FAKE86922` (Browser)

Mit den Kacheln muss die ELF nicht mehr gesendet werden. Aus der Veröffentlichung `FAKE86921.zip` und/oder
`FAKE86922.zip` entpacken und die Ordner nach `/data/homebrew/` kopieren (per FTP oder mit dem Dateimanager);
ShadowMountPlus legt dafür Kacheln im Medien-Bereich an. Beide können nebeneinander liegen.

**Neue Titel-IDs ab 1.74.1:** Bis 1.74.0 hießen die Kacheln `PPSA86921` und `PPSA86922`. ShadowMountPlus hielt eine
offene PPSA-Kachel für ein laufendes Spiel und lehnte währenddessen Löschen, Aushängen, Verschieben und Entpacken ab
(„game_active“; so bis ShadowMountPlus 1.7beta9, ab 1.7beta10 sperrt ein laufendes Spiel nur noch sich selbst und seine Quellen). Mit `FAKE…` geschieht das nicht. Die alten Ordner `/data/homebrew/PPSA86921` und `/data/homebrew/PPSA86922`
und ihre Kacheln danach löschen.

- **`FAKE86921`, die Vollbild-App** (seit 1.75.0): startet die App, falls sie noch nicht läuft, und zeigt sie im
  Vollbild mit Controller-Bedienung. Gebaut auf dem Oberflächen-Baukasten ps5-homebrew-ui (`tvapp/`); Einzelheiten im
  Abschnitt „Die Vollbild-App“ unten.
- **`FAKE86922`, die Browser-Kachel** (bis 1.74 die einzige Kachel, damals unter `PPSA86921`): startet die App, falls sie
  noch nicht läuft (die `eboot.bin` schickt die eingebettete ELF an den Lader auf `127.0.0.1:9021`), wartet, bis die
  Oberfläche antwortet, und öffnet sie über `POST /api/v1/open-ui` im Browser der Konsole. Läuft die App schon, öffnet
  sie nur die Oberfläche.
- Die `eboot.bin` ist ein nativer, signierter PS5-Titel (gebaut mit `tools/make_starter.sh` nach dem Vorbild von
  ps5-native-app-boilerplate); eine einfache ELF startet die Konsole nicht. `sce_module/libc.prx` verlangt der App-Lader.
- Die App steckt in der `eboot.bin`: Nach einem Update den Ordner aus der neuen Veröffentlichung kopieren (die
  Vollbild-App muss dafür geschlossen sein, sonst lehnt FTP ihre `eboot.bin` ab).
- **Eine ältere laufende App wird ersetzt (seit 1.77.1).** Bis 1.77.0 schickten die Kacheln ihre App nur, wenn auf
  Port 8086 nichts antwortete; eine noch laufende ältere blieb stehen und zeigte zum Beispiel weiter „1.76.9“. Jetzt
  vergleichen die Kacheln beim Antippen die Versionen: Die laufende App nennt ihre in `GET /api/v1/system`
  (`app_version`), die ELF in der Kachel trägt die Marke `PS5CC-VERSION:<Version>:` (`src/main.c`).
  - Ist die laufende App **älter**, tritt die eingebaute an ihre Stelle (die neue beendet die alte beim Start); die
    Kachel wartet, bis die neue Version antwortet, höchstens 30 Sekunden.
  - Eine **gleiche oder neuere** App bleibt, ebenso eine ohne Versionsangabe.
  - Läuft ein **Auftrag** (Abbild/LZ4/zstd-Pakete, Dateimanager, Kopieren/Konvertieren/Löschen, Paket-Installation,
    Spielstände, AMPR-Index), wird nichts ersetzt, denn das Ende der App würde ihn abbrechen. Die Kachel meldet
    „Eine neuere App liegt bereit“; nach dem Auftrag noch einmal öffnen.
  - Kacheln aus 1.77.0 oder älter können das noch nicht: einmal die neue ELF an Port 9021 senden (sie beendet die
    ältere Instanz selbst) oder die Konsole neu starten und dann die Kachel öffnen. Geprüft wurde die Vergleichslogik
    am PC, das Ersetzen an der Konsole noch nicht.

## Die Vollbild-App (seit 1.75.0)

Die Vollbild-App kann fast alles, was die Weboberfläche kann (Vorgabe: beide Oberflächen gleich); beide sprechen mit
derselben laufenden App über `127.0.0.1:8086`.

- **Tasten:** Touchpad-Taste öffnet die Seitenleiste (nur sie – links auf dem Steuerkreuz gehört der Seite). L1/R1
  vorheriger/nächster Bereich, L2/R2 Unterreiter, Steuerkreuz auswählen und Werte ändern, X-Taste starten/bestätigen,
  Kreis-Taste zurück, Quadrat-Taste Aktionen, Dreieck-Taste Details oder „Mehr“, Options-Taste Menü („Weboberfläche im
  Browser öffnen“, „Neu laden“, „Sprache / Language“, „App beenden“, Power-Optionen). Die Leiste unten zeigt die gerade
  gültigen Tasten.
- **Sprache (seit 1.75.3):** Deutsch, Englisch, Spanisch, Französisch, Italienisch, Russisch, Schwedisch. Ab Werk wie die
  Systemsprache der Konsole (andere Sprachen: Englisch); Options-Taste → „Sprache / Language“ öffnet eine Liste
  aller Sprachen (Häkchen = aktiv); ein Druck wählt, die Wahl bleibt gespeichert. Die Texte kommen aus denselben Wörterbüchern wie im
  Browser; was keines kennt, bleibt deutsch.
- **Bereiche:** Kühlung (Übersicht, Einstellungen mit Warnungen, Lichtleiste, Spielprofilen, Nachtmodus, Selbsttest; dazu der Reiter Lüfterkurve mit der Kurve der Automatik und der Regelaufzeichnung),
  Spiele (Bibliothek mit Aktionen-Menü – Kopieren, Konvertieren, Verschieben, Entpacken, Abbild prüfen, Löschen, Extras,
  Aus-/Einhängen, laufendes Spiel beenden –, Spielzeit, Spielstände, Pakete mit Installieren/Warteschlange/Aufteilen,
  PS4 PKG von USB, FPKGMount, Disc, Sicherungen, Aufträge), Dateimanager, Payloads (eigene, geladene, Store, Profile mit Editor),
  System (Konsole, ShadowMountPlus, Speicher, Kühlleistung, Zusatzabfragen, Experte), Profil (Name, Profilbild, Konto, Danke) und Protokoll
  (App-Protokoll, Kernel-Log live, Gespeichert).
- **Nur im Browser:** Hochladen vom PC, Herunterladen, Profile als Datei sichern/laden, Links öffnen. Unter System zeigt
  die Vollbild-App einen QR-Code mit der Adresse der Weboberfläche.
- **Eingaben** über die Bildschirmtastatur des Baukastens (eigene Seite mit ä ö ü ß). Hintergrundbilder je Bereich unter
  System → Speicher → Hintergrundbilder (eigene Bilder lädt man über die Weboberfläche hoch, die du mit Options → „Weboberfläche im Browser öffnen“ aus der Vollbild-App startest: Nur dann zeigt die Seite System die Karte „Hintergründe der Vollbild-App“).
- Beenden, Abbrechen und die Power-Optionen wirken beim ersten Druck, ohne Rückfrage. **Löschen** fragt zuerst und nennt, was weggeht.

## Tasten am Controller (seit 1.66.0)

Im Browser der Konsole wechseln **L1** und **R1** zur vorherigen und nächsten Seite der Seitenleiste, **L2** und **R2**
rollen die Seite um knapp eine Bildschirmhöhe nach oben und unten. Der Browser der PS5 meldet diese Tasten der Seite
als Tastendrücke F5 bis F8 (an der Konsole gemessen); die Seite fängt sie ab, ein Neuladen lösen sie nicht aus. Die
Gamepad-Schnittstelle des Browsers liefert auf der PS5 nichts.

## Bildschirmanzeige auf der Konsole

Beim Start meldet sich der Payload mit einem Hinweis oben links (einzeilig, ohne Ton) —
Version, IP-Adresse und Port der Web-UI. Mit einem Zeilenumbruch spielte die Konsole dabei ihren Meldeton, deshalb steht alles in einer Zeile. Weitere Meldungen kommen nur bei einer Warnung oder einem Fehler.

Auf Knopfdruck geht es auch: Wer die **Mikrofon-Taste des Controllers zweimal kurz
hintereinander** drückt, sieht oben links (ohne Ton) kurz Prozessortemperatur und
Lüfterdrehzahl — auch ohne geöffnete Web-Oberfläche (Schalter unter
*Kühlung → Anzeige per Mikrofon-Taste*, höchstens alle 5 Sekunden). Das
Mikrofon wird dabei zweimal umgeschaltet und ist danach wieder wie vorher;
ein einzelner Druck löst nichts aus. Ein Payload darf den Controller nicht
lesen, deshalb wertet die App die Zeile aus, die das System für jeden Druck
selbst ins Kernelprotokoll schreibt (`MicMuteKeyPressed`). Diese Zeile gibt
es erst ab Firmware 10.01; auf älteren Firmwares (z. B. 9.60) zählt die App
stattdessen die Zeile, mit der das System den neuen Stummschalt-Zustand meldet
(`onPostUmmStatusChanged`). Die meldet sich auch, wenn sich der Zustand aus
anderem Grund ändert (Headset, Party), dann kann selten eine Meldung ohne
Tastendruck kommen. Bis 1.45.x lag diese Anzeige auf der PS-Taste.

**Spiel im Hintergrund pausieren (seit 1.86.5).** Geht ein Spiel in den Hintergrund und ist der Startbildschirm vorn,
pausiert die App das Spiel selbst (Schalter unter *Kühlung → Spiel im Hintergrund pausieren*, ab Werk an; in der
Vollbild-App unter *Einstellungen → Spiel im Hintergrund*). Das schützt vor der Fehlermeldung der Konsole („Device
busy“), die beim Antippen einer Kachel der App erscheinen konnte: Die Konsole pausiert ein laufendes Spiel im Augenblick
des Antippens ohnehin, scheitert dabei aber einmal (`launchApp` meldet `checkExistingApp 0x80940011`, weil das Spiel noch
läuft), und ShadowMountPlus gibt in dieser Zeit die Einhängung der Kachel frei, sodass der zweite Versuch der Konsole mit
`status=16` abgelehnt wird. Ist das Spiel schon pausiert, entfällt der erste Fehlstart. Die App ruft die Konsole in einem
eigenen kleinen Hilfsprogramm auf (`src/helper/bgpause_helper.c`: `sceLncUtilSuspendApp` für genau die Anwendungs-ID des
Spiels, nur wenn der Dienst sie als die laufende große Anwendung meldet; kurz davor liest das Hilfsprogramm noch einmal das Kernel-Log
und bricht ab, wenn der Startbildschirm nicht mehr vorn ist; es beendet sich nach 8 Sekunden von selbst), nur für ein echtes Spiel (PPSA/CUSA, nie eine
Kachel der App und nie eine Medien-App wie YouTube: Die Liste der Spiele-Seite entscheidet, `ps5tm_library_is_game`), nur wenn der Startbildschirm selbst vorn ist (die letzte Szenenänderung der Konsole ging zu
`Render.NPXS40002`, kein Overlay), nur einmal je Aufenthalt im Hintergrund und nur, solange ShadowMountPlus antwortet.
Solange ein Spiel läuft, liest die App seinen Zustand zweimal pro Sekunde statt alle 4 Sekunden. Fortsetzen geht wie
sonst. Gesehen am 17.11.2026: PS4-Spiel aus PS4Mount auf der .94 und FPKG-Spiel aus Kraken Mount auf der .52, jeweils
Pause etwa 1 Sekunde nach der PS-Taste, Kachelstart ohne Fehlstart und ohne Meldung. Protokoll: `bg_pause` (pausiert),
`bg_pause_skip` (war schon pausiert), `bg_pause_failed` (Grund in der Zeile).

Ein **dauerhaft eingeblendetes Overlay** wie bei etaHEN ist damit nicht
gemeint und von außen auch nicht möglich: dafür müsste sich die App in den
Prozess des laufenden Spiels einklinken und dessen Renderer hooken. Die
Systembenachrichtigung erscheint dafür zuverlässig über Spielen und Menüs,
ohne irgendetwas zu destabilisieren.

## Weboberfläche nach einem Netzwerkwechsel (seit 21.10.2026, nach PatchDL 0.3.4)

Wird das WLAN aus- und wieder eingeschaltet oder das Kabel gezogen, beendet die Konsole mitunter den Zugang der Weboberfläche, obwohl die App (und die Lüftersteuerung) weiterläuft: `select()` meldet nichts, ein Verbindungsversuch wird aber abgelehnt. Bisher hat die App nur reagiert, wenn `select()` oder `accept()` einen Fehler lieferten. Jetzt macht die Schleife in `src/http.c` alle zehn Sekunden Leerlauf einen Verbindungsversuch an die eigene Adresse (bei „alle Adressen“ an `127.0.0.1`); „Verbindung abgelehnt“ gibt den Zugang zurück, die Schleife in `main.c` öffnet einen neuen und das Protokoll sagt `http_listener_refused`. Ein Zeitüberschreiten oder ein anderer Fehler gilt nicht als abgelehnt.

## Einstellungen

Alles wird in `/data/PS5-Cooling-Center/config.json` gespeichert und beim
nächsten Start automatisch wieder verwendet. Läuft der Payload nicht, greift
nichts davon — dann regelt die PS5 den Lüfter mit ihrer eigenen Kennlinie.
Eine Konfiguration der Vorgängerversion wird beim ersten Start einmalig
übernommen.

### Einstellungen sichern und übertragen

Hinweis: Zusätzlich zur kompletten Konfigurationsdatei bietet die Weboberfläche
einen separaten Web-Profil-Export/Import an (Dateiformat
`ps5tm-web-profile-v1`). Dieses Profil enthält eine praxisnahe Teilmenge aus
Kühl-Parametern plus UI-Präferenzen (z. B. Monitoring-Alarme,
Expertenmodus-Schalter) und ist für schnelle Profilwechsel gedacht.

## Die Systemseite im Detail

Auf der Systemseite gibt es zusätzlich:

- **Seriennummer (seit 21.11.2026)**: Die Zeile „Seriennummer“ zeigt ab Werk nur die letzten vier Stellen (`••••8023`). Der Knopf
  **Vollständig** in der Zeile zeigt die ganze Nummer, **Gekürzt** schaltet zurück; in der Vollbild-App schaltet die X-Taste auf
  der Zeile um. Die Wahl wird auf der Konsole gespeichert (Einstellung `serial_full`) und gilt für Browser und Vollbild-App. Die
  App hat keine Anmeldung: Ist die ganze Nummer eingeschaltet, kann jedes Gerät im Heimnetz sie abrufen; darum ist sie ab Werk
  gekürzt.
- **USB-Laufwerk auswerfen (seit 29.10.2026)**: Unter „Speicher“ steht bei jedem USB-Laufwerk (`/mnt/usb0` bis `usb7`,
  `/mnt/ext0`) der Knopf **Auswerfen**; in der Vollbild-App unter System → Speicher die X-Taste. Ausgeworfen wird über
  ShadowMountPlus (ab 1.7beta10), aber nur, wenn kein Auftrag der App läuft (Kopieren, Konvertieren, Verschieben, Löschen,
  Dateimanager, Spielstände, Pakete installieren oder teilen, AMPR-Index, Disc-Abbild), PS4Mount keine Spiele von diesem
  Laufwerk spielt und die App selbst nichts auf dem Laufwerk offen hat (andere Programme nur, wo die Konsole die Abfrage zulässt: auf der Pro mit Firmware 12.00 nicht). Sonst wirft die App nicht aus und sagt, was noch läuft.
  **Warum so vorsichtig (Stand 21.11.2026):** Die Konsole trennt das Laufwerk mit einem erzwungenen Unmount (`unmount(Pfad, MNT_FORCE)`) und fragt
  niemanden, ob noch etwas darauf offen ist; sie prüft nur, dass das Gerät bekannt ist und ein Dateisystem eingehängt ist (belegt in jeder ShellCore-Fassung
  von 3.20 bis 12.70, auch 9.40 und 12.00). Was dann noch auf dem Laufwerk liest oder offen hat, wird mitgerissen. ShadowMountPlus und Kraken Mount schützen
  davor nicht, sie lassen den Aufruf der Konsole zu. Darum macht die App vor dem Anfragen (`src/usbeject.c`): (1) sie beansprucht das Laufwerk, sodass
  nichts Neues davon liest, und **wartet bis zu 4 s, bis jeder Leser, der schon dabei war, fertig ist** (das Laufwerks-Tor `src/drivegate.c`: der
  5-Sekunden-Takt von System, die Orte des Dateimanagers, die Bibliothek, die Payload-Seite, das Paket-Symbol); (2) sie prüft, **welche Dateien auf dem Laufwerk offen sind** (`src/driveholders.c`): die der App selbst auf jeder Firmware (`fstatfs` auf jede Dateinummer), die
  anderer Prozesse mit `sysctl kern.proc.filedesc` (ShadowMountPlus, kstuff und die Dienste der Konsole zählen nicht), aber **auf der Pro mit Firmware 12.00 antwortet die Konsole darauf
  mit „nicht vorhanden“**, dort ist es also „nicht bekannt“ und das Auswerfen geht weiter; sie lehnt mit dem Namen des Programms ab; (3) `sync()`, 0,3 s Pause, das Tor noch einmal; (4) ein Zustandsbericht (`usb_eject_state`, `usb_eject_holder`) in
  `app.log`, sofort auf die Platte geschrieben, damit nach einem Absturz etwas da ist; erst dann fragt sie ShadowMountPlus. Fragt man die App, wer ein
  Laufwerk offen hat: `GET /api/v1/storage/holders?mount=/mnt/usb0`. Die Liste von ShadowMountPlus selbst fragt die App währenddessen nicht neu ab (das
  wäre ein `stat()` jedes Spiels). Danach meldet die Konsole „… ist ausgeworfen und
  kann abgezogen werden“ oder den Grund, aus dem ShadowMountPlus abgelehnt hat (etwa ein laufendes Spiel von diesem
  Laufwerk). **Wichtig:** Der Auswerfen-Knopf auf der Seite von ShadowMountPlus selbst kennt die Aufträge dieser App nicht
  und lässt sich von ihr nicht aufhalten; während eines Auftrags nur über diese App auswerfen. Im Dateimanager wechselt die Seite dabei zuerst auf
  System → Speicherplatz (so, dass die Dateimanager-Seite mit ihren Abfragen nicht mehr offen ist) und wirft dort aus; die Vollbild-App fragt auf der Seite Dateien
  nichts mehr ab, sobald das Auswerfen angefragt ist. Was der Dateimanager gerade noch liest (Ordnerliste, Größe, Ansicht, Download),
  hält das Auswerfen zurück, und für dieses Laufwerk antwortet er bis 30 s nach dem Auswerfen mit „Das Laufwerk wird gerade ausgeworfen“.
  Das Auswerfen trennt das Laufwerk nur per Software: Manche Laufwerke meldet die Konsole nach einigen Sekunden von selbst
  wieder an und bindet sie neu ein, andere erst nach erneutem Einstecken; nach „ausgeworfen“ also abziehen. **Gesperrt nach einem Absturz:** Kurz vor der Anfrage an ShadowMountPlus schreibt die App eine
  Markierung (`usb_eject_offen.txt` im Datenordner der App) und löscht sie 30 s nach dem Ende. Findet sie die Markierung beim nächsten Start, ist die Konsole oder
  die App dabei oder kurz danach abgestürzt; dann bleibt das Auswerfen gesperrt (`ejectable` ist nirgends mehr wahr, `blocked` steht in `GET /api/v1/storage/eject`,
  ein Hinweis mit „Auswerfen wieder erlauben“ erscheint im Browser unter „Speicherplatz“ und auf der Seite „Dateien“; `POST /api/v1/storage/eject` mit
  `{"action":"rearm"}` hebt es auf). Ein planmäßiges Beenden der App löscht die Markierung nicht, wenn die Sperre schon bestand. Wer ganz sicher gehen will: die Konsole
  ganz ausschalten und das Laufwerk erst danach abziehen.
- **Am Handy öffnen (ab 1.52.0)**: ein QR-Code mit der Adresse dieser Seite (`http://<Adresse der Konsole>:<Port>/`). Die Kamera des Handys
  darauf halten, und die Seite geht auf; das Handy muss im selben Netzwerk sein. Der Code enthält nur die Adresse, keine Zugangsdaten und
  keinen Text, den jemand anders bestimmen könnte (die Konsole erzeugt ihn nur für ihre eigene Adresse). Der Encoder (Byte-Modus, Stufe M,
  Version 1 bis 6) stammt aus webhb von slopmaster33 (GPL-3.0); er ist mit einem Lesegerät (OpenCV) für alle Versionen und die Grenzen geprüft.
- **Grafikdaten**: zeigt verifizierbare GPU-Stellvertreterwerte (z. B.
   Grafiktakt und GPU-Seitentabellen), und kennzeichnet explizit, dass diese
   Firmware keine direkte GPU-Temperatur/GPU-Auslastung liefert.
- **Rohsensoren (SoC-Kanäle)**: letzte gültige Messung je Kanal aus den
   8 Sensorlinien (`/api/v1/channels`) für technische Einordnung.
  Im Expertenmodus erscheinen zusätzlich bewusst als unbestätigt markierte
  Heuristik-Hinweise (derzeit für Kanal 1/2/5 sowie Lastdynamik).
  Mit dem Schalter **Nur bestätigte Rohsensorwerte** lassen sich diese
  Heuristik-Hinweise ausblenden; die Rohkanalwerte bleiben dabei sichtbar.
- **Live-Status und Warnungen**: kompakte Sofortsicht auf thermische Warnungen,
   Sicherheitsmodus, Adapterstatus und aktuelle Lüfter-Soll/Ist-Werte,
  inklusive Ampelstatus mit festen Schwellen:
   Grün = keine Warnungen; Gelb = mindestens Ziel + 5 °C oder Adapter fehlt;
   Rot = Warnflag gesetzt oder Sicherheitsmodus aktiv.
    Der konkrete Auslöser wird als **Ampelgrund** in Klartext angezeigt,
    priorisiert und mehrzeilig (Hauptauslöser zuerst, sekundäre Gründe danach).
    Zusätzlich markieren farbige Text-Badges (OK/INFO/GELB/ROT) die
   Dringlichkeit direkt in der Zeile.
    Darunter zeigt **Letzte Zustandswechsel** die jüngsten Wechsel von
    Ampelstatus und Lüfterdiagnose als kurze Timeline.
    Jede Zeile trägt zusätzlich ein Quellsymbol (Ampel/Lüfter), damit
   die Herkunft des Wechsels sofort erkennbar ist.
    Zusätzlich wird die Wechselrichtung angezeigt (z. B. OK→GELB oder
   GELB→ROT), um Eskalation oder Entspannung sofort zu sehen.
    Die Diff-Kapsel ist farblich codiert: Eskalation rot, Entspannung grün,
   gleichbleibend neutral.
    Eine kompakte Statistikzeile zählt Wechsel nach ROT/GELB/OK in einem
    wählbaren Fenster (2/5/10 Minuten), damit die aktuelle Gesamtlage ohne
   Lesen jeder Einzelzeile sichtbar bleibt.
   Die Ereignisliste darunter folgt demselben Zeitfenster, damit
   Detailansicht und Statistik konsistent bleiben.
   Beim Umschalten des Fensters wird die Liste kurz eingeblendet, damit der
   Wechsel visuell sofort erkennbar ist (mit Rücksicht auf Reduced Motion).
   Die einzelnen Zeilen erscheinen dabei leicht gestaffelt, um die neue
   Reihenfolge schneller erfassbar zu machen.
   Die gewählte Fensterlänge bleibt lokal im Browser gespeichert und
   wird nach einem Neuladen der Seite wiederhergestellt.
   Ein Zähler und der Knopf **Zurücksetzen** erlauben, Diagnoseläufe
   gezielt neu zu starten und sauber zu vergleichen.
- **Lüfterdiagnose (Soll / Ist / Clamp)** (auf der Seite Kühlung, Expertenansicht): zeigt effektives Regelziel,
  aktive Spielregel, Soll-Ist-Abweichung, Rohwert und erkennbare Clamp-Lage
  (unterer/oberer Anschlag oder Regelbereich) sowie einen 60-Sekunden-
   Mini-Trend für Soll- und Ist-Drehzahl.
   Darunter wird ein dynamischer Diagnosehinweis mit Handlungsempfehlung
   angezeigt (ok/warn/hot), abgeleitet aus Adapterstatus, Sicherheitsmodus,
   Soll-Ist-Abweichung und Trenddrift.
   Die Hinweise enthalten eine priorisierte Erstmaßnahme plus optionale
   Folgeschritte (z. B. warten, Zieltemperatur in kleinen Schritten anpassen,
   aktive Spielregel prüfen, Payload neu laden) und werden nummeriert
   ausgegeben (1., 2., 3. ...) sowie mehrzeilig dargestellt.
   Wie beim Ampelgrund werden farbige Badges genutzt, damit Status und
   Schritt-Priorität schneller erfassbar sind.
   Eine kompakte Legende unter der Karte erklärt: OK = stabil,
   GELB = beobachten/nachregeln, ROT = sofort handeln.
- **Rohkanal-Expertenblock**: im Rohsensorbereich wird heuristisch ausgewertet,
  welcher SoC-Kanal bei erkannten Lastspitzen am häufigsten zuerst anzieht.
  Das ist eine technische Hilfsauswertung ohne offizielles Hardware-Label.
- **Warn-Countdown und Integrationsstatus**: sobald die wärmste gültige
   Temperatur eine Warnschwelle erreicht, startet ein Countdown
   (`warning_countdown_s`). Die verbleibende Zeit wird in der Live-Karte
   sichtbar angezeigt, damit Eskalationen früh erkennbar sind.
- **Prospero Lights (Best Effort)**: die Konfiguration enthält Schalter und
   Schwellwerte für einen Lightbar-Ampelstatus (`lightbar_enabled`,
   `lightbar_warn_c`, `lightbar_hot_c`). Der abgeleitete Status (OK/GELB/ROT)
   wird in `status.control.lightbar` veröffentlicht und im UI angezeigt.
   Die Ansteuerung wird im Hintergrund-Probe-Thread ausgeführt, damit
   potenziell blockierende Pad-Aufrufe die Lüfterregelung nicht ausbremsen.
   Hinweis: je nach Firmware/Permissions kann die echte Controller-Ansteuerung
   aus einem Payload-Kontext eingeschränkt sein.
- **Spielprofile Export/Import** (auf der Seite „Profil“): vorhandene Titelprofile lassen sich als JSON
   exportieren und wieder importieren (Backup/Transfer zwischen Konsolen).
- **Live-Protokoll (Tail)**: zusätzlich zum normalen Protokoll kann ein
   kompaktes Live-Fenster die letzten Einträge zyklisch nachladen.

Hinweis: Für Region/Sprache/Zeitzone werden absichtlich Rohcodes gezeigt
(`SYSTEM_language`, `DATE_time_zone`, `DATE_timezone_offset`,
`SECURITY_PARENTAL_game_age_limit_region`), weil die textliche Zuordnung je
nach Firmware/Region variieren kann.
