# APR-EMU (libSceAmpr.sprx) – eingebettete Fassungen

Die sieben Dateien hier sind Bauten von **ampr_emu** (drakmor, GPL-3.0-or-later,
<https://github.com/drakmor/ampr_emu>) und von **ampr-emu-ng** (quer3q, GPL-3.0,
<https://github.com/quer3q/ampr-emu-ng>, eine Weiterentwicklung von ampr_emu); sie werden von `tools/gen_emu_blob.py`
gepackt in die App eingebettet (`gen/ampr_emu_blob.c`) und dort nur auf Wunsch in den Ordner
`/data/PS5-Cooling-Center/AMPR_EMU` gelegt oder in ein Spiel eingebaut (`src/amprlib.c`). Quelltext der Programme: die
Projekte oben (ampr_emu: Veröffentlichung 0.4.2.1 vom 06.09.2026; ampr-emu-ng: Veröffentlichungen v0.4.1 vom 15.11.2026 und v0.3.5 vom 11.11.2026; v0.4.0 vom 13.11.2026 war eine Zeit lang eingebettet).

ampr_emu: bezogen am 11.10.2026 von der Veröffentlichungsseite des Projekts (Anhänge der Veröffentlichungen), vom
Nutzer bereitgestellt; geprüft: SELF-Kopf (`4F 15 3D 1D`) und Kennung `[AMPR_EMU]` in jeder Datei.
`libSceAmpr.sprx-0.4.2.1-test-pack` ist byte-gleich mit der Laufzeit „0.4.2.1 PackedStdio“ im LZ4-Werkzeug
(theghostonline, PS5 LZ4 Compression 1.3.0).

ampr-emu-ng v0.4.1: am 15.11.2026 von der Veröffentlichungsseite geladen (`gh release download v0.4.1`: die beiden Anhänge und `SHA256SUMS`),
SHA-256 mit `SHA256SUMS` verglichen: gleich; geprüft: SELF-Kopf und Kennung `ampr-emu-ng 0.4.1` in beiden Dateien. Namen hier
`libSceAmpr.sprx-0.4.1-ng` und `-ng-debug`. Neu in 0.4.1 laut Veröffentlichung: eine neue Dateiindex-Ansicht (jeder Spielindex liegt als eine kompakte
Tabelle im Speicher, ohne die feste 48-MiB-Grenze; der Indexspeicher hat 80 MiB statt 64 MiB), `/data/ampr_cache` erscheint nicht mehr im
Dateimanager; Korrekturen: große Spiele, die mit „index tables need … MiB, over the 48 MiB“ nicht starteten (Final Fantasy XVI, Demon's Souls,
Returnal), Absturz in Spider-Man: Miles Morales, Hänger bei „Closing…“ und nicht anlegbare Spielstände (Spielstand-Einhängen), Ladehänger in
Horizon Forbidden West, Ordner-Handles unter 1024 und ein Ordner, der von einer gleichnamigen Datei in anderer Schreibweise verdeckt wurde. Die
Paketformate (zstd, LZ4) sind unverändert; `docs/zstd-format.md` nennt neu die Regeln, die ein Packer für `ampr_emu.index` einhalten muss.

ampr-emu-ng v0.4.0 (13.11.2026) war vom 13. bis 15.11.2026 eingebettet. Es hatte bekannte Fehler (die feste 48-MiB-Grenze für Indexe: große Spiele wie
Final Fantasy XVI, Demon's Souls und Returnal starteten nicht; Absturz in Spider-Man: Miles Morales; Spielstände, die sich nicht anlegen ließen; ein Ladehänger in
Horizon Forbidden West); v0.4.1 behebt sie. Auf Wunsch des Users (15.11.2026) bleibt deshalb als zweite Fassung die letzte fehlerfreie stabile, v0.3.5, statt
v0.4.0 (Ausnahme von der Regel „die zwei neuesten“).

ampr-emu-ng v0.3.5: am 11.11.2026 von der Veröffentlichungsseite des Projekts geladen (`gh release download v0.3.5`: die beiden Anhänge)
und mit der `SHA256SUMS` der Veröffentlichung und den Prüfsummen von GitHub verglichen: gleich; geprüft: SELF-Kopf und
Kennung `ampr-emu-ng 0.3.5` (statt `[AMPR_EMU]`) in beiden Dateien. `libSceAmpr.sprx` der Veröffentlichung heißt hier
`libSceAmpr.sprx-0.3.5-ng`, `libSceAmpr-debug.sprx` heißt `libSceAmpr.sprx-0.3.5-ng-debug` (so liest `src/amprlib.c`
die Namen). Die Paketformate (zstd, LZ4) sind gegenüber 0.3.4 unverändert (Laufzeit-Korrekturen: Spielstände beim
Neustart eines Spiels über einen vorhandenen Stand, ruhigerer Lauf).

| Datei | Größe | SHA-256 (Anfang) | Bauart |
|---|---|---|---|
| libSceAmpr.sprx-0.4.1-ng | 733.104 | 9c7c41b3216bc4f877d6 | NG, liest LZ4- und zstd-Pakete, ohne Protokoll; neue Indexansicht (80 MiB Indexspeicher) |
| libSceAmpr.sprx-0.4.1-ng-debug | 855.408 | f82e26aa9306f142a4120eb1 | NG, liest LZ4- und zstd-Pakete, mit Protokoll |
| libSceAmpr.sprx-0.3.5-ng | 751.504 | 998f2029b250d954725f | NG, liest LZ4- und zstd-Pakete, ohne Protokoll |
| libSceAmpr.sprx-0.3.5-ng-debug | 869.168 | a76c0336d667b486ba1a | NG, liest LZ4- und zstd-Pakete, mit Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-pack | 423.350 | 69e6c4d5e4f5fb83 | liest LZ4-Pakete, ohne Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-debug-pack | 633.094 | b44a986f2fa9903e | liest LZ4-Pakete, mit Protokoll (die Mitschnitt-Funktion legt diese Datei ein) |
| libSceAmpr.sprx-0.4.2.1-test-nopack | 303.878 | 0aadd31dbe72c080 | ohne Paketunterstützung |

Die Fassungen 0.3.6.6 (`test`, `test-debug`, ältere Fassung) lagen bis zum 05.11.2026 ebenfalls bei; sie sind für den
Platz von NG entfallen (Wunsch des Users, 06.11.2026). NG 0.3.1 (vom 05.11.2026) war am selben Tag eingebettet und wurde
von 0.3.2 abgelöst, NG 0.3.2 (vom 06.11.2026) am 07.11.2026 von 0.3.3 und NG 0.3.3 am 10.11.2026 von 0.3.4 und NG 0.3.4 am 11.11.2026 von 0.3.5. Seit dem 13.11.2026 (Wunsch des Users) stehen immer die zwei neuesten NG-Fassungen zur Auswahl: 0.4.1 kam, 0.4.0 blieb, 0.3.5 ging; am 15.11.2026 (Wunsch des Users, 0.4.0 hatte bekannte Fehler) kam 0.3.5 wieder an die Stelle von 0.4.0; kommt eine neuere, geht die älteste. Weitere Fassungen legt man im Fenster „APR & LZ4“ mit
„Datei hinzufügen“ in den Ordner AMPR_EMU. Mehr Fassungen einzubetten kostet Platz im ELF (die fünf: rund 1,1 MB gepackt;
die Nutzlast sparsam halten; eine harte Grenze bei 10 MB gibt es nicht, siehe Test vom 11.11.2026).
