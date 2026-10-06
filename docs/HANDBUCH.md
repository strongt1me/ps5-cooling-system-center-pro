# Handbuch

[README](../README.md) · [Handbuch](HANDBUCH.md) · [API](API.md) · [Entwicklung](ENTWICKLUNG.md)

Ausführliche Bedienung von PS5 Cooling & System Center - Pro. Die Kurzfassung
steht in der [README](../README.md); die Schnittstelle der App in [API.md](API.md).

## Die Seiten der Oberfläche

Nach dem Start: Browser auf **`http://<PS5-IP>:8086`**.

Der Port liegt fest auf 8086, weil die Startmenü-Kachel diese Adresse als
Deeplink enthält. Frühere Fassungen nutzten 8080 und 8770; alte
Konfigurationen ziehen beim ersten Start automatisch um.

- **Profil** — Anzeigename und Profilbild der Konsole (eigenes Bild oder eines
   der 30 eingebauten)
- **Spiele** — die Spiele des Startbildschirms: starten, kopieren, verschieben,
   konvertieren; ein zweiter Reiter zeigt die **Spielzeit**, ein dritter sichert die
   **Spielstände**, ein vierter listet die **Pakete** auf Sticks und Laufwerken, teilt
   große auf und installiert sie (alles siehe unten)
- **Payloads** — laufende, gespeicherte und auf USB liegende Payloads: starten,
   beenden, kopieren (siehe unten)
- **Kühlung** — Klartext-Status, wärmster Punkt, Lüfter und laufendes Spiel, gleich
   darunter Zieltemperatur mit Schnellwahl und Betriebsart (leise/ausgewogen/kühl),
   dann Temperaturverlauf, Sensoren und Spielprofile. Bis 1.45.x stand ein Teil davon auf
   einer eigenen Seite „Übersicht“; sie ist hier aufgegangen, ohne dass dieselbe
   Zahl zweimal erscheint. Im Expertenmodus kommen Verläufe, Rohwerte,
   Regelzustand, Diagnose und erweiterte Parameter dazu.
- **System** — Konsole, Firmware, Laufzeit, Speicherplatz, Netzwerk, Kachel,
   Bildschirmanzeige, Zusatzabfragen, Experte-Modus mit Rohdiagnose
- **Protokoll** — Ereignisse der App, als `.log` exportierbar, und das
   Kernel-Log live (siehe unten)

Darunter öffnen **Handbuch** und **FAQ** (seit 1.49.1) das eingebaute Benutzerhandbuch und die häufigen Fragen,
am PC in einem neuen Tab; oben steht „← Zurück zur App“. Die eingebauten Fassungen haben keine Bildschirmfotos
(die Fassungen mit Bildern gibt es als PDF und HTML zum Release).

In der **Kopfzeile**, auf jeder Seite, stehen die **Power-Optionen**: vier Knöpfe, mit denen
sich die Konsole steuern lässt. Ein gemeinsamer Rahmen zeigt, dass sie zusammengehören; sie sitzen
zwischen dem Seitentitel und den Schaltern „Vollbild“, „Expertenmodus“ und der
Verbindungsanzeige: **Ruhemodus**, **Neu starten**, **Ausschalten** und
**Abgesicherter Modus** (bis 1.47.0 eine Karte auf der Systemseite). Jeder Befehl
unterbricht, was gerade läuft, und verlangt deshalb zwei Klicks auf denselben Knopf: Der
erste färbt ihn gelb („Wirklich? Nochmal klicken“) und nennt die Folge (nicht gespeicherte
Spielstände gehen beim Ausschalten und beim Neustart verloren; der abgesicherte Modus ist ein
Menü beim Hochfahren, von dort führt „PS5 neu starten“ ganz normal zurück). Ein zweiter Klick
in der ersten knappen Sekunde zählt nicht, damit ein Doppelklick nichts auslöst; nach zehn
Sekunden ohne zweiten Klick ist der Knopf wieder gewöhnlich.

