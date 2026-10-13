# PlayGo-Stub (libScePlayGo.sprx) – eingebettete Fassungen

Die zwei Dateien hier sind Bauten von **pgo_stub** 0.6 (drakmor, GPL-3.0, <https://github.com/drakmor/pgo_stub>, Veröffentlichung
„PlayGo emu 0.6“ vom 10.10.2026); sie werden von `tools/gen_emu_blob.py playgo` gepackt in die App eingebettet
(`gen/playgo_blob.c`) und dort nur auf Wunsch in den Ordner `/data/PS5-Cooling-Center/PlayGo` gelegt oder in ein Spiel
eingebaut (`src/amprlib.c`, dieselben Regeln wie bei der APR-EMU-Datei) oder von der Zeile „Spiel-Plugins“ auf dem neuesten
Stand gehalten (`src/plugins.c`). Der Stub meldet einem Spiel, dessen PlayGo-Inhalte nicht (mehr) zu finden sind, alles als
vollständig installiert; er lädt nichts und prüft nichts. Die Fassung 0.6 behebt den Umgang mit leeren und NULL-PlayGo-Listen,
synchronisiert den PlayGo-Zustand und prüft Chunks und Orte strenger.

Bezogen am 13.10.2026 mit Erlaubnis des Users von <https://github.com/drakmor/pgo_stub/releases/download/0.6/> (`libScePlayGo.sprx`
18.150 Byte, `libScePlayGo_log.sprx` 23.702 Byte); geprüft: SELF-Kopf (`4F 15 3D 1D`) und die Kennung `playgo_stub.dat` in jeder
Datei. Die Prüfsumme der „nolog“-Datei stimmt mit der überein, die V-10 (exFAT2AMPR) für dieselbe Veröffentlichung festhält.

| Datei | Größe | SHA-256 | Bauart |
|---|---|---|---|
| libScePlayGo.sprx-0.6-log | 23.702 | 7595924889d67c33… | schreibt `/app0/playlgo.log` (jeden PlayGo-Aufruf) |
| libScePlayGo.sprx-0.6-nolog | 18.150 | f5fdb0ed6cc7413e… | ohne Protokoll |

Die älteren Fassungen 0.5 (nolog `645caa46…`, log `305e6e11…`) und 0.4 (nolog `a0f20f40…`, log `39c508a8…`) kennt `src/plugins.c`
an ihrer Prüfsumme und ersetzt sie durch 0.6 (die alte Datei bleibt als `.old`); sie sind nicht mehr eingebettet. Eine Datei
`/app0/playgo_stub.dat` im Spiel ist optional und stellt Abschnittszahl und Sprachen ein (Aufbau: README von pgo_stub). Weitere
Fassungen legt man im Fenster „LZ4-Asset-Packs & APR-EMU“ mit „Datei hinzufügen“ in den Ordner PlayGo.
