# DLC-Module (dlc_emu) – eingebettete Fassungen

Die drei Dateien hier sind die „nolog“-Bauten von **dlc_emu** 0.3 (drakmor, GPL-3.0, <https://github.com/drakmor/dlc_emu>,
Veröffentlichung „dlc_emu 0.3“ vom 28.06.2026); `LICENSE` ist der Lizenztext aus demselben Paket, `dlc_emu.example.ini` das
Beispiel dazu. Sie werden von `tools/gen_emu_blob.py dlc` gepackt in die App eingebettet (`gen/dlc_blob.c`) und von der Zeile
„Spiel-Plugins“ (`src/plugins.c`) nur in ein Spiel gelegt, dessen Ordner eine `dlc_emu.ini` enthält; ohne diese Datei würden die
Module die echten DLC verdecken, deshalb werden sie nie einzeln hinzugefügt. Die Module lesen `/app0/dlc_emu.ini` und zeigen die
dort aufgeführten DLC als gekauft. Die Liste ersetzt die der Konsole (nichts wird gemischt); die Datei darf höchstens 32 KiB und
1024 Einträge haben.

Bezogen am 13.10.2026 mit Erlaubnis des Users von <https://github.com/drakmor/dlc_emu/releases/download/0.3/dlc_emu_0.3.zip>
(93.638 Byte, SHA-256 `c9f353ae4c7951de…`, Ordner `nolog/`); geprüft: SELF-Kopf (`4F 15 3D 1D`) und die Kennung `dlc_emu` in
jeder Datei. Die Prüfsummen stimmen mit denen überein, die V-10 (exFAT2AMPR) für dieselbe Veröffentlichung festhält.

| Datei | Größe | SHA-256 |
|---|---|---|
| libSceAppContent.sprx-0.3-nolog | 32.982 | 53e485e22970c693… |
| libSceGameUpdate.sprx-0.3-nolog | 9.126 | 1e31ddbe4809b7c5… |
| libSceNpEntitlementAccess.sprx-0.3-nolog | 43.286 | 3f1826f20e174ff0… |

Die Fassungen 0.1 und 0.2 kennt `src/plugins.c` an ihrer Prüfsumme (Tabelle nach V-10, exFAT2AMPR) und ersetzt sie durch 0.3 (die
alte Datei bleibt als `.old`).