## Die Seite „Spiele“

**Woher die Liste kommt.** Aus der App-Datenbank der Konsole
(`/system_data/priv/mms/app.db`): die sichtbaren Kacheln der Spiele-Reihe für
den angemeldeten Benutzer, mit Spielzeit und „zuletzt gespielt“, so wie die PS5
sie zählt. Gesucht wird nach Titel, Titel-ID oder Content-ID; gefiltert nach
Alle, PS5, PS4 und „Backport · AMPR · PlayGo“; sortiert wie auf dem
Startbildschirm, nach Name, zuletzt gespielt, Spielzeit, Größe oder
installiert.

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
Hinweis des Knopfs.

**Backport, AMPR EMU und PlayGo** liest die App aus dem Spielordner eingehängter
PS5-Spiele: Ersatzbibliotheken im Ordner `fakelib` (`libSceAmpr.sprx`,
`libScePlayGo.sprx`, weitere) und die SDK-Version der `eboot.bin`, die ein
Backport unter die aus `param.json` absenkt. Spiele, die ShadowMountPlus als
Abbild einhängt, lassen sich nur prüfen, solange das Abbild eingehängt ist.

Jedes Spiel bringt diese Dateien selbst mit, im eigenen `fakelib`-Ordner. Die App
**zeigt sie nur an** und baut oder lädt davon nichts hoch:

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

**Covers & Metadaten speichern.** Rechts neben den Reitern der Seite steht ein Schalter dieses
Namens, ab Werk aus. Eingeschaltet legt die App auf der Konsole ab, was sie beim Lesen der Liste
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
Neben dem Schalter steht, wie viel im Ordner liegt, und „Ordner leeren“ (zwei Klicks) räumt
ihn auf: Gelöscht wird nur, was die App selbst angelegt hat (Ordner mit einer Titel-ID als Namen
und darin `meta.json`, `icon0.png` und deren Zwischendateien), nie etwas anderes, und nie ein Spiel.
Ausschalten löscht nichts; die Dateien bleiben liegen, werden dann aber nicht benutzt. Ein Bild über
6 MB wird nicht kopiert, und alle Bilder zusammen bleiben unter 160 MB.

## Spiele starten und beenden

„Starten“ startet das Spiel direkt; läuft es schon im Hintergrund, holt die
App es nach vorn. Im Browser der Konsole schließt sich der Browser dabei, damit
das Spiel vorn ist.

- **Läuft schon ein anderes Spiel**, auch pausiert im Hintergrund, startet die
  App nichts. Sie zeigt fünf Sekunden lang einen Hinweis ohne Knopf (ein Tipp
  schließt ihn früher): „Tetris® Ultimate läuft noch. Bevor Assassin's Creed
  Shadows starten kann, beende Tetris® Ultimate zuerst mit „Spiel beenden“ …“,
  mit der Warnung, dass das Spiel sofort geschlossen wird und vorher nicht
  speichert. Beendet wird dann über „Spiel beenden“, gestartet danach mit einem
  eigenen Tipp auf „Starten“, nie im selben Zug (Wunsch vom 03.10.2026). Die
  Meldung nach dem Beenden nennt das Spiel, das starten sollte. „Beendet“ meldet
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
  innerhalb von 30 Sekunden startet deshalb nichts und bietet auch keine zweite
  Meldung an.

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

**Verschieben** erledigt ShadowMountPlus selbst, damit es weiß, wo das Spiel danach
liegt; zur Wahl stehen nur Ordner, in denen es sucht. Ein Abbild zurück zu einem
Ordner entpackt ebenfalls ShadowMountPlus.

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
- **`.ffpfsc`** (exFAT innen, komprimiert): der Aufbau, den MkPFS und
  ShadowMountPlus empfehlen. Auch ein exFAT-, ffpkg- oder ffpfs-Abbild lässt sich
  so zu `.ffpfsc` machen.

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

