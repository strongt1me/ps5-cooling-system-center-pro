# DLC-Module (dlc_emu) – eingebettete Fassungen

Die drei Dateien hier sind die „log“-Bauten von **dlc_emu** 0.3 (drakmor, GPL-3.0, <https://github.com/drakmor/dlc_emu>,
Veröffentlichung „dlc_emu 0.3“ vom 28.06.2026); `LICENSE` ist der Lizenztext aus demselben Paket, `dlc_emu.example.ini` das
Beispiel dazu. Sie werden von `tools/gen_emu_blob.py dlc` gepackt in die App eingebettet (`gen/dlc_blob.c`) und von der Zeile
„Spiel-Plugins“ (`src/plugins.c`) nur in ein Spiel gelegt, dessen Ordner eine `dlc_emu.ini` enthält; ohne diese Datei würden die
Module die echten DLC verdecken, deshalb werden sie nie einzeln hinzugefügt. Die Module lesen `/app0/dlc_emu.ini` und zeigen die
dort aufgeführten DLC als gekauft. Die Liste ersetzt die der Konsole (nichts wird gemischt); die Datei darf höchstens 32 KiB und
1024 Einträge haben. Die „log“-Bauten schreiben zusätzlich `/app0/dlc_emu.log` in das Spiel.

**Warum „log“ (15.10.2026):** Bis dahin waren die „nolog“-Bauten derselben Veröffentlichung eingebettet. Laut Autor stürzt genau
diese Fassung bei jedem Spielstart ab: Der Ausgabe-Beschreiber des IPC-Aufrufs ist 24 Byte groß, im Quelltext 0.3 aber nur 16, und
die 8 Byte dahinter waren im „nolog“-Bau ein gesichertes Register des Aufrufers (auf der Konsole gemessen; behoben in Commit
48efe06 vom 29.09.2026, zusammengeführt am 10.10.2026, bisher ohne neue Veröffentlichung). Im „log“-Bau liegt an dieser Stelle eine
unbenutzte Variable, er läuft. Selbst bauen lässt sich der Quelltext nur mit Sonys lizenziertem Prospero SDK, deshalb bleibt es bis
zu einer neuen Veröffentlichung bei den „log“-Bauten. Die App erkennt einen „nolog“-Bau 0.3 in einem Spiel an seiner Prüfsumme als
fehlerhaft und ersetzt ihn mit „Aktualisieren“ (die alte Datei bleibt als `.old`).

Bezogen am 13.10.2026 mit Erlaubnis des Users von <https://github.com/drakmor/dlc_emu/releases/download/0.3/dlc_emu_0.3.zip>
(93.638 Byte, SHA-256 `c9f353ae4c7951de…`, am 15.10.2026 erneut gegen die Angabe des Releases geprüft; Ordner `log/`); geprüft:
SELF-Kopf (`4F 15 3D 1D`) und die Kennung `dlc_emu` in jeder Datei. Die Prüfsummen stimmen mit denen überein, die V-10
(exFAT2AMPR) für dieselbe Veröffentlichung festhält.

| Datei | Größe | SHA-256 |
|---|---|---|
| libSceAppContent.sprx-0.3-log | 39.734 | 6ca6d0e10399422e… |
| libSceGameUpdate.sprx-0.3-log | 10.342 | 6a3e65faf2635639… |
| libSceNpEntitlementAccess.sprx-0.3-log | 48.694 | 51871d03aef46163… |

Die Fassungen 0.1 und 0.2 kennt `src/plugins.c` an ihrer Prüfsumme (Tabelle nach V-10, exFAT2AMPR) und ersetzt sie durch 0.3 (die
alte Datei bleibt als `.old`).
