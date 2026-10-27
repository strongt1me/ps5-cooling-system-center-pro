# Herkunft von `tvapp/`

Die Vollbild-App für die Konsole (die Kachel von „PS5 Cooling & System Center - Pro“) ist aus
**ps5-homebrew-ui** von BlackBearReloaded gebaut, GPL-3.0-or-later (`LICENSE` hier).

| | |
| --- | --- |
| Original | `github.com/blackbearreloaded/ps5-homebrew-ui` (nicht mehr erreichbar) |
| Verwendete Kopie | <https://github.com/JMUtechnologies/ps5-homebrew-ui>, erhaltene Quellkopie; laut deren `PRESERVATION.md` Stand des Original-Commits `dbf8286661224c7d8b01fdea62052251eec43eca`, Quelltext, Lizenzen und Hinweise unverändert |
| OpenGL-SDK | ps5-opengl 1.0.2 (BlackBearReloaded, GPL-3.0), Archiv `ps5-opengl-sdk-1.0.2.tar.gz` von <https://github.com/JMUtechnologies/ps5-opengl/releases/tag/v1.0.2>, SHA-256 `30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4` – gleich dem im Kit festgeschriebenen Wert (`tools/fetch-opengl-sdk.sh`); Manifest `0d432edc9d648d6e0cccb4d5eb3db8484f10ba4b075b5ccf23edda3251b66360`. Wird von `make deps`/`make opengl` nach `.deps/` geladen und geprüft, liegt nicht im Repository |
| Übernommen am | 27.10.2026 |

## Entfernt

- Alle 21 Beispiel-Designs (`src/concepts/*.cpp` außer `registry.cpp`) und die Komponenten-Galerie (`src/concepts/components/`), dazu ihre Tests (`tests/unit/<design>_test.cpp`) und die Galerie-Fälle in `tests/unit/components_*_test.cpp`
- `docs/media` (erzeugte Bilder), `assets/audio/music` (Musik des Originals), die Startbildschirm-Grafiken des Originals (`sce_sys/pic0.dds`, `pic1.dds`, `snd0.at9`, Quellbilder)
- Die Kachelbilder der Beispiel-App: `sce_sys/icon0.png` ist das Symbol unserer Kachel (`starter/sce_sys/icon0.png`)

`src/demo/` (die erfundenen Beispieltitel) bleibt nur, weil `app::Context` sie erwartet; die App rendert ihre Cover nicht mehr.

## Geändert

- `src/app/shell.cpp`: Die Shell behält weder L1/R1 noch das Touchpad, zeigt kein Banner und kein Info-Fenster – es gibt nur ein Design, und das nutzt L1/R1 für seine drei Seiten.
- `src/main.cpp`: keine Cover der Beispieltitel; die HTTP-Datenquelle wird vor der Shell gestartet.
- `tools/bake-fonts.sh`: Inter, Montserrat und DejaVu Sans Mono mit dem Zeichensatz `european` (Umlaute, ß, „ “, –, °) in einem 2048er-Atlas; die Schriften liegen neu gebacken in `assets/fonts`.
- `Makefile`: Ziel `payload` (bindet die App-ELF ein, `PAYLOAD_ELF`, Standard `../PS5_Cooling_Center.elf`), Suchpfad `build/gen`.
- `tests/unit/concepts_test.cpp`: verlangt mindestens ein Design statt zwei; `tests/unit/sources.txt`: `src/ps5cc/*.cpp` statt der Galerie.

## Neu (eigener Code, GPL-3.0-or-later)

- `src/concepts/ps5cc.cpp` – die Oberfläche: Kühlung, Spiele, Aufträge, Menü, Lade- und Fehlerbildschirm
- `src/ps5cc/data.hpp`, `src/ps5cc/mock_source.cpp` – Datenmodell, erfundene Daten für die PC-Vorschau und die Tests
- `src/platform/ps5/http_source.*` – HTTP zur App auf 127.0.0.1:8086 in einem eigenen Faden (1 MiB Stack); startet die App über den ELF-Loader (127.0.0.1:9021), wenn sie nicht läuft
- `src/platform/ps5/payload_blob.c` – die App-ELF in der Kachel (`.incbin`)
- `src/third_party/cjson/` – cJSON 1.7.19 (MIT), dieselbe Fassung wie in `src/third_party/` der App
- `src/ps5cc/covers.*` – Spiel-Cover: der HTTP-Faden lädt sie einzeln (`/api/v1/library/cover`, das Spiel im Fokus zuerst), dekodiert und verkleinert sie auf 160×160; das Hauptprogramm macht daraus höchstens zwei Texturen je Bild (höchstens 400 Cover, etwa 40 MiB)
- `src/third_party/stb/stb_image.inc`, `image.c`, `image.h` – stb_image v2.30 (Public Domain oder MIT), unverändert vom stb-Commit in `src/third_party/stb/UPSTREAM`, nur PNG und JPEG (seit 27.10.2026 für die Entwicklerbilder der Payloads) und ohne stdio; Git-Blob `9eedabed…53272ec`, SHA-256 `594c2fe3…e4200b3` (vollständig in `THIRD_PARTY_NOTICES.md`)

## Bauen

```bash
make deps                                   # einmal: Payload-SDK, zlib, Testbibliothek
make init TITLE_ID=PPSA86922 APP_NAME="PS5 Cooling & System Center - Pro"
USE_CCACHE=0 make app PAYLOAD_ELF=../PS5_Cooling_Center.elf   # Ordner in dist/PPSA86922
tools/host-snapshots.sh build/snapshots ps5cc                 # Vorschau am PC, Bilder in build/snapshots
make test-unit
```

Nicht mit `-j`: zlib und `libc.prx` kamen sich beim parallelen Bau in die Quere.