**Prüfsummen-Datei.** Neben dem Ergebnis entsteht `<Name>.sha256` mit dem SHA-256
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
Konvertierung kann eine leere `….ps5cc-konv-teil`-Datei liegen bleiben, weil die Platte beim
Aufräumen gerade fehlte; der nächste Versuch entfernt sie.

## Dateien: der Dateimanager (seit 06.10.2026)

Die Seite **Dateien** zeigt die Ordner der Konsole. Oben stehen die Orte (Interne SSD `/data`, jedes angeschlossene
Laufwerk, „Ganzes System (nur ansehen)“), darunter der Pfad zum Anklicken und die Liste: Ordner zuerst, mit Größe und
Datum. Ein Ordnername öffnet ihn, **↑ Hoch** geht eine Ebene zurück.

**Ansehen und Herunterladen gehen überall.** Jede Datei hat **Herunterladen** (im Browser am PC; Gerätedateien wie unter
`/dev` werden nicht ausgeliefert).

**Ändern geht nur auf den Laufwerken und in `/data`** (USB, M.2, interne SSD): **Neuer Ordner**, **Hochladen** (eine oder
mehrere Dateien, mit Fortschrittsbalken in Prozent; am PC auch per Ziehen auf die Liste), **Umbenennen**, **Kopieren**,
**Ausschneiden** (Verschieben) und **Löschen**. Überall sonst steht „Nur ansehen“, und die Knöpfe sind aus. Das prüft die
Konsole selbst, nicht nur die Seite. Zusätzlich geschützt:

- der Ordner der App `/data/PS5-Cooling-Center` (Einstellungen, Protokolle) – nur sein Unterordner `payloads` darf
  verändert werden, damit man Payloads für die Seite „Payloads“ hochladen kann;
- Verknüpfungen: Sie werden nie verfolgt, wenn etwas geändert wird, und lassen sich weder umbenennen noch löschen;
- Laufwerke, die in `/data` eingehängt sind, und die Laufwerke selbst (`/mnt/usb0`, `/data`): nur ihr Inhalt.

**Kopieren und Verschieben:** Einträge ankreuzen, **Kopieren** oder **Ausschneiden**, in den Zielordner wechseln, **Hier
einfügen**. Ein Balken zeigt Prozent, Bytes und die aktuelle Datei; **Abbrechen** hält an und entfernt im Ziel, was vom
angefangenen Eintrag schon geschrieben war. Nichts wird überschrieben: Gibt es einen Namen im Ziel schon, beginnt nichts,
und die Meldung nennt ihn. Verschieben innerhalb eines Laufwerks ist ein Umbenennen; zwischen zwei Laufwerken wird
kopiert, und das Original erst gelöscht, wenn die Kopie vollständig ist. Vorher prüft die Konsole den freien Platz.

**Löschen** fragt wie bei den Spielen: Die Konsole nennt, was weggeht (Einträge, Dateien, Größe), dann ein Häkchen
„endgültig“ und zwei Klicks auf **Endgültig löschen**. Ein Balken zeigt den Fortschritt.

Während Kopieren, Verschieben oder Löschen läuft, startet kein zweiter solcher Auftrag; der Dateimanager startet nicht,
solange ein anderer Vorgang der App läuft (Spiel kopieren, konvertieren, installieren …). Jede Änderung steht im Protokoll.

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
- **Sicherungen der App:** die Datei bzw. der Ordner und die Prüfsummen-Datei daneben.

**Die Spielstände bleiben immer erhalten.** Keiner dieser Wege fasst sie an.

