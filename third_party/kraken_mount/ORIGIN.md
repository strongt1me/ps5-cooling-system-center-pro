# Kraken Mount

**Version:** v0.4.2 (27.11.2026)
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/kraken-mount (Tag `v0.4.2`, Commit `7c73d0a09708464d74fe22009b3ccad9b5cd1ebe`)
**Lizenz:** GPL-3.0-or-later (Lizenztext und Hinweise zu Fremdbausteinen im Quellprojekt: `LICENSE`, `THIRD_PARTY_NOTICES`)
**Datei:** `kraken-mount_v0.4.2.elf` (PS5-ELF, 7 601 584 Bytes), unverändert aus der Veröffentlichung
**SHA-256:** `4d4ec0fc2b436d7be3e9d573dc63e6c94c64eac0d72c08aa15fe54446a19201f` (gleich `SHA256SUMS` der Veröffentlichung; am 27.11.2026 mit `gh release download v0.4.2` geladen, ELF-Kopf und Kennung `kraken-mount/0.4.2` geprüft)

## Verwendung

Kraken Mount ist ein eigenes Programm (Rust), das Spiele aus Debug-FPKG-`.pkg`-Dateien und Spielordnern (roh, zstd oder LZ4) ohne
Installation auf dem Startbildschirm anmeldet und sie mit ampr-emu-ng startet. Der Reiter „KrakenMount“ der App (Spiele) startet es,
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
gibt es unter der oben genannten Adresse (Tag `v0.4.2`).

## Neu in v0.4.2 (27.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.4.1…v0.4.2)

- **Spiel-Abbilder** (`src/image.rs`, `src/image/probe.rs`, Port von ShadowMountPlus `sm_image.c`/`sm_mount_device.c`, nur LVD): `.exfat`, `.ffpkg`
  (UFS), `.ffpfs` (PFS) und `.ffpfsc` (PFS-Behälter) direkt in einem Suchordner (oder eine Ebene tiefer) werden vorher mit den Lesern von PS5 Dump
  Forge geprüft (`ps5-dump-forge-pfs`, `ps5upload-fpkg`; ihre Lizenzhinweise stehen in den `THIRD_PARTY_NOTICES` von Kraken Mount), dann über
  `/dev/lvdctl` angehängt und **schreibgeschützt** auf `/mnt/krakenmount/[pfsc_]<Name>_<Prüfwert>` eingehängt. Das Spiel ist dann ein Ordner-Spiel:
  `path` in `GET /api/state` ist der Spielordner in diesem Einhängepunkt, das neue Feld `image` nennt die Abbild-Datei. `/data/krakenmount/images/<Name>`
  hält `<Einheit>\n<Abbild>`; eine spätere Kopie übernimmt eine noch lebende Einhängung. Freigegeben wird nur, was kein Spiel mehr nutzt, beim
  eigenen Aushängen eines Laufwerks (`POST /api/eject`, vor dem Auswerfen) und vor dem Löschen. **Für diese App:** Die App gibt Kraken Mount eigene
  Suchordner (`Kraken-Games`, bis 27.11.2026 `FPKG-Games`), in denen ShadowMountPlus nicht sucht; doppelt eingehängt wird ein Abbild also nur, wenn jemand die Suchordner ändert
  (dann meldet `mounters.c` den Titel wie bisher). Die Einhängung eines Abbilds nennt keinen Pfad des Laufwerks (sie kommt von `/dev/lvdN`) und
  bleibt auch nach dem Beenden von Kraken Mount stehen; das Auswerfen der App über ShadowMountPlus gibt sie nicht frei. Deshalb lehnt die App das
  Auswerfen ab, solange Kraken Mount ein Abbild des Laufwerks eingehängt hat (`kraken_images_on` in `src/usbeject.c`, über die Einhängetabelle und
  `/data/krakenmount/images`), und nennt den Weg über das Aushängen in Kraken Mount. Die Seite KrakenMount zeigt bei solchen Spielen „Abbild (exfat)“
  usw. und nennt beim Löschen die Abbild-Datei (Kraken Mount löscht die Datei, nachdem es die Einhängung freigegeben hat).
