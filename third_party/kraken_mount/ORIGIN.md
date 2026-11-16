# Kraken Mount

**Version:** v0.2.1 (16.11.2026)
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/kraken-mount (Tag `v0.2.1`, Commit `029386534988d7f70ba3eb2e01d4c285336e5611`)
**Lizenz:** GPL-3.0-or-later (Lizenztext und Hinweise zu Fremdbausteinen im Quellprojekt: `LICENSE`, `THIRD_PARTY_NOTICES`)
**Datei:** `kraken-mount_v0.2.1.elf` (PS5-ELF, 3 803 240 Bytes), unverändert aus der Veröffentlichung
**SHA-256:** `3b50bdfffaad5bacdd41ad380cc7ce3dfe419d8efde57b30c0c0e7a338fe2494` (gleich `SHA256SUMS` der Veröffentlichung)

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
gibt es unter der oben genannten Adresse (Tag `v0.2.1`).

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
Länge, sonst bleibt die ELF unverändert; das Warten auf kstuff bleibt. Die Datei `kraken-mount_v0.2.1.elf` in diesem Ordner ist die unveränderte
Veröffentlichung (SHA-256 oben); die geänderte entsteht erst im Bau (`gen/kraken-mount-patched.elf`, nicht im Repository). Der Quelltext der Änderung ist
der eine Wert in `SMP_PROCESS` (`src/main.rs` im Quellprojekt, Tag `v0.2.1`).

## Hinweise des Autors

Experimentell: Nur wenige Spiele wurden bisher ausprobiert. Ein Spiel kann als `.pkg` schlechter laufen als in zstd-Paketen;
Einstellungen, Cache-Aufbau und Pfade können sich zwischen Fassungen ändern (dann baut sich der Cache neu auf). Probleme bitte mit
den Zeilen aus `/data/krakenmount/kraken.log` beim Autor melden.