**Bestätigung:** Der Dialog zeigt genau, was gelöscht wird (Name, Art, Ort, Größe). Der rote Knopf **Endgültig
löschen** ist erst nach dem Häkchen „Ich verstehe: Das Löschen ist endgültig“ bedienbar, und er braucht zwei Klicks:
Der erste färbt ihn („Wirklich? Nochmal klicken“), der zweite löscht. Ein Doppelklick zählt nicht, nach zehn Sekunden
ohne zweiten Klick ist er wieder gewöhnlich. Die Konsole löscht nur mit einem Kennwort, das sie zum gezeigten Plan
ausgegeben hat (einmal gültig, zehn Minuten lang, nur für genau dieses Spiel oder diese Sicherung).

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
  und Calc). „Verlauf löschen“ verlangt zwei Klicks und löscht die gespeicherten
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
  „Zurückspielen“; er verlangt zwei Klicks. Die App geht dann so vor:
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
[README](../README.md#credits-und-danksagung)): finden, aufteilen und [installieren](#pakete-installieren).

- **Wo gesucht wird.** Auf jedem Laufwerk unter `/mnt` im Hauptordner (dort nur die Dateien, die
  direkt darin liegen) und im Ordner `pkg` mit seinen Unterordnern, bis zu drei Ebenen tief
  (`pkg` oder `PKG`, die Schreibweise ist gleich); im Konsolenspeicher nur im Ordner
  `/data/pkg`. Andere Ordner werden nicht durchsucht, Verknüpfungen nicht verfolgt. Dateien unter 4 KB
  zählen nicht, und aus einer Suche bleiben höchstens 512 Pakete.
- **Was gelesen wird.** Von jedem Paket nur ein paar Stellen im Kopf, einige Kilobyte einer Datei, die
  zig Gigabyte groß sein kann: Titel-ID, Name (der deutsche Titel zuerst, sonst die Standardsprache
  des Pakets), Version, Content-ID, die Art (Spiel, Update, Zusatzinhalt), PS4 oder PS5 und das Bild des
  Pakets. Es wird nichts geschrieben, und kein Paket wird ganz gelesen. Eine Datei, die kein Paket ist
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

An den Karten des Reiters **Pakete** steht der Knopf **„Installieren …“** (bei einem großen, noch nicht geteilten Paket neben „Aufteilen …“).
Er fehlt bei einem geteilten Paket, dem Teile fehlen, und er verschwindet, solange ein Vorgang dieser Seite läuft (Aufteilen oder eine Installation). Die Konsole
installiert das Paket selbst, mit der Installation, die auch das Menü benutzt (die Systembibliothek AppInstUtil). Die App
stellt es ihr nur bereit und zeigt den Fortschritt.

- **Erst die Prüfung, dann der Klick.** „Installieren …“ klappt unter der Karte einen Plan auf, den die Konsole für genau dieses
  Paket erstellt: Name, Art (Spiel, Update, Zusatzinhalt), Version, PS4 oder PS5, Größe (bei einem geteilten Paket die des
  ganzen Pakets und die Zahl der Teile), der freie Platz im internen Speicher und, wenn es schon installiert ist, welche Version.
  Rot steht, was die Installation verhindert, gelb, was man wissen sollte; „Jetzt installieren“ ist nur dann zu drücken, wenn nichts
  verhindert. Die Konsole prüft beim Start noch einmal selbst alles nach; der Plan zeigt es nur vorher.
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
  wird nur gelesen). Eine Installation beginnt nur mit dem Klick auf „Jetzt installieren“, und wenn die Konsole sie ablehnt, gibt es keinen
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
  die gespeicherten Auszüge und Aufnahmen nach zwei Klicks; eine Aufnahme, die gerade läuft, bleibt.
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

Die App ist mit dem PS5-Payload-SDK **v0.43** gebaut. Dessen Startcode kennt die
Firmware **bis einschließlich 13.60**; v0.41, mit dem die Versionen bis 1.46.0
gebaut waren, endete bei 13.40. Eine Firmware, die der Startcode nicht kennt,
kommt nicht bis `main()`: Das Programm startet dann nicht und schreibt nichts ins
Protokoll. Getestet ist nur die **Firmware 12.00** auf einer PS5 Pro; ob
Lüftersteuerung und Sensoren auf 13.xx so arbeiten wie dort, ist nicht geprüft.
Was die Konsole meldet, steht auf der Systemseite („Firmware“) und im Protokoll
(`firmware`).