- **ampr-emu-ng je Ordner-Spiel abschaltbar** (`ampr_off`, auf seiner Seite; `POST /api/ng/<ID>` jetzt auch für ungepackte Ordner, solange
  `inject_ampr` an ist). Von der App nicht benutzt.
- **Suche läuft weiter, während ein Homebrew-Titel läuft** (eine Titel-ID, die nicht mit `CUSA`/`PPSA` beginnt): Ein Starter wie Playden bekommt neue
  Spiele, ohne geschlossen zu werden. Laufende Spiele halten die Suche weiter an (`paused`).
- **Schnittstelle von ShadowMountPlus:** `games/info` (ein Titel, für den Installer von RecompStore) und `games/fakelib` für einen Titel, der noch nicht
  in der Liste steht. Sie liegt in der Fassung dieser App weiter auf 18097 (Änderung 3).
- **Behoben:** Ein außerhalb von Kraken Mount deinstallierter Titel (anderes Programm, Löschen auf dem Startbildschirm) wird beim nächsten Durchlauf
  wieder angemeldet, nicht erst nach einem Neustart.
- Mitgeliefert weiter ampr-emu-ng 0.5.1.

## Neu in v0.4.1 (26.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.4.0…v0.4.1)

- **Schnittstelle von ShadowMountPlus auf `127.0.0.1:10101`** (`src/web.rs` `serve_smp`, Hinweis des Users 26.11.2026): Kraken Mount beantwortet dort
  einen Teil der API von ShadowMountPlus (`/api/v1/version`, `games`, `scan`, `manual/add|remove`, `games/uninstall`, `games/fakelib`; alles andere 404),
  damit Programme, die nur ShadowMountPlus kennen (Playden, RecompStore), es finden. Solange der Port belegt ist, bindet es alle 2 s neu. ShadowMountPlus
  bindet `0.0.0.0:10101`; Rust bindet mit `SO_REUSEADDR`, und auf einem BSD-Kern darf der genauere `127.0.0.1:10101` daneben gebunden werden und bekommt
  dann jede Verbindung an 127.0.0.1 – genau so spricht diese App mit ShadowMountPlus. Beide laufen hier absichtlich nebeneinander, deshalb verlegt die App
  diesen einen Port (Änderung 3 unten). Zusätzlich erkennt `src/smp.c` Kraken Mount auf dem Port (Antwort mit `kraken_mount_version`) und hält es nicht für
  ShadowMountPlus (für eine eigenständig gestartete Fassung).
- **Installierte Spiele über ampr-emu-ng** (Einstellung `ampr_installed`, ab Werk **aus**; je Spiel `ampr_installed_on/off`, `POST /api/ng/<ID>`): ein
  installiertes Debug-FPKG startet dann aus seiner `app.pkg`, ein installiertes Spiel aus dem Handel mit `/data/krakenmount/ampr/*` im `fakelib`. Ab Werk
  ändert sich an installierten Spielen nichts; die App zeigt sie weiter nicht im Reiter (`kind: "installed"`, jetzt auch für eine `.pkg`, die die
  eigene Installation der Konsole ist).
- **Spiel schließen** (`POST /api/close/<ID>`), Duplikate mit Größe (`POST /api/sizes/<ID>`), QR-Code der Adresse (`GET /api/qr`), neun Farbschemata,
  größere Schrift auf seiner Seite. Von der App nicht benutzt.
- Mitgeliefert weiter ampr-emu-ng 0.5.1, dlc_emu 0.3 und pgo_stub 0.6.

## Neu in v0.4.0 (25.11.2026, laut Veröffentlichung, `DESIGN.md` und Vergleich v0.3.2…v0.4.0)

