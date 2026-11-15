# Kraken Mount

**Version:** v0.2.0 (15.11.2026)
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/kraken-mount (Tag `v0.2.0`, Commit `8531071016c27bcb5e23e49f143409b1e0b7371f`)
**Lizenz:** GPL-3.0-or-later (Lizenztext und Hinweise zu Fremdbausteinen im Quellprojekt: `LICENSE`, `THIRD_PARTY_NOTICES`)
**Datei:** `kraken-mount_v0.2.0.elf` (PS5-ELF, 3 803 240 Bytes), unverändert aus der Veröffentlichung
**SHA-256:** `a9c02fe0dabd24581f2dec79b7bb92825284005045862c28e4e9fb973c3cf5bf` (gleich `SHA256SUMS` der Veröffentlichung)

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
gibt es unter der oben genannten Adresse (Tag `v0.2.0`).

## Neu in v0.2.0 (laut Veröffentlichung und DESIGN.md)

Alle FPKG-Dateien werden gelesen, auch solche mit Lücken (sparse); Oberfläche und Meldungen in 11 Sprachen; ampr-emu-ng v0.4.1 ist
eingebaut und wird beim Start auf Wunsch aktualisiert (`ampr_auto_update`, `GET /api/ampr`, `POST /api/ampr/update`); ein auf ein anderes
Laufwerk oder in einen anderen Ordner verschobenes Spiel wird neu verknüpft statt neu gebaut; neue Spiele melden sich auch dort an, wo nur
elfldr läuft (`/system_ex` wird bei EROFS einmal schreibbar eingehängt; Dank an strongt1me, #22); leichtere Oberfläche. Die Einstellung
`game_fakelib` gibt es nicht mehr (in `config.toml` ignoriert).

**Wichtig für diese App:** Kraken Mount wartet seit v0.2.0 nach dem Start (`wait_ready`, `src/main.rs`), bis kstuff geladen ist **und kein Prozess
`shadowmountplus.elf` mehr läuft**, bevor es irgendein Spiel anmeldet. Der Zustand steht als Feld `waiting` (`"kstuff"`, `"shadowmount"` oder
null) in `GET /api/state`. Läuft ShadowMountPlus dauerhaft, meldet Kraken Mount nichts an; die Seite FPKGMount zeigt es an.

## Hinweise des Autors

Experimentell: Nur wenige Spiele wurden bisher ausprobiert. Ein Spiel kann als `.pkg` schlechter laufen als in zstd-Paketen;
Einstellungen, Cache-Aufbau und Pfade können sich zwischen Fassungen ändern (dann baut sich der Cache neu auf). Probleme bitte mit
den Zeilen aus `/data/krakenmount/kraken.log` beim Autor melden.
