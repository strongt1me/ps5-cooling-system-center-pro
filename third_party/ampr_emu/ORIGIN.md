# APR-EMU (libSceAmpr.sprx) – eingebettete Fassungen

Die fünf Dateien hier sind Bauten von **ampr_emu** (drakmor, GPL-3.0-or-later,
<https://github.com/drakmor/ampr_emu>); sie werden von `tools/gen_emu_blob.py` gepackt in die App eingebettet
(`gen/ampr_emu_blob.c`) und dort nur auf Wunsch in den Ordner `/data/PS5-Cooling-Center/AMPR_EMU` gelegt oder in ein
Spiel eingebaut (`src/amprlib.c`). Quelltext der Programme: das Projekt oben (Veröffentlichungen 0.3.6.6 vom 23.08.2026
und 0.4.2.1 vom 06.09.2026).

Bezogen am 11.10.2026 von der Veröffentlichungsseite des Projekts (Anhänge der Veröffentlichungen), vom Nutzer
bereitgestellt; geprüft: SELF-Kopf (`4F 15 3D 1D`) und Kennung `[AMPR_EMU]` in jeder Datei. `libSceAmpr.sprx-0.4.2.1-test-pack`
ist byte-gleich mit der Laufzeit „0.4.2.1 PackedStdio“ im LZ4-Werkzeug (theghostonline, PS5 LZ4 Compression 1.3.0).

| Datei | Größe | SHA-256 (Anfang) | Bauart |
|---|---|---|---|
| libSceAmpr.sprx-0.4.2.1-test-pack | 423.350 | 69e6c4d5e4f5fb83 | liest LZ4-Pakete, ohne Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-debug-pack | 633.094 | b44a986f2fa9903e | liest LZ4-Pakete, mit Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-nopack | 303.878 | 0aadd31dbe72c080 | ohne Paketunterstützung |
| libSceAmpr.sprx-0.3.6.6-test | 255.126 | 362c84e31e3a06a4 | ältere Fassung, ohne Protokoll |
| libSceAmpr.sprx-0.3.6.6-test-debug | 414.614 | 27dec32b11d125d6 | ältere Fassung, mit Protokoll |

Weitere Fassungen legt man im Fenster „APR & LZ4“ mit „Datei hinzufügen“ in den Ordner AMPR_EMU. Mehr Fassungen
einzubetten wäre möglich, kostet aber Platz im ELF (die fünf: rund 0,85 MB gepackt).
