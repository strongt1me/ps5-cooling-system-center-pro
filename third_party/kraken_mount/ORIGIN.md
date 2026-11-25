# Kraken Mount

**Version:** v0.4.0 (25.11.2026)
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/kraken-mount (Tag `v0.4.0`, Commit `6e51ef197feb7ad3487dee91e88392a468b38d08`)
**Lizenz:** GPL-3.0-or-later (Lizenztext und Hinweise zu Fremdbausteinen im Quellprojekt: `LICENSE`, `THIRD_PARTY_NOTICES`)
**Datei:** `kraken-mount_v0.4.0.elf` (PS5-ELF, 7 077 216 Bytes), unverändert aus der Veröffentlichung
**SHA-256:** `6c778613b796039230f6690a16e4c04a76069a6b7b6c8030e292f54e904cc401` (gleich `SHA256SUMS` der Veröffentlichung; am 25.11.2026 mit `gh release download v0.4.0` geladen, ELF-Kopf und Kennung `kraken-mount/0.4.0` geprüft)

## Verwendung

Kraken Mount ist ein eigenes Programm (Rust), das Spiele aus Debug-FPKG-`.pkg`-Dateien und Spielordnern (roh, zstd oder LZ4) ohne
Installation auf dem Startbildschirm anmeldet und sie mit ampr-emu-ng startet. Der Reiter „FPKGMount“ der App (Spiele) startet es,
zeigt seine Spiele und bedient es über seine Schnittstelle auf Port 8097 (`src/kraken.c`).

Die fertige ELF wird als zlib-Blob eingebettet (`tools/gen_blob.py`, `gen/kraken_blob.c`) und bei „Kraken Mount starten“ an den
lokalen ELF-Lader (127.0.0.1:9021) gesendet. Eine neuere Fassung lädt die App nur auf Knopfdruck von
`github.com/quer3q/kraken-mount/releases` (HTTPS, Prüfsumme aus `SHA256SUMS` der Veröffentlichung) nach
`/data/PS5-Cooling-Center/kraken/`; sie wird dann statt der eingebauten gestartet.

Kraken Mount legt bei jedem Start eine Kachel (KRKN00001) für seine Seite an (`register.rs`, `ensure_tile`). Die App will keine: Vor dem
Start legt sie `/user/app/KRKN00001` als leere Datei an. Bis v0.3.1 ließ das Programm den Platz ohne seine Markierung
`/data/krakenmount/tile.ok` in Ruhe; ab v0.3.2 gibt es diese Regel nicht mehr, aber sein `create_dir_all(KRKN00001/sce_sys)` scheitert an der
Datei, und es entsteht keine Kachel (nur eine Zeile „not registered“ im Protokoll). Ein leerer Ordner an dieser Stelle wird vorher entfernt
(ab v0.3.2 würde das Programm ihn füllen und anmelden). Eine früher angelegte Kachel entfernt die App mit der Deinstallation der Konsole und
löscht die Markierung (`keep_tile_away` in `src/kraken.c`, `src/tilekeep.c`).

Aus dem Quellcode von Kraken Mount ist nichts in diese App übernommen; die ELF wird nur weitergegeben. Den Quellcode zu dieser Fassung
gibt es unter der oben genannten Adresse (Tag `v0.4.0`).

## Neu in v0.4.0 (25.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.3.2…v0.4.0)

