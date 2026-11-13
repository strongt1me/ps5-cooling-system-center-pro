# Kraken Mount

**Version:** v0.0.1-beta (13.11.2026)
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/kraken-mount (Tag `v0.0.1-beta`, Commit `365b1a37b136d95415c8121883e1428895f39046`)
**Lizenz:** GPL-3.0-or-later (Lizenztext und Hinweise zu Fremdbausteinen im Quellprojekt: `LICENSE`, `THIRD_PARTY_NOTICES`)
**Datei:** `kraken-mount_v0.0.1-beta.elf` (PS5-ELF, 4 327 528 Bytes), unverändert aus der Veröffentlichung
**SHA-256:** `32cdf8e9aee9a8e91a7acd8884ac4701a75a38d96d483ea00bb7cbf4fe11a522` (gleich `SHA256SUMS` der Veröffentlichung)

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
gibt es unter der oben genannten Adresse (Tag `v0.0.1-beta`).

## Hinweise des Autors

Experimentell: Nur wenige Spiele wurden bisher ausprobiert. Ein Spiel kann als `.pkg` schlechter laufen als in zstd-Paketen;
Einstellungen, Cache-Aufbau und Pfade können sich zwischen Fassungen ändern (dann baut sich der Cache neu auf). Probleme bitte mit
den Zeilen aus `/data/krakenmount/kraken.log` beim Autor melden.