## Payloads starten (ab 1.46.0)

Die Seite „Payloads“ zeigt drei Listen: was gerade läuft (mit „Beenden“), was im
Ordner `/data/PS5-Cooling-Center/payloads` liegt, und die `.elf`-Dateien auf
angesteckten USB-Sticks — im Hauptverzeichnis des Sticks und im Ordner `payloads`
darin. „Starten“ schickt die Datei an den Payload-Lader der Konsole (elfldr,
Port 9021), genau das, was sonst vom PC aus geschieht, nur ohne PC. Von einem
Stick lässt sich eine Datei außerdem in den internen Ordner kopieren (das
Original bleibt); aus dem internen Ordner lässt sie sich löschen (zwei Klicks).

- Der Ordner wird beim Start der App angelegt, **wenn es ihn nicht gibt**. Ein
  vorhandener bleibt unberührt: kein erneutes Anlegen, keine geänderten Rechte.
- Die Seite nennt der Konsole nie einen Pfad, nur Ort, Ordner und Dateiname aus
  deren eigener Liste; geprüft wird dort noch einmal. Gestartet wird nur, was
  auf `.elf` endet, eine gewöhnliche Datei ist (kein Link) und mit dem Kopf eines
  64-Bit-x86-64-ELF beginnt. Andere Dateien erscheinen markiert und bleiben
  gesperrt.
- Ein Start nach dem anderen: Ein zweiter Klick während eines laufenden Starts
  oder kurz nach dem ersten auf dieselbe Datei wird abgewiesen.
- Startet ein Payload diese App selbst neu, reißt die Verbindung kurz ab; die
  Seite findet die neue Ausführung von allein wieder.

## Betriebsarten

| Modus | Verhalten |
| --- | --- |
| `automatic` | Komfortregelung: hält die Zieltemperatur, sanft und träge. |
| `observe` | Nur Beobachtung. Der Lüfter wird nur angefasst, wenn zuvor per „Anwenden" eine feste Schwelle gesetzt wurde. |

## Komfortregelung

Das Ziel ist **nicht** die niedrigste Temperatur, sondern ein ruhiger,
unauffälliger Lüfter — angelehnt an die Temperaturregelung von Xbox 360
DashLaunch.

| Parameter | Standard | Bedeutung |
| --- | --- | --- |
| `target_temp_c` | 66 °C | zu haltende Temperatur (60–78, bis 72 empfohlen) |
| `deadband_c` | 1 °C | innerhalb passiert nichts |
| `control_interval_s` | 5 s | Messung bleibt bei 1 s |
| `average_window_s` | 12 s | gleitender Mittelwert |
| `max_step_pct` | 1 % | Drehzahländerung pro Zyklus |
| `safety_temp_c` | 78 °C | darüber zählt nur noch die Hardware |
| `profile` | comfort | comfort / balanced / cool |

Wie es arbeitet:

- Die Regelung nutzt **nie** den Rohwert, sondern den gleitenden Mittelwert.
- Innerhalb der Totzone (65–67 °C) passiert **gar nichts**.
- Der **Trend** zählt mehr als der Einzelwert: Fällt die Temperatur bereits,
  wird nicht weiter hochgeregelt.
- Hochregeln in 1-%-Schritten; Runterregeln nur 1 % und erst nach 20 s
  stabil niedriger Temperatur — bei deutlich untertemperierter Konsole
  entsprechend schneller.
- Direkter Eingriff ist weiter möglich: der Direktwert-Block bietet Presets
   (`Kühl`, `Ausgewogen`, `Leise`) und setzt je nach Modus entweder die
   Zieltemperatur (`automatic`: 62, 70 und 78 °C; bis 03.10.2026 waren es 62, 66 und
   70 °C, als der Regler noch nur bis 72 °C ging) oder eine feste Schwelle (`observe`:
   58, 65 und 74 °C).
