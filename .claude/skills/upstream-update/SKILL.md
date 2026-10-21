---
name: upstream-update
description: Prüft täglich (oder auf Zuruf), ob die Fremdbausteine der PS5-App neuere Fassungen haben (PS4Mount, ShadowMountPlus, ampr_emu, pgo_stub, dlc_emu, PatchDL, Payload Manager, SDK, MkPFS, libdeflate, LZ4, cJSON …), liest, was neu ist, und arbeitet es in Code, Handbuch (6 Sprachen), FAQ, CHANGELOG und Hinweise ein. Zu benutzen bei „aktualisiere die App“, „gibt es Updates für ps4mount/shadowmountplus/…“, „Fremdbausteine prüfen“ und für die tägliche Prüfung.
---

# Fremdbausteine prüfen und einarbeiten

Antworten und Doku-Texte auf Deutsch, Commit-Nachrichten englisch. Arbeitsordner ist der Projektordner `PS5_Temperature_Manager_src`.

## Eiserne Regeln des Projekts

- **Nichts ohne neues „Ja“ des Users:** kein `git commit`/`push`/Tag/Release, kein Senden an die Konsole (ELF 9021, eboot.bin FTP 2121). Alles andere (lesen, ändern, bauen, testen am Host) darf direkt geschehen; am Ende fragen „sende die ELF und eboot.bin an die PS5?“ bzw. „committen/veröffentlichen?“.
- **Der Name „Sony“ kommt in Oberfläche, Meldungen, Wörterbüchern, Handbuch, FAQ, README und Doku nicht vor** (neutral: „offizielle Server“, „offiziell signiert“). Ausnahmen nur: Dateinamen im Quelltext, Zertifikatsname im Vertrauensanker.
- **Lizenzen:** GPL-Quellen dürfen übernommen werden (Quelle und Fassung in `third_party/<x>/ORIGIN.md` und `THIRD_PARTY_NOTICES.md` nennen). Quellen ohne Lizenz (PS5 Game Compressor) und V-10s exFAT2AMPR (Zusatzbedingung „Made by V-10“) nur als Wissen, nie als Code.
- Keine Fremddateien der Konsole (Systembibliotheken, Firmware, Fakelibs) ins Projekt oder nach GitHub.
- Vorschau vor Gestaltungsänderungen: erst Screenshot zeigen, erst nach „Ja“ bauen.
- SMP-Schnittstelle (Port 10101) nie ohne Absprache anfassen; `POST /api/v1/debug-log` ist die Ausnahme (nur lesend).
- Die Nutzlast der App darf nicht auf ~10 MB wachsen (die Konsole lud 10,5 MB nicht): neue eingebettete Dateien nur sparsam.
- Windows-Bau: `bash tools/build-windows.sh` (LLVM 21, SDK 0.44); Starter: `bash tools/make_starter.sh PS5_Cooling_Center.elf` (WSL). Senden-Skripte (ELF per Socket und eboot per FTP) **in zwei getrennten Aufrufen**, sonst blockt der Virenschutz.

## Ablauf

1. **Prüfen:** `python tools/upstream_check.py --json` (Liste der Bausteine: `tools/upstream.json`; „NEU“ = auf GitHub neuer als `current`). Nichts neu → eine Zeile „alles aktuell“ ausgeben und enden (kein Bau, keine Datei anfassen).
2. **Je neuem Baustein** die Zeile `how`/`where` aus der Ausgabe lesen und:
   - Release-Notizen lesen (`notes`), bei Quellprojekten den Unterschied: `gh api repos/<repo>/compare/<alt>...<neu>` (Dateiliste, dann `patch` der Dateien, die die App betreffen).
   - Entscheiden, was die App berührt. Faustregel: Dateiformate, Pfade, Schnittstellen und Regeln übernehmen; Dinge, die die App selbst anders löst (Webserver, Protokolldatei, Dämon), nicht.
   - Einarbeiten: Code, dann Handbuch (`tools/i18n/docs/docs_src.<xx>.json` **und** `web/handbuch*.html`, alle sechs Sprachen), `docs/HANDBUCH.md`, bei Bedarf FAQ, `third_party/<x>/ORIGIN.md`, `THIRD_PARTY_NOTICES.md`, `docs/ENTWICKLUNG.md`, `CHANGELOG.md` (Abschnitt „Unveröffentlicht“, Unterabschnitt „Fremdbausteine“).
   - JSON-Quellen nach jeder Änderung mit `python -c "import json,glob;[json.load(open(f,encoding='utf-8')) for f in glob.glob('tools/i18n/docs/docs_src.*.json')]"` prüfen. Bei Python-Patches mit `\n`: im Heredoc `chr(92)+'n'` benutzen, nicht `'\\n'` (wird zerlegt).
