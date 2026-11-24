# APR-EMU (libSceAmpr.sprx) – eingebettete Fassungen

Die sieben Dateien hier sind Bauten von **ampr_emu** (drakmor, GPL-3.0-or-later,
<https://github.com/drakmor/ampr_emu>) und von **ampr-emu-ng** (quer3q, GPL-3.0,
<https://github.com/quer3q/ampr-emu-ng>, eine Weiterentwicklung von ampr_emu); sie werden von `tools/gen_emu_blob.py`
gepackt in die App eingebettet (`gen/ampr_emu_blob.c`) und dort nur auf Wunsch in den Ordner
`/data/PS5-Cooling-Center/AMPR_EMU` gelegt oder in ein Spiel eingebaut (`src/amprlib.c`). Quelltext der Programme: die
Projekte oben (ampr_emu: Veröffentlichung 0.4.2.1 vom 06.09.2026; ampr-emu-ng: Veröffentlichungen v0.5.1 vom 23.11.2026 und v0.5.0 vom 20.11.2026; v0.4.1 vom 15.11.2026 war vom 15. bis 24.11.2026 eingebettet, v0.3.5 vom 11.11.2026 war vom 15. bis 20.11.2026 die zweite Fassung, v0.4.0 vom 13.11.2026 war eine Zeit lang eingebettet).

ampr_emu: bezogen am 11.10.2026 von der Veröffentlichungsseite des Projekts (Anhänge der Veröffentlichungen), vom
Nutzer bereitgestellt; geprüft: SELF-Kopf (`4F 15 3D 1D`) und Kennung `[AMPR_EMU]` in jeder Datei.
`libSceAmpr.sprx-0.4.2.1-test-pack` ist byte-gleich mit der Laufzeit „0.4.2.1 PackedStdio“ im LZ4-Werkzeug
(theghostonline, PS5 LZ4 Compression 1.3.0).

ampr-emu-ng v0.5.1: am 24.11.2026 von der Veröffentlichungsseite geladen (`gh release download v0.5.1`: die beiden Anhänge und `SHA256SUMS`; Tag-Commit
`1847fde3774f830607f5a512aaae5b3fd12be6e8`), SHA-256 mit `SHA256SUMS` verglichen: gleich; geprüft: SELF-Kopf (`4F 15 3D 1D`) und Kennung
`ampr-emu-ng 0.5.1` in beiden Dateien. Namen hier `libSceAmpr.sprx-0.5.1-ng` und `-ng-debug`. Neu in 0.5.1 laut Veröffentlichung: schnellere Lese-Stapel (der
nächste Stapel eines Spiels beginnt, während der vorige noch endet; im pkg-Modus etwa 10 % schneller, ein zstd-Spiel mit ungünstig gereihten Lesevorgängen von
98 auf 5 ms je Stapel; 8 statt 6 Lesefäden); Korrekturen im pkg-Modus: Spiele, die nie speichern konnten (Marvel's Wolverine, #116: die Laufzeit reicht das
Spielstand-Symbol selbst weiter, auch bei Spielen auf der M.2), Hades hing beim Beenden (0xa0024301, #114), DIRT 5 stürzte nach 15 s ab (#117: Ordner, die mit
kleinem Puffer gelesen werden, zeigen auch ihre Dateien auf der Platte), MARVEL Cosmic Invasion (#111) und Scott Pilgrim EX (ein nur in den Paketen bestehender
Ordner meldet am Ende seiner Liste kein veraltetes „nicht gefunden“ mehr). **FPKG-Unterstützung bleibt laut Veröffentlichung experimentell.** Paketformate und
Indexspeicher sind unverändert (`rt/index_view.rs` nicht geändert; in `rt/index_memory.rs` ist nur der Speicher der Debug-Fassung 32 KiB kleiner, das trifft den
Zwischenspeicher, nicht den Index), die App rechnet für 0.5.1 wie für 0.5.0 (`src/indexlimit.c`).

ampr-emu-ng v0.5.0: am 20.11.2026 von der Veröffentlichungsseite geladen (`gh release download v0.5.0`: die beiden Anhänge und `SHA256SUMS`; Tag-Commit
`d20210e224871864bee0888193c7c1062890639c`), SHA-256 mit `SHA256SUMS` verglichen: gleich; geprüft: SELF-Kopf (`4F 15 3D 1D`) und Kennung
`ampr-emu-ng 0.5.0` in beiden Dateien (die Debug-Datei trägt zusätzlich die Kennung `ampr-emu-ng debugpack_format`). Namen hier `libSceAmpr.sprx-0.5.0-ng` und
`-ng-debug`. Neu in 0.5.0 laut Veröffentlichung: bis zu 20 % schnellere Lesezugriffe (Fäden holen eine Leseanforderung früher ab, der Lesepfad hat weniger
Schritte, der Zwischenspeicher behält die meistgenutzten Daten, wenn ein Spiel durch viele Dateien läuft); weniger Speicher (150,89 statt 184,44 MiB; der
Zwischenspeicher beginnt bei 32 MiB und wächst in den Platz, den der Dateiindex nach dem Start frei lässt); die Dateiindex-Ansicht lädt 30 bis 40 % schneller;
Korrekturen: Final Fantasy XVI (Absturz im Spiel, Hänger bei Dalimil, Absturz beim Start bei einem Abzug ohne `ampr_emu.index`; #84), Marvel's Avengers (Absturz
beim Start; #103), Demon Slayer (Start; #105), Grand Theft Auto V (Start; #88), auf Firmware 4.xx zurückportierte Spiele, die nie speicherten (#92), und ein Spiel,
das speichert, bekommt keinen schreibgeschützten Spielstand-Einhängepunkt mehr, den es schon hält. **FPKG-Unterstützung ist laut Veröffentlichung
experimentell** (ein pkg-Spiel kann schlechter laufen als in zstd-Paketen). Die Paketformate (zstd, LZ4) sind unverändert; der Indexspeicher bleibt bei 80 MiB (die
Dateizeilen der Ansicht sind ein 32-Byte-Block wie die Spalten in 0.4.1, `rt/index_view.rs` des Tags v0.5.0), die App rechnet für 0.5.0 deshalb wie für 0.4.1
(`src/indexlimit.c`). Dank der Veröffentlichung an strongt1me für die Ursache des Startabsturzes von Final Fantasy XVI (#84) und viele Auswertungen von Berichten.

ampr-emu-ng v0.4.1 (vom 15.11.2026, eingebettet bis 24.11.2026, mit v0.5.1 abgelöst, die beiden Dateien sind entfernt): damals von der Veröffentlichungsseite geladen (`gh release download v0.4.1`: die beiden Anhänge und `SHA256SUMS`),
SHA-256 mit `SHA256SUMS` verglichen: gleich; geprüft: SELF-Kopf und Kennung `ampr-emu-ng 0.4.1` in beiden Dateien. Namen hier
`libSceAmpr.sprx-0.4.1-ng` und `-ng-debug`. Neu in 0.4.1 laut Veröffentlichung: eine neue Dateiindex-Ansicht (jeder Spielindex liegt als eine kompakte
Tabelle im Speicher, ohne die feste 48-MiB-Grenze; der Indexspeicher hat 80 MiB statt 64 MiB), `/data/ampr_cache` erscheint nicht mehr im
Dateimanager; Korrekturen: große Spiele, die mit „index tables need … MiB, over the 48 MiB“ nicht starteten (Final Fantasy XVI, Demon's Souls,
Returnal), Absturz in Spider-Man: Miles Morales, Hänger bei „Closing…“ und nicht anlegbare Spielstände (Spielstand-Einhängen), Ladehänger in
Horizon Forbidden West, Ordner-Handles unter 1024 und ein Ordner, der von einer gleichnamigen Datei in anderer Schreibweise verdeckt wurde. Die
Paketformate (zstd, LZ4) sind unverändert; `docs/zstd-format.md` nennt neu die Regeln, die ein Packer für `ampr_emu.index` einhalten muss.

ampr-emu-ng v0.4.0 (13.11.2026) war vom 13. bis 15.11.2026 eingebettet. Es hatte bekannte Fehler (die feste 48-MiB-Grenze für Indexe: große Spiele wie
Final Fantasy XVI, Demon's Souls und Returnal starteten nicht; Absturz in Spider-Man: Miles Morales; Spielstände, die sich nicht anlegen ließen; ein Ladehänger in
Horizon Forbidden West); v0.4.1 behebt sie. Auf Wunsch des Users (15.11.2026) blieb deshalb vom 15. bis 20.11.2026 als zweite Fassung die letzte fehlerfreie stabile, v0.3.5,
statt v0.4.0 (Ausnahme von der Regel „die zwei neuesten“); mit v0.5.0 ist v0.4.1 die zweite Fassung und die Ausnahme endet.

ampr-emu-ng v0.3.5 (vom 11.11.2026) war vom 11. bis 20.11.2026 eingebettet (als `libSceAmpr.sprx-0.3.5-ng` und `-ng-debug`; geprüft wie die anderen: SELF-Kopf, Kennung,
`SHA256SUMS`); mit v0.5.0 am 20.11.2026 abgelöst, die beiden Dateien sind entfernt. Die Namen der eingebetteten Dateien bildet `src/amprlib.c` aus
`libSceAmpr.sprx-<Fassung>-ng` und `-ng-debug`.

| Datei | Größe | SHA-256 (Anfang) | Bauart |
|---|---|---|---|
| libSceAmpr.sprx-0.5.1-ng | 706.976 | 7ca61afc444f0b44d2f0 | NG, liest LZ4- und zstd-Pakete, ohne Protokoll; schnellere Lese-Stapel, Spielstände im pkg-Modus |
| libSceAmpr.sprx-0.5.1-ng-debug | 835.280 | b7b269f5f6ac0f60e414 | NG, liest LZ4- und zstd-Pakete, mit Protokoll |
| libSceAmpr.sprx-0.5.0-ng | 715.264 | 7379e49953d8f14f26b4 | NG, liest LZ4- und zstd-Pakete, ohne Protokoll; schnellere Lesezugriffe, weniger Speicher |
| libSceAmpr.sprx-0.5.0-ng-debug | 844.080 | 3b8c5c6fc6c168d75298 | NG, liest LZ4- und zstd-Pakete, mit Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-pack | 423.350 | 69e6c4d5e4f5fb83 | liest LZ4-Pakete, ohne Protokoll |
| libSceAmpr.sprx-0.4.2.1-test-debug-pack | 633.094 | b44a986f2fa9903e | liest LZ4-Pakete, mit Protokoll (die Mitschnitt-Funktion legt diese Datei ein) |
| libSceAmpr.sprx-0.4.2.1-test-nopack | 303.878 | 0aadd31dbe72c080 | ohne Paketunterstützung |

Die Fassungen 0.3.6.6 (`test`, `test-debug`, ältere Fassung) lagen bis zum 05.11.2026 ebenfalls bei; sie sind für den
Platz von NG entfallen (Wunsch des Users, 06.11.2026). NG 0.3.1 (vom 05.11.2026) war am selben Tag eingebettet und wurde
von 0.3.2 abgelöst, NG 0.3.2 (vom 06.11.2026) am 07.11.2026 von 0.3.3 und NG 0.3.3 am 10.11.2026 von 0.3.4 und NG 0.3.4 am 11.11.2026 von 0.3.5. Seit dem 13.11.2026 (Wunsch des Users) stehen immer die zwei neuesten NG-Fassungen zur Auswahl: 0.4.1 kam, 0.4.0 blieb, 0.3.5 ging; am 15.11.2026 (Wunsch des Users, 0.4.0 hatte bekannte Fehler) kam 0.3.5 wieder an die Stelle von 0.4.0; am 20.11.2026 kam 0.5.0, 0.4.1 blieb, 0.3.5 ging; am 24.11.2026 kam 0.5.1, 0.5.0 blieb, 0.4.1 ging; kommt eine neuere, geht die älteste. Weitere Fassungen legt man im Fenster „APR & LZ4“ mit
„Datei hinzufügen“ in den Ordner AMPR_EMU. Mehr Fassungen einzubetten kostet Platz im ELF (die fünf: rund 1,1 MB gepackt;
die Nutzlast sparsam halten; eine harte Grenze bei 10 MB gibt es nicht, siehe Test vom 11.11.2026).