- **Installierte Spiele in der Bibliothek:** Spiele, die auf der Konsole installiert sind (`/user/app` und der erweiterte Speicher), stehen jetzt
  auch in seiner Liste (`GET /api/state`, `kind: "installed"`, neue Felder `installed`, `installed_version`, `retail` je Spiel). Kraken Mount hängt sie
  nie ein, nimmt sie nie vom Startbildschirm und löscht sie nie (`/api/delete`, `/api/hide` lehnen ab: „an installed game: uninstall it from the
  console's menu“). Eine installierte Kopie gewinnt gegen dasselbe Spiel als FPKG oder Ordner (#121). **Die App lässt diese Einträge aus** ihrem Reiter
  KrakenMount, aus der Warnliste für doppelt angemeldete Titel und aus der Prüfung vor dem Löschen heraus (`kr_drop_installed` in `src/kraken.c`); die
  Seite Spiele der App zeigt sie ohnehin.
- **Backports für installierte Spiele:** Ein Backport in `backports/<TITLE_ID>*` (in den Suchordnern, sonst in `/data/homebrew` wie bei ShadowMountPlus)
  gilt jetzt auch für ein installiertes Spiel: Seine Bibliotheken kommen in den Fakelib-Überbau, seine übrigen Dateien werden bei jedem Start über
  NSFS-Umleitungen in das `/app0` des Spiels gelegt (Port von ShadowMountPlus 1.7 `sm_pkg_backport.c`, neu `src/launch/nsfs.rs`); die Installation
  selbst bleibt unverändert. Mit `ampr_installed` aus (Standard) läuft bei einem installierten Spiel die `libSceAmpr.sprx` des Systems.
  **Wichtig für diese App:** Weil die App Kraken Mount neben ShadowMountPlus laufen lässt (siehe „Änderung durch diese App“), könnten beide denselben
  Backport eines installierten Spiels anwenden. **An der Slim geprüft (25.11.2026, ASTRO's PLAYROOM, echt installiert, ShadowMountPlus
  1.7beta12 und Kraken Mount 0.4.0, Test-Backport `/data/homebrew/backports/PPSA01325` mit einer Textdatei):** Beide legen ihre NSFS-Umleitung
  auf dieselbe Datei an, ohne Fehler (ShadowMountPlus `[BKP][NSFS] … redirects=1`, Kraken Mount `backport redirects ACTIVE: 1 … before exec`);
  die Konsole nimmt die zweite Umleitung an. Das Spiel startet und läuft normal, es gibt keine Fehlermeldung und keinen doppelten
  Fakelib-Überbau (bei einem fremden unionfs auf `common/lib` weichen beide aus; laut Quelltext wertet ShellCore die Antwort von
  ShadowMountPlus ohnehin nicht aus). Beim Schließen geben beide ihre Umleitung mit dem Namensraum frei, der Kernel entfernt beide
  (`nsfs_unmount_rdct: remaining rdct …` zweimal); kein Absturz, kein Coredump. Auf der Pro gibt es kein echt installiertes Spiel
  (keine `/user/app/<ID>/app.pkg` außer YouTube), dort tritt der Fall nicht auf.
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
  **Die App zeigt den Schalter** je Spiel im Reiter „KrakenMount“ (Browser: Knopf „PlayGo-Stub ausschalten/einschalten“; Vollbild-App: Eintrag im Spielmenü), nur bei
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
- **Mitgelieferte Emulatoren für DLC und PlayGo:** dlc_emu 0.3 und pgo_stub 0.6 von drakmor (dlc_emu GPL-3.0-or-later, pgo_stub GPL-3.0) stecken in der ELF.
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
null) in `GET /api/state`. Läuft ShadowMountPlus dauerhaft, meldet Kraken Mount nichts an; die Seite KrakenMount zeigt es an.