3. **Bauen:** `bash tools/build-windows.sh` — es muss ohne Fehler und Warnungen durchlaufen (`-Werror`). Bei geändertem Starter `tools/make_starter.sh`.
4. **Stand eintragen:** `current` in `tools/upstream.json` auf die eingearbeitete Fassung setzen (nur für Bausteine, die wirklich fertig eingearbeitet sind).
5. **Bericht (kurz, deutsch):** je Baustein „alt → neu, was geändert, was nicht übernommen und warum“; offene Punkte; die Frage nach Senden/Commit/Release.

## Einarbeiten je Baustein (Kurzfassung, Einzelheiten in `tools/upstream.json`)

- **ps4mount (gasb07):** `src/ps4m_core.c` ist der Port von `src/main.c`; `src/ps4m_pkg|build|crypto.c` aus `pkg.c|build.c|crypto.c`. Übernommen werden Änderungen an Suche/Anmeldung/Marker/Einhängen; nicht übernommen `logts.c` (eigenes Protokoll), `web.c`, `db.c`. `VERSION`-Zeichenkette und Kopf der Datei mitziehen.
- **ShadowMountPlus (drakmor):** Release-ZIP + Quell-ZIP liegen oft vom User in `Downloads`; sonst Quellcode des Tags per `gh`. Prüfen: README (Backport/fakelib/emulators), `docs/api.md`, `include/sm_api_protocol.h`, `src/sm_path_utils.c`, `config.ini.example`. App-Stellen: `src/smp.c`, `src/library.c` (`ps5tm_backport_dir`, `overlay_backports`), `src/amprlib.c` (`probe_backport`, `al_emus_t`), Handbuch AMPR-/Backport-Kapitel. Die ELF selbst sendet man nur auf ausdrücklichen Wunsch an die Konsole.
- **ampr_emu / pgo_stub / dlc_emu (drakmor):** Anhänge laden, SELF-Kopf `4F 15 3D 1D` und Kennung (`[AMPR_EMU]`, `playgo_stub.dat`, `dlc_emu`) prüfen, ablegen als `<lib>.sprx-<version>-<variante>`, `tools/gen_emu_blob.py` baut sie gepackt ein. Handbuch: Versionstabelle im AMPR-/PlayGo-Kapitel.
- **PatchDL (Knutwurst):** nur Regeln/Zahlen (`src/upd_sony.c`, `upd_http.c`, `updates.c`): Hosts, Schlüssel der PS4-Adresse, Puffer- und Blockgrößen, Prüfsummen. **Direktdownload only** — kein SC-Link, keine Prospero/Orbis-Abfrage (vom User ausdrücklich gestrichen).
- **Payload Manager (itsPLK):** Regeln für `src/paystore.c` / `payloads.c` (Versionsvergleich, Prozessregel).
- **MkPFS (PSBrew):** `src/conv_pfs.c`/`conv_exfat.c` bleiben formatgleich; Änderungen an Block-/Inode-Layout, Schwellen (5 %-Regel) oder Verifizierung nachziehen; Hinweise zu Kompressoren (ISA-L-Fehler #132) in die Doku.
- **libdeflate / LZ4 / cJSON:** Dateien tauschen, Makefile-Liste unverändert; LZ4 nur mit Absprache (die APR-EMU liest dessen Format).
- **SDK (ps5-payload-dev):** nur mit Absprache; alte Fassung sichern, `tools/check_sdk_firmware.py` muss Firmware bis 13.60 bestätigen.

## Tägliche Ausführung

Die geplante Aufgabe `ps5-upstream-update` startet diesen Skill einmal am Tag. Sie darf prüfen, einarbeiten, bauen und berichten — und endet dort. Committen, Senden und Veröffentlichen bleibt dem User vorbehalten.
