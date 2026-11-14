# Dump Forge

**Version:** v0.5.5
**Autor:** quer3q
**Quelle:** https://github.com/quer3q/ps5-dump-forge (Tag `v0.5.5`)
**Lizenz:** GPL-3.0-or-later (Lizenztext im Quellprojekt)
**Datei:** `ps5-dump-forge-0.5.5-ps5.elf` (PS5-ELF, 18 140 240 Bytes), **nicht in diesem Quellbaum und nicht in der App eingebettet**
**SHA-256:** `858b8ae2954341a3389e4a9f998c6a57d01bc4e4fb8f01a71c41719001c30be7` (gleich `SHA256SUMS` der Veröffentlichung)

## Verwendung

Dump Forge ist ein eigenes Programm (Rust), das unter anderem ein **Debug-FPKG** (`.pkg`) aus einem PS5-Spielordner oder einem Abbild
schreibt (Kompressionsstufen `fast`, `balanced`, `smallest`, `stored`). Die App benutzt es als Motor des Konvertierformats „FPKG
(Debug-.pkg)“ (Browser-App und Vollbild-App):

- Die ELF holt die App nur auf Knopfdruck („Dump Forge laden“, `POST /api/v1/dumpforge {"action":"fetch"}`) von der neuesten
  Veröffentlichung auf GitHub (HTTPS, die Prüfsumme wird mit `SHA256SUMS` der Veröffentlichung verglichen) nach
  `/data/PS5-Cooling-Center/dumpforge/`. Wer sie selbst laden will, legt `ps5-dump-forge-<Version>-ps5.elf` (oder `ps5-dump-forge.elf`) in
  diesen Ordner; die neueste Version zählt.
- **Mit den Kacheln:** `tools/make_starter.sh` legt die unveränderte Datei (der Bau lädt sie über `tools/fetch-dumpforge.sh`, feste SHA-256) und diese
  `ORIGIN.md` in den Ordner `dumpforge/` der beiden Kacheln `FAKE86921` und `FAKE86922`; die App sucht auch in `/data/homebrew/<Kachel>/dumpforge/`. Von allen
  Stellen zählt die neueste Version, bei gleicher Version der Ordner der App. Wer nur die ELF der App benutzt, lädt Dump Forge mit dem Knopf oder legt es selbst ab.
- Für einen Auftrag sendet die App die ELF an den lokalen ELF-Lader (127.0.0.1:9021), schickt `POST /api/start_job` an Dump Forge
  (Port 8095), fragt `GET /api/jobs` nach Fortschritt und Stufen (scan, preflight, check, plan, compress, write, verify, finalize),
  bricht mit `POST /api/cancel_job` ab und beendet das Programm danach mit `POST /api/quit` (`src/dumpforge.c`, `src/gameconvert.c`).
- Dump Forge legt bei jedem Start eine Kachel `PDFG00001` an (`crates/ps5-dump-forge-server/src/tile.rs`). Die App will keine: Vor dem Start legt sie
  `/user/app/PDFG00001` als leere Datei an, die Dump Forge in Ruhe lässt („is not a folder“); eine früher angelegte Kachel entfernt sie mit der Deinstallation
  der Konsole und löscht die Besitzer-Datei `/data/dumpforge/tile-PDFG00001.owner` (`src/tilekeep.c`).
- Das Programm bleibt unverändert; aus seinem Quelltext ist nichts in diese App übernommen.

## Hinweise des Autors (aus `ps5-dump-forge convert --help`)

- Der Name der Ausgabe darf ohne Endung höchstens 63 Bytes haben.
- `balanced` und `smallest` machen das Paket etwa 2,6 % kleiner, brauchen aber etwa das 6- und 9-fache der Zeit, alle Kerne rechnen
  durchgehend; `stored` komprimiert nichts (viel größeres Paket), für eine Konsole, die ein komprimiertes nicht startet.
- Mit `--full-verify` wird jedes Byte der Ausgabe noch einmal gelesen; sonst prüft es den Aufbau, kleine Dateien ganz und vom Rest eine Stichprobe.