**Änderung durch diese App (modifizierte Kopie, GPL-3.0):** Die App betreibt Kraken Mount absichtlich neben ShadowMountPlus (getrennte Suchordner, wie
mit v0.0.1-beta). Deshalb ändert sie in der eingebetteten ELF beim Bau (`tools/patch_kraken.py`, `Makefile`) und in einer von GitHub geladenen
Fassung beim Speichern (`patch_smp_wait`, `patch_self_update` und `patch_smp_port`, `src/kraken.c`) zwei Zeichenketten, jede an einem Byte, und ab v0.4.1
eine Portnummer, jede Änderung an zwei Stellen (das Programm trägt sich für seine Selbstkopie zweimal in der Datei):

1. `shadowmountplus.elf` (die Konstante `SMP_PROCESS` in `src/main.rs`) wird zu `shadowmountplus.elX`: Kein Prozess hat diesen Namen, das Warten auf
   ShadowMountPlus endet sofort. Das Warten auf kstuff bleibt.
2. Ab v0.3.2: `https://api.github.com/repos/quer3q/kraken-mount/releases/latest` (die Konstante `RELEASE_URL` in `src/update.rs`) wird zu
   `xttps://…`: Die HTTP-Bibliothek der Konsole lehnt das unbekannte Schema sofort ab, keine Anfrage verlässt die Konsole, das Programm findet keine
   neuere Fassung und bietet auf seiner Seite kein „Update“ an. Sonst käme über diesen Knopf eine ungeänderte Fassung, die die laufende ablöst und
   dann auf das Ende von ShadowMountPlus wartet. Neue Fassungen kommen über den Knopf der App, der alle Änderungen macht. Die Abfrage für
   ampr-emu-ng hat eine eigene Adresse und bleibt.
3. Ab v0.4.1: Der Port seiner Schnittstelle für Programme, die nur ShadowMountPlus kennen (`SMP_PORT` = 10101 in `src/web.rs`), wird 18097. Er steht
   als 16-Bit-Wert in einem Befehl vor dem Binden (`movw $0x2775, 0x70(%rsp)`, Bytes `66 C7 44 24 70 75 27`, je Stufe einmal) und wird zu
   `66 C7 44 24 70 B1 46`. Die Schnittstelle bleibt, aber auf einem Port, den niemand fragt; ShadowMountPlus bleibt für diese App auf 127.0.0.1:10101
   erreichbar. Seine eigene Schnittstelle auf 8097 bleibt unverändert. Eine spätere Fassung, in der der Befehl anders aussieht, wird nicht geändert
   (das Bauprotokoll und die Meldung `kraken_patch` nennen dann 0 Stellen); `src/smp.c` erkennt Kraken Mount auf 10101 dann trotzdem.
   Achtung beim Prüfen: Seine Protokollzeile „ShadowMount+ API on 127.0.0.1:10101“ gibt die Konstante aus (`log!(… {SMP_PORT})`), nicht den Port
   des Bindens; sie nennt auch in der geänderten Fassung 10101. Ob ShadowMountPlus erreichbar bleibt, zeigt die App (kein `smp_port_kraken`).

Gleiche Länge, sonst bleibt die ELF unverändert. Die Datei `kraken-mount_v0.4.2.elf` in diesem Ordner ist die unveränderte Veröffentlichung (SHA-256
oben); die geänderte entsteht erst im Bau (`gen/kraken-mount-patched.elf`, nicht im Repository). Der Quelltext der Änderungen sind die drei Werte
`SMP_PROCESS` (`src/main.rs`), `RELEASE_URL` (`src/update.rs`) und `SMP_PORT` (`src/web.rs`) im Quellprojekt, Tag `v0.4.2`.

## Hinweise des Autors

Kraken Mount ist keine Beta mehr (die Readme von v0.3.2 nennt keine; vorher „experimentell“). Für große, anspruchsvolle Spiele
empfiehlt der Autor das zstd-Paketformat: Es läuft besser als FPKG. Einstellungen stehen in `/data/krakenmount/config.toml`, das
Protokoll in `/data/krakenmount/kraken.log`; Probleme bitte mit dessen Zeilen beim Autor melden.