- Netzbindung ist als Option verfügbar: `bind_address` kann in der UI direkt
   auf `0.0.0.0` (LAN) oder `127.0.0.1` (nur lokal auf der Konsole) gesetzt
   und gespeichert werden.
- Experte-Modus zeigt zusätzlich Rohfelder aus der Quelle, u. a.
   `firmware_raw`, `firmware_group`, `cpu_mode` (roh) und erweiterte
   Controller-Rohdaten.
- **Komfortgrenze** (65 % im Komfortprofil): verhindert, dass ein Thermostat,
  der sein Ziel unter Dauerlast nie erreicht, bis auf 100 % hochdreht.
- Ab `safety_temp_c` werden Totzone, Mittelwert und Komfortgrenze ignoriert.
- **Ziele über 72 °C** (seit dem 03.10.2026 bis 78 °C einstellbar): Die Konsole läuft
  dann deutlich wärmer, der Lüfter bleibt länger auf seiner leisen Grundkurve. Die
  Notfallgrenze rückt dabei von selbst auf Ziel + 4 °C (bei 78 °C auf 82 °C), sonst
  würde die Regelung mitten in ihrer Arbeit in den Notfallmodus kippen. Die
  Warnschwelle der App (`warning_cpu_c`, 80 °C ab Werk) bleibt, wo sie ist, und liegt
  bei hohen Zielen nah über der gehaltenen Temperatur: Wer dort dauernd eine
  Temperaturwarnung bekommt, hebt sie in den Einstellungen an. Die Firmware der
  Konsole schützt sich unabhängig davon weiter selbst.

### Wichtig: Schwelle statt Drehzahl

Der ICC-Controller kennt **keine** direkte Drehzahlvorgabe. Der einzige
Stellhebel ist eine **Schwellentemperatur** — sie wirkt wie ein Sollwert: Liegt
sie über der Temperatur, bleibt der Lüfter auf seiner leisen Grundkurve; liegt
sie darunter, dreht die Firmware proportional zum Abstand auf.

Ein Grad Schwelle entspricht etwa 5 % Drehzahl. Feine 1-%-Schritte sind damit
nicht direkt möglich, deshalb arbeitet die App zweistufig: Der Komfortregler
rechnet intern in Prozent, ein nachgelagerter Servo überträgt das auf die
Schwelle und greift erst ab 4 % Abweichung ein. Er liest die **tatsächliche**
Drehzahl zurück und kalibriert sich damit selbst.

Die Schwelle wird mindestens alle 15 Sekunden neu gesetzt, weil die Firmware
den ICC-Zustand bei jedem Spiel-/App-Start zurücksetzt.

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

Offen heißt nicht „für jede Webseite". Eine Seite, die im Browser eines
Geräts im Heimnetz geöffnet ist, kann dessen Verbindungen zur Konsole
mitbenutzen — ohne Kennwort hätte sie damit ausschalten, Spiele verschieben,
Zusatzprogramme beenden oder die Einstellungen ändern können. Seit 1.46.0
weist der Server deshalb ab, was erkennbar von einer fremden Webseite kommt:

- Der `Host` einer Anfrage muss eine IP-Adresse, `localhost`, ein einzelner
  Rechnername ohne Punkt oder ein Name mit üblicher Heimnetz-Endung sein
  (`.local`, `.lan`, `.home`, `.home.arpa`, `.internal`, `.localdomain`,
  `.intranet`, `.fritz.box`). Das schließt Umlenkungstricks über DNS aus.
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

## Profilbilder und Avatar-Pakete

- Auf der Seite „Profil“ stehen **30 fertige Profilbilder** zur Auswahl. Sie
   stecken in der App selbst (`web/avatars/`, 440 × 440 JPEG, gebaut mit
   `tools/build_avatars.py` aus den Originalen), man braucht also weder eine
   Datei noch einen PC: Bild antippen, Vorschau ansehen, „Profilbild
   übernehmen“. Sonst läuft alles wie bei einem eigenen Bild.
