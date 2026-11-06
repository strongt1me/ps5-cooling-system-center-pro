# APR-EMU (libSceAmpr.sprx) – eingebettete Fassungen

Die fünf Dateien hier sind Bauten von **ampr_emu** (drakmor, GPL-3.0-or-later,
<https://github.com/drakmor/ampr_emu>) und von **ampr-emu-ng** (quer3q, GPL-3.0,
<https://github.com/quer3q/ampr-emu-ng>, eine Weiterentwicklung von ampr_emu); sie werden von `tools/gen_emu_blob.py`
gepackt in die App eingebettet (`gen/ampr_emu_blob.c`) und dort nur auf Wunsch in den Ordner
`/data/PS5-Cooling-Center/AMPR_EMU` gelegt oder in ein Spiel eingebaut (`src/amprlib.c`). Quelltext der Programme: die
Projekte oben (ampr_emu: Veröffentlichung 0.4.2.1 vom 06.09.2026; ampr-emu-ng: Veröffentlichung v0.3.2 vom 06.11.2026).

ampr_emu: bezogen am 11.10.2026 von der Veröffentlichungsseite des Projekts (Anhänge der Veröffentlichungen), vom
Nutzer bereitgestellt; geprüft: SELF-Kopf (`4F 15 3D 1D`) und Kennung `[AMPR_EMU]` in jeder Datei.
`libSceAmpr.sprx-0.4.2.1-test-pack` ist byte-gleich mit der Laufzeit „0.4.2.1 PackedStdio“ im LZ4-Werkzeug
(theghostonline, PS5 LZ4 Compression 1.3.0).

ampr-emu-ng: am 06.11.2026 von der Veröffentlichungsseite geladen (`gh release download v0.3.2`), Prüfsummen gleich
denen der `SHA256SUMS` der Veröffentlichung; geprüft: SELF-Kopf und Kennung `ampr-emu-ng 0.3.2` (statt `[AMPR_EMU]`) in
beiden Dateien. `libSceAmpr.sprx` der Veröffentlichung heißt hier `libSceAmpr.sprx-0.3.2-ng`, `libSceAmpr-debug.sprx`
heißt `libSceAmpr.sprx-0.3.2-ng-debug` (so liest `src/amprlib.c` die Namen).

| Datei | Größe | SHA-256 (Anfang) | Bauart |
|---|---|---|---|
| libSceAmpr.sprx-0.3.2-ng | 697.072 | 9a8f553a68d4e6e6be11 | NG, liest LZ4- und zstd-Pakete, ohne Protokoll |
| libSceAmpr.sprx-0.3.2-ng-debug | 779.360 | 5e4f1fcbfb83c9beb06d | NG, liest LZ4- und zstd-Pakete, mit Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-pack | 423.350 | 69e6c4d5e4f5fb83 | liest LZ4-Pakete, ohne Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-debug-pack | 633.094 | b44a986f2fa9903e | liest LZ4-Pakete, mit Protokoll (die Mitschnitt-Funktion legt diese Datei ein) |
| libSceAmpr.sprx-0.4.2.1-test-nopack | 303.878 | 0aadd31dbe72c080 | ohne Paketunterstützung |

Die Fassungen 0.3.6.6 (`test`, `test-debug`, ältere Fassung) lagen bis zum 05.11.2026 ebenfalls bei; sie sind für den
Platz von NG entfallen (Wunsch des Users, 06.11.2026). NG 0.3.1 (vom 05.11.2026) war am selben Tag eingebettet und wurde
von 0.3.2 abgelöst (Wunsch des Users: die alte Fassung entfernen). Weitere Fassungen legt man im Fenster „APR & LZ4“ mit
„Datei hinzufügen“ in den Ordner AMPR_EMU. Mehr Fassungen einzubetten kostet Platz im ELF (die fünf: rund 1,1 MB gepackt;
die Nutzlast soll unter etwa 10 MB bleiben).
