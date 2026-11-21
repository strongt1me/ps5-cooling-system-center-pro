# Dump Forge

**Version:** v0.5.6
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/ps5-dump-forge (Tag `v0.5.6`)
**Lizenz:** GPL-3.0-or-later (Lizenztext im Quellprojekt)
**Datei:** `ps5-dump-forge-0.5.6-ps5.elf` (PS5-ELF, 18 549 840 Bytes), unverändert aus der Veröffentlichung. Seit dem 21.11.2026 **in der App eingebettet**, als zstd-Rahmen
`ps5-dump-forge-0.5.6-ps5.elf.zst` (Stufe 19, 8 411 490 Bytes, mit der Größe im Rahmenkopf und der XXH64-Prüfsumme des Rahmens) in diesem Ordner; die ELF selbst liegt nicht im Quellbaum.
**SHA-256:** `f0415aab6ded1cdf875418b6683ab56aeae3721492af99fba71b08c2c351f82f` (der entpackten ELF; gleich `SHA256SUMS` der Veröffentlichung, steht in `ps5-dump-forge-0.5.6-ps5.elf.sha256`)

## Verwendung

Dump Forge ist ein eigenes Programm (Rust), das unter anderem ein **Debug-FPKG** (`.pkg`) aus einem PS5-Spielordner oder einem Abbild
schreibt (Kompressionsstufen `fast`, `balanced`, `smallest`, `stored`). Die App benutzt es als Motor des Konvertierformats „FPKG
(Debug-.pkg)“ (Browser-App und Vollbild-App):

- **In der App eingebettet (seit 21.11.2026, Wunsch des Users: wer nur die ELF benutzt, soll nichts laden müssen):** `tools/pack-dumpforge.sh` lädt die Datei über
  `tools/fetch-dumpforge.sh` (feste SHA-256), packt sie mit `zstd -19` (17,7 MiB auf 8,0 MiB; zlib hätte 10,2 MiB ergeben) und prüft den Rahmen durch Entpacken gegen die SHA-256;
  `tools/gen_zstd_blob.py` legt den Rahmen unverändert in `gen/dumpforge_blob.c` (`Makefile`, `DF_ZST`), die ELF der App wächst um 8,0 MiB (23,0 MiB). Die App entpackt ihn bei
  jedem Auftrag im Speicher (`ZSTD_decompress`), vergleicht Größe, ELF-Kennung und SHA-256 und sendet die Datei von dort an den ELF-Lader (`src/dumpforge.c`, `unpack_built_in`);
  auf die Konsole wird nichts geschrieben. Die Kacheln tragen keine eigene Kopie mehr (`tools/make_starter.sh`); ihr Ordner `dumpforge/` wird nur noch gelesen, falls ein älterer
  Bau ihn gefüllt hat.
- **Eine neuere Fassung hat Vorrang:** „Neue Fassung von GitHub laden“ (Browser, im Konvertieren-Fenster; vorher „Dump Forge laden“, das mit dem Einbau nicht mehr erschien; `POST /api/v1/dumpforge {"action":"fetch"}`) holt die neueste Veröffentlichung nur auf Knopfdruck von GitHub (HTTPS, die
  Prüfsumme wird mit `SHA256SUMS` der Veröffentlichung verglichen) nach `/data/PS5-Cooling-Center/dumpforge/`; wer sie selbst laden will, legt `ps5-dump-forge-<Version>-ps5.elf` in diesen
  Ordner. Es zählt die höchste Version aus der eingebauten Datei, dem Ordner der App und den Ordnern der Kacheln; bei gleicher Version geht eine Datei im Ordner der App vor der eingebauten,
  eine Datei ohne Version im Namen ist älter als die eingebaute. `GET /api/v1/dumpforge` nennt die Quelle (`source`: `built_in` oder `folder`).
- Für einen Auftrag sendet die App die ELF an den lokalen ELF-Lader (127.0.0.1:9021), schickt `POST /api/start_job` an Dump Forge
  (Port 8095), fragt `GET /api/jobs` nach Fortschritt und Stufen (scan, preflight, check, plan, compress, write, verify, finalize),
  bricht mit `POST /api/cancel_job` ab und beendet das Programm danach mit `POST /api/quit` (`src/dumpforge.c`, `src/gameconvert.c`).
- Dump Forge legt bei jedem Start eine Kachel `PDFG00001` an (`crates/ps5-dump-forge-server/src/tile.rs`). Die App will keine: Vor dem Start legt sie
  `/user/app/PDFG00001` als leere Datei an, die Dump Forge in Ruhe lässt („is not a folder“); eine früher angelegte Kachel entfernt sie mit der Deinstallation
  der Konsole und löscht die Besitzer-Datei `/data/dumpforge/tile-PDFG00001.owner` (`src/tilekeep.c`).
- Seit 0.5.6 (16.11.2026, Vergleich `v0.5.5...v0.5.6`): bringt ampr-emu-ng v0.4.1 mit (vorher v0.3.4), behält beim Umwandeln eines `.pkg` die nur im Container verschlüsselt liegenden `nptitle.dat` und `npbind.dat` (Trophäen meldeten sich bei einem kleinen Teil der Pakete nicht an), schreibt Packs mit mehr als 32 Teilen auf eine gekoppelte PS5 (beide Seiten brauchen 0.5.6) und zeigt unter Linux jede eingehängte Platte als Wurzel. Die Schnittstelle, die die App benutzt (`/api/start_job`, `/api/jobs`, `/api/cancel_job`, `/api/quit`), hat sich im Vergleich nicht geändert.
- Seit 19.11.2026 schickt die App auch Aufträge, die ein Abbild mit gepacktem Spiel schreiben: `format` `exfat` oder `ffpkg` mit `lz4: "pack"` (`lz4_level` 0 oder 3 bis 12, `lz4_runtime` `ng` oder `ampr_emu`) oder `zstd_pack: true` (`zstd_level` 1 bis 22), ohne `kraken_level`; Dump Forge misst und schreibt im Strom, ohne Zwischenordner (Stufen scan, preflight, measure, write, verify, finalize). Das verlangt Dump Forge 0.5.6 oder neuer (`src/dumpforge.c`, `ps5tm_dumpforge_lacks_image_pack`; `src/gameconvert.c`).
- Das Programm bleibt unverändert; aus seinem Quelltext ist nichts in diese App übernommen.

## Hinweise des Autors (aus `ps5-dump-forge convert --help`)

- Der Name der Ausgabe darf ohne Endung höchstens 63 Bytes haben.
- `balanced` und `smallest` machen das Paket etwa 2,6 % kleiner, brauchen aber etwa das 6- und 9-fache der Zeit, alle Kerne rechnen
  durchgehend; `stored` komprimiert nichts (viel größeres Paket), für eine Konsole, die ein komprimiertes nicht startet.
- Mit `--full-verify` wird jedes Byte der Ausgabe noch einmal gelesen; sonst prüft es den Aufbau, kleine Dateien ganz und vom Rest eine Stichprobe.