- **Installierte Spiele in der Bibliothek:** Spiele, die auf der Konsole installiert sind (`/user/app` und der erweiterte Speicher), stehen jetzt
  auch in seiner Liste (`GET /api/state`, `kind: "installed"`, neue Felder `installed`, `installed_version`, `retail` je Spiel). Kraken Mount hängt sie
  nie ein, nimmt sie nie vom Startbildschirm und löscht sie nie (`/api/delete`, `/api/hide` lehnen ab: „an installed game: uninstall it from the
  console's menu“). Eine installierte Kopie gewinnt gegen dasselbe Spiel als FPKG oder Ordner (#121). **Die App lässt diese Einträge aus** ihrem Reiter
  FPKGMount, aus der Warnliste für doppelt angemeldete Titel und aus der Prüfung vor dem Löschen heraus (`kr_drop_installed` in `src/kraken.c`); die
  Seite Spiele der App zeigt sie ohnehin.
- **Backports für installierte Spiele:** Ein Backport in `backports/<TITLE_ID>*` (in den Suchordnern, sonst in `/data/homebrew` wie bei ShadowMountPlus)
  gilt jetzt auch für ein installiertes Spiel: Seine Bibliotheken kommen in den Fakelib-Überbau, seine übrigen Dateien werden bei jedem Start über
  NSFS-Umleitungen in das `/app0` des Spiels gelegt (Port von ShadowMountPlus 1.7 `sm_pkg_backport.c`, neu `src/launch/nsfs.rs`); die Installation
  selbst bleibt unverändert. Mit `ampr_installed` aus (Standard) läuft bei einem installierten Spiel die `libSceAmpr.sprx` des Systems.
  **Wichtig für diese App:** Weil die App Kraken Mount neben ShadowMountPlus laufen lässt (siehe „Änderung durch diese App“), könnten beide denselben
  Backport eines installierten Spiels anwenden. Ob sich das stört, ist an der Konsole noch nicht geprüft.
- **Installieren auf die Konsole:** Ein FPKG-Spiel lässt sich auf seiner Seite installieren (`/api/install`, Reiter „Install“ mit Patches und DLC; die
  Konsole lädt die Datei über `http://127.0.0.1:8097/pkg/…`). Solange eine Installation läuft oder wartet, lehnt `POST /api/quit` mit 409 ab („an install
  is running“); die App nennt das beim Ausschalten.
- **Neue Weboberfläche** (einklappbare Seitenleiste, helles und dunkles Design, drei Farbschemata, Sprachwahl; neue Einstellungen `ui_theme`,
  `ui_scheme`, `ui_sidebar_collapsed`, `ui_lang`, dazu `ampr_installed` und `install_delete_source`). Die Einstellungsdatei bleibt verträglich; die App
  schreibt sie weiter nur beim allerersten Start.
- **Titelmusik:** Kacheln behalten ihre Hintergrundmusik nach einem Spiel, und Kraken Mount stellt einen Ton wieder her, den das System später
  verliert (#124, #125; Dank an aTenb).
- **ampr-emu-ng v0.5.1** ist weiter eingebaut. Die Schnittstelle, die `src/kraken.c` benutzt (`version`, `scanpath`, `games` mit `title_id`,
  `title_name`, `path`, `kind`, `status`, `playgo`, `playgo_off`, `rebuild_queued`; `/api/rescan`, `/api/rebuild/<TID>`, `/api/rebuild-all`,
  `/api/playgo/<TID>`, `/api/delete/<TID>`, `/api/quit`), ist sonst unverändert; beide Ein-Byte-Eingriffe greifen unverändert (je 2 Stellen, an der
  ELF dieser Fassung mit `tools/patch_kraken.py` geprüft).

## Neu in v0.3.2 (24.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.3.1…v0.3.2)

- **Selbst-Update:** Bei jedem Start fragt das Programm `api.github.com/repos/quer3q/kraken-mount/releases/latest` (`src/update.rs`, ohne
  Einstellung, immer an); ist eine neuere Fassung da, bietet seine Seite „Update“ an (`/api/state` Feld `update`, `POST /api/self_update`): Es lädt
  `kraken-mount.elf` und sendet es an den ELF-Lader, die neue Kopie übernimmt Port 8097 und beendet die alte (#117). Neu ist auch eine
  `payloads.json` für den ps5-payload-manager. **Die App schaltet diese Abfrage ab** (siehe „Änderung durch diese App“): Die geladene Kopie wäre
  nicht geändert und würde auf das Ende von ShadowMountPlus warten.
- **Playden-Unterstützung (#105):** Ordner lassen sich vom Suchlauf ausnehmen (Einstellung `exclude`); ein Titel lässt sich vom Startbildschirm
  nehmen, während sein Ordner bleibt; `GET /api/state` sagt je Spiel `registered`, `mounted` und `source_present`; ein Schalter je Ordner-Spiel
  (`overlay_off`, `POST /api/overlay/<TID>`) schaltet den Fakelib-Überbau ab. Die Aufrufe folgen der `/api/v1`-Schnittstelle von ShadowMountPlus auf
  Port 8097. Die App nutzt davon nichts.
- **Absturzbericht (#100):** Ein Absturz eines Spiels wird mit Zeit und Prozess festgehalten (`crashes.json`, Feld `crash` je Spiel,
  `GET /api/report/<TID>`).
- **Neu aufbauen** bleibt gesperrt, bis der Neuaufbau fertig ist (#115; Feld `rebuild_queued` je Spiel). **Die App übernimmt das**: Browser und
  Vollbild-App sperren „Neu aufbauen“, solange `rebuild_queued` gilt oder das Spiel noch wartet (`pending`).
- **Weitere:** Bibliothek mit Suche, Spielen-Knopf und Laufwerksfilter (#116); Apps, die beim Beenden eine zweite ELF starten, laufen aus einer
  `.pkg`, und das Schließen eines Spiels hängt nicht mehr und schaltet die Konsole nicht mehr aus (#109; die Zwischenspeicher der `.pkg`-Spiele
  bauen sich dafür einmal neu auf); Homebrew sieht die Schriften der Konsole unter `/preinst/common/font` (#119); auf der Firmware 12 und neuer
  meldet sich die eigene Kachel wieder an, auch nachdem sie entfernt wurde (#120; siehe oben, die App hält sie fern); die Seite ist nach dem
  Ruhemodus wieder erreichbar (#110).
- **ampr-emu-ng v0.5.1 ist eingebaut** (vorher v0.5.0).
- Die Schnittstelle, die `src/kraken.c` benutzt (`version`, `scanpath`, `games` mit `title_id`, `title_name`, `path`, `kind`, `status`, `playgo`,
  `playgo_off`; `/api/rescan`, `/api/rebuild/<TID>`, `/api/rebuild-all`, `/api/playgo/<TID>`, `/api/delete/<TID>`, `/api/quit`), ist unverändert;
  der Ein-Byte-Eingriff (`shadowmountplus.elf`) greift unverändert (2 Stellen, `wait_ready` in `src/main.rs` gleich geblieben).

## Neu in v0.3.1 (20.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.3.0…v0.3.1)

- **Firmware 12 und neuer:** Spiele melden sich wieder am Startbildschirm an. Wo der Aufruf für ein einzelnes Spiel fehlt, bereitet jeder Suchlauf seine Spiele vor
  und meldet sie am Ende mit einem einzigen Aufruf (`sceAppInstUtilAppInstallAll`) an, mit einer Meldung für die ganze Gruppe (#95; Dank an Dang3rZone). Nicht, solange
  ein Spiel läuft: Dann werden die vorbereiteten Titel nach dem Spiel noch einmal vorbereitet und angemeldet.
- **PlayGo-Stub je Spiel abschaltbar:** Einstellung `playgo_off` (Liste von Titelnummern) und neu in der Schnittstelle `POST /api/playgo/<TID>` (Rumpf genau `on` oder
  `off`; 204, 404 für eine ungültige oder unbekannte Titelnummer, 400 sonst); die Spiele in `GET /api/state` tragen neu `playgo` (die `eboot.bin` lädt PlayGo, `null`
  wenn unbekannt oder `add_emulators` aus) und `playgo_off`. Resident Evil Village und einige andere bleiben mit dem Stub auf einem schwarzen Bildschirm (#84, #85, #87).
  **Die App zeigt den Schalter** je Spiel im Reiter „FPKGMount“ (Browser: Knopf „PlayGo-Stub ausschalten/einschalten“; Vollbild-App: Eintrag im Spielmenü), nur bei
  Spielen, die PlayGo laden oder bei denen er schon aus ist, und nur wenn die Antwort `playgo_off` enthält (ältere Fassungen kennen die Route nicht); die Aktion
  heißt `playgo` in `POST /api/v1/kraken` (`src/kraken.c`).
- **Scan:** Spiele in einem Ordner direkt in einem Suchordner werden jetzt auch gefunden (#83; ein Titel, der direkt in einem Suchordner liegt, wird nie von einer Kopie
  in einem Unterordner verdrängt).
- **Schnellere Anmeldung:** Dateien in `sce_sys` und im Appmeta-Ordner werden in 1-MiB-Stücken kopiert; ein Spiel anzumelden dauert etwa 21 statt 36 bis 44 Sekunden (#94).
- **Protokoll:** Jede Zeile in `kraken.log` beginnt mit der Uhrzeit; Zwischenspeicher-Bauten, Anmeldungen und langsame Suchläufe nennen ihre Dauer (#92).
- **Einstellungen:** Ein falscher Eintrag in einer Liste (`scanpath`, `playgo_off`) wird übersprungen, die anderen bleiben.
- **ampr-emu-ng v0.5.0 ist eingebaut** (vorher v0.4.1).
- Die übrige Schnittstelle (Port 8097; die Felder, die `src/kraken.c` liest: `version`, `scanpath`, `games` mit `title_id`, `title_name`, `path`, `kind`) ist unverändert; der
  Ein-Byte-Eingriff (`shadowmountplus.elf`) gilt unverändert (2 Stellen, an der ELF dieser Fassung geprüft).

## Neu in v0.3.0 (18.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.2.1…v0.3.0)

- **Spielstände:** Die eigene `libSceSaveData.native.sprx` aus dem `fakelib/` eines Spiels wird nicht mehr benutzt. Sie ersetzte das
  Spielstand-Modul der Konsole und ließ Spielstände verschwinden oder nicht laden (#68, #64; Dank an AstroBoot-Dev, #76, und an strongt1me für die Prüfung).
- **.pkg-Spiele bekommen immer ampr-emu-ng,** auch bei `inject_ampr` aus: Nur ampr-emu-ng liest ein Spiel aus der `.pkg`. Die Einstellung gilt
  nur noch für Ordner-Spiele (#73).
- **Mitgelieferte Emulatoren für DLC und PlayGo:** dlc_emu 0.3 und pgo_stub 0.6 von drakmor (beide GPL-3.0) stecken in der ELF.
  dlc_emu kommt in den `fakelib` eines Spiels mit `dlc_emu.ini`, pgo_stub bei einem Spiel, das PlayGo nutzt, jeweils nur, wenn das Spiel
  keine neuere Kopie mitbringt. Einstellung `add_emulators` (an); siehe `docs/dlcemu.md`, `docs/playgo.md` im Quellprojekt (#71). Deshalb ist
  die ELF von 3,8 auf 6,4 MB gewachsen.
- **Weitere:** Titelmusik beim Markieren eines Spiels auf dem Startbildschirm (#57); Spielnamen in der Sprache der Konsole; **jedes USB-Laufwerk
  hat auf der Seite von Kraken Mount einen Knopf zum Auswerfen** (neu in der Schnittstelle: `POST /api/eject` mit dem Laufwerkspfad im
  Rumpf, 409 bei laufendem Spiel; das Feld `ejectable` je Laufwerk in `GET /api/state`; die App benutzt beides nicht).
- **Scan:** Ein Spiel, das noch kopiert wird, bleibt außen vor, bis sich seine Dateien nicht mehr ändern (`stability_wait_seconds`, 10; bei
  0 aus). Ein Titel, dessen Quelle 5 Minuten fehlt (zum Beispiel ein abgezogenes Laufwerk), wird vom Startbildschirm genommen
  (`auto_remove_missing`, an; `auto_remove_delay_seconds`, 300); sein Zwischenspeicher bleibt, und er kommt mit der Quelle zurück. Nicht, solange ein Spiel läuft,
  nicht bei DLC in `/user/addcont/<TID>`, und nie ein von der Konsole selbst installiertes Spiel mit gleicher Titelnummer.
- **Beim ersten Start nach dem Wechsel** baut Kraken Mount den `fakelib` **jedes** Spiels einmal neu zusammen.
- Die übrige Schnittstelle (Port 8097; die Felder, die `src/kraken.c` liest: `version`, `scanpath`, `games` mit `title_id`, `title_name`, `path`, `kind`) ist unverändert; der
  Ein-Byte-Eingriff (`shadowmountplus.elf`) gilt unverändert (2 Stellen, an der ELF dieser Fassung geprüft; `wait_ready` in `src/main.rs` ist gleich geblieben).

## Neu in v0.2.1 (16.11.2026, laut Veröffentlichung)

- **Trophäen, Spielstand-Symbole und Hintergründe** kommen jetzt auch bei FPKG-Spielen an: Dateien, die nur im äußeren Behälter des Pakets
  liegen (`save_data.png`, `pic0`/`pic1`, `snd0.at9`, die Trophäendateien, die verschlüsselten `npbind.dat`/`nptitle.dat`), legt Kraken Mount
  jetzt auch in den Zwischenspeicher des Spiels. Das behebt die Meldung CE-107112-2 bei Spielen, deren Spielstand eigentlich funktionierte
  (#41, #48), fehlende Trophäen und fehlende Hintergründe (#51).
- **Dateizugriff:** Spiele und die Spielstand- und Trophäendienste der Konsole lesen Dateien unter ihrem eigenen Benutzer. Kraken Mount macht
  Zwischenspeicher, `.pkg`-Dateien samt Ordnern und Spielordner dafür lesbar (nur Rechte hinzufügen, nie entfernen). Vorher ließ eine mit
  eingeschränkten Rechten hochgeladene `.pkg` ampr-emu-ng in den LZ4-Modus zurückfallen, und das Spiel startete nicht (#36, #56).
- Beim ersten Start nach dem Wechsel baut Kraken Mount den Zwischenspeicher **jedes** Spiels einmal neu auf (dauert, braucht freien Platz).
- Die Schnittstelle (Port 8097) ist unverändert; der Ein-Byte-Eingriff (`shadowmountplus.elf`) gilt unverändert (2 Stellen).

## Neu in v0.2.0 (laut Veröffentlichung und DESIGN.md)

Alle FPKG-Dateien werden gelesen, auch solche mit Lücken (sparse); Oberfläche und Meldungen in 11 Sprachen; ampr-emu-ng v0.4.1 ist
eingebaut und wird beim Start auf Wunsch aktualisiert (`ampr_auto_update`, `GET /api/ampr`, `POST /api/ampr/update`); ein auf ein anderes
Laufwerk oder in einen anderen Ordner verschobenes Spiel wird neu verknüpft statt neu gebaut; neue Spiele melden sich auch dort an, wo nur
elfldr läuft (`/system_ex` wird bei EROFS einmal schreibbar eingehängt; Dank an strongt1me, #22); leichtere Oberfläche. Die Einstellung
`game_fakelib` gibt es nicht mehr (in `config.toml` ignoriert).

**Wichtig für diese App:** Kraken Mount wartet seit v0.2.0 nach dem Start (`wait_ready`, `src/main.rs`), bis kstuff geladen ist **und kein Prozess
`shadowmountplus.elf` mehr läuft**, bevor es irgendein Spiel anmeldet. Der Zustand steht als Feld `waiting` (`"kstuff"`, `"shadowmount"` oder
null) in `GET /api/state`. Läuft ShadowMountPlus dauerhaft, meldet Kraken Mount nichts an; die Seite FPKGMount zeigt es an.

**Änderung durch diese App (modifizierte Kopie, GPL-3.0):** Die App betreibt Kraken Mount absichtlich neben ShadowMountPlus (getrennte Suchordner, wie
mit v0.0.1-beta). Deshalb ändert sie in der eingebetteten ELF beim Bau (`tools/patch_kraken.py`, `Makefile`) und in einer von GitHub geladenen
Fassung beim Speichern (`patch_smp_wait` und `patch_self_update`, `src/kraken.c`) zwei Zeichenketten, jede an einem Byte und an zwei Stellen (das Programm
trägt sich für seine Selbstkopie zweimal in der Datei):

1. `shadowmountplus.elf` (die Konstante `SMP_PROCESS` in `src/main.rs`) wird zu `shadowmountplus.elX`: Kein Prozess hat diesen Namen, das Warten auf
   ShadowMountPlus endet sofort. Das Warten auf kstuff bleibt.
2. Ab v0.3.2: `https://api.github.com/repos/quer3q/kraken-mount/releases/latest` (die Konstante `RELEASE_URL` in `src/update.rs`) wird zu
   `xttps://…`: Die HTTP-Bibliothek der Konsole lehnt das unbekannte Schema sofort ab, keine Anfrage verlässt die Konsole, das Programm findet keine
   neuere Fassung und bietet auf seiner Seite kein „Update“ an. Sonst käme über diesen Knopf eine ungeänderte Fassung, die die laufende ablöst und
   dann auf das Ende von ShadowMountPlus wartet. Neue Fassungen kommen über den Knopf der App, der beide Änderungen macht. Die Abfrage für
   ampr-emu-ng hat eine eigene Adresse und bleibt.

Gleiche Länge, sonst bleibt die ELF unverändert. Die Datei `kraken-mount_v0.4.0.elf` in diesem Ordner ist die unveränderte Veröffentlichung (SHA-256
oben); die geänderte entsteht erst im Bau (`gen/kraken-mount-patched.elf`, nicht im Repository). Der Quelltext der Änderungen sind die beiden Werte
`SMP_PROCESS` (`src/main.rs`) und `RELEASE_URL` (`src/update.rs`) im Quellprojekt, Tag `v0.4.0`.

## Hinweise des Autors

Kraken Mount ist keine Beta mehr (die Readme von v0.3.2 nennt keine; vorher „experimentell“). Für große, anspruchsvolle Spiele
empfiehlt der Autor das zstd-Paketformat: Es läuft besser als FPKG. Einstellungen stehen in `/data/krakenmount/config.toml`, das
Protokoll in `/data/krakenmount/kraken.log`; Probleme bitte mit dessen Zeilen beim Autor melden.
