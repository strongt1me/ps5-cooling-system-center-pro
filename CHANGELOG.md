# Changelog

## Unveröffentlicht

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
