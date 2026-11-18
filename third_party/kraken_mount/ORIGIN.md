# Kraken Mount

**Version:** v0.3.0 (18.11.2026)
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/kraken-mount (Tag `v0.3.0`, Commit `e1e784560fe3a56e3b5ffd579f47ed5b2cf194f7`)
**Lizenz:** GPL-3.0-or-later (Lizenztext und Hinweise zu Fremdbausteinen im Quellprojekt: `LICENSE`, `THIRD_PARTY_NOTICES`)
**Datei:** `kraken-mount_v0.3.0.elf` (PS5-ELF, 6 355 424 Bytes), unverändert aus der Veröffentlichung
**SHA-256:** `092623a204b707b89d0867a0087f9dd1c430eef6a22db497f03b59aaf1297bfe` (gleich `SHA256SUMS` der Veröffentlichung)

## Verwendung

Kraken Mount ist ein eigenes Programm (Rust), das Spiele aus Debug-FPKG-`.pkg`-Dateien und Spielordnern (roh, zstd oder LZ4) ohne
Installation auf dem Startbildschirm anmeldet und sie mit ampr-emu-ng startet. Der Reiter „FPKGMount“ der App (Spiele) startet es,
zeigt seine Spiele und bedient es über seine Schnittstelle auf Port 8097 (`src/kraken.c`).

Die fertige ELF wird als zlib-Blob eingebettet (`tools/gen_blob.py`, `gen/kraken_blob.c`) und bei „Kraken Mount starten“ an den
lokalen ELF-Lader (127.0.0.1:9021) gesendet. Eine neuere Fassung lädt die App nur auf Knopfdruck von
`github.com/quer3q/kraken-mount/releases` (HTTPS, Prüfsumme aus `SHA256SUMS` der Veröffentlichung) nach
`/data/PS5-Cooling-Center/kraken/`; sie wird dann statt der eingebauten gestartet.

Kraken Mount legt bei jedem Start eine Kachel (KRKN00001) für seine Seite an (`register.rs`, `ensure_tile`). Die App will keine: Vor dem
Start legt sie `/user/app/KRKN00001` als leere Datei an; ohne seine Markierung `/data/krakenmount/tile.ok` lässt das Programm den Platz
laut `ensure_tile` in Ruhe. Eine früher angelegte Kachel entfernt die App mit der Deinstallation der Konsole und löscht die Markierung
(`keep_tile_away` in `src/kraken.c`).

Aus dem Quellcode von Kraken Mount ist nichts in diese App übernommen; die ELF wird nur weitergegeben. Den Quellcode zu dieser Fassung
gibt es unter der oben genannten Adresse (Tag `v0.3.0`).

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
Fassung beim Speichern (`patch_smp_wait`, `src/kraken.c`) die Zeichenkette `shadowmountplus.elf` (die Konstante `SMP_PROCESS` in `src/main.rs`, zwei
Stellen in der Datei) an ihrem letzten Byte zu `shadowmountplus.elX`: Kein Prozess hat diesen Namen, das Warten auf ShadowMountPlus endet sofort. Gleiche
Länge, sonst bleibt die ELF unverändert; das Warten auf kstuff bleibt. Die Datei `kraken-mount_v0.3.0.elf` in diesem Ordner ist die unveränderte
Veröffentlichung (SHA-256 oben); die geänderte entsteht erst im Bau (`gen/kraken-mount-patched.elf`, nicht im Repository). Der Quelltext der Änderung ist
der eine Wert in `SMP_PROCESS` (`src/main.rs` im Quellprojekt, Tag `v0.3.0`).

## Hinweise des Autors

Experimentell: Nur wenige Spiele wurden bisher ausprobiert. Ein Spiel kann als `.pkg` schlechter laufen als in zstd-Paketen;
Einstellungen, Cache-Aufbau und Pfade können sich zwischen Fassungen ändern (dann baut sich der Cache neu auf). Probleme bitte mit
den Zeilen aus `/data/krakenmount/kraken.log` beim Autor melden.