- Bild auswählen, dann optional als Paket im Ordner
   `/data/PS5-Cooling-Center/Avatars` speichern.
- Später ein gespeichertes Paket laden und mit „Profilbild übernehmen“
   aktivieren, ohne das Bild neu zu konvertieren.

## Homescreen-Kachel

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
  getan; sie kehrt nicht bei jedem Start zurück. Auf der Systemseite legt „Kachel
  installieren“ sie jederzeit wieder an. Eine neue Version versucht es nach einem
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

Das Paket (`IV9999-PSCC69690_00-PS5COOLINGCNTR01-A0100-V0100.pkg`, 8,2 MB) liegt der
Veröffentlichung bei und steckt im Installer
`cooling-center-launcher-installer_v<Version>.elf`: einmal senden, er installiert nur
die Kachel und zeigt den nackten Fehlercode. Im Hauptprogramm ist das Paket nicht
eingebaut: Es kostete dort 10,5 MB Speicher, und die Konsole konnte dann keine
Anfrage mehr bedienen (siehe [ENTWICKLUNG.md](ENTWICKLUNG.md#bauen)). Über die
Systemseite lässt sich ein Paket auch hochladen.

Wie die Kachel-Dateien eingebettet und das Paket gebaut werden, steht in
[ENTWICKLUNG.md](ENTWICKLUNG.md#kachel-dateien-und-kachel-paket-bauen).

## Bildschirmanzeige auf der Konsole

Beim Start meldet sich der Payload mit einem System-Hinweis oben rechts —
Version, IP-Adresse und Port der Web-UI. Zusätzlich lässt sich unter
*System → Anzeige auf dem Fernseher* eine wiederkehrende Statusmeldung
einschalten (Temperaturen, Lüfter, IP, freier Speicher; Intervall einstellbar).

Auf Knopfdruck geht es auch: Wer die **Mikrofon-Taste des Controllers zweimal kurz
hintereinander** drückt, sieht oben rechts kurz Prozessortemperatur und
Lüfterdrehzahl — auch ohne geöffnete Web-Oberfläche (Schalter unter
*Kühlung → Anzeige per Mikrofon-Taste*, höchstens alle 5 Sekunden). Das
Mikrofon wird dabei zweimal umgeschaltet und ist danach wieder wie vorher;
ein einzelner Druck löst nichts aus. Ein Payload darf den Controller nicht
lesen, deshalb wertet die App die Zeile aus, die das System für jeden Druck
selbst ins Kernelprotokoll schreibt (`MicMuteKeyPressed`). Bis 1.45.x lag
diese Anzeige auf der PS-Taste.

Ein **dauerhaft eingeblendetes Overlay** wie bei etaHEN ist damit nicht
gemeint und von außen auch nicht möglich: dafür müsste sich die App in den
Prozess des laufenden Spiels einklinken und dessen Renderer hooken. Die
Systembenachrichtigung erscheint dafür zuverlässig über Spielen und Menüs,
ohne irgendetwas zu destabilisieren.

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
- **Lüfterdiagnose (Soll / Ist / Clamp)**: zeigt effektives Regelziel,
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
- **Spielprofile Export/Import**: vorhandene Titelprofile lassen sich als JSON
   exportieren und wieder importieren (Backup/Transfer zwischen Konsolen).
- **Live-Protokoll (Tail)**: zusätzlich zum normalen Protokoll kann ein
   kompaktes Live-Fenster die letzten Einträge zyklisch nachladen.

Hinweis: Für Region/Sprache/Zeitzone werden absichtlich Rohcodes gezeigt
(`SYSTEM_language`, `DATE_time_zone`, `DATE_timezone_offset`,
`SECURITY_PARENTAL_game_age_limit_region`), weil die textliche Zuordnung je
nach Firmware/Region variieren kann.
