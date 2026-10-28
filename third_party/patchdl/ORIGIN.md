# PatchDL 0.3.9 (Knutwurst)

Quelle: <https://github.com/knutwurst/ps5-patchdl>, Quelltext des Tags `v0.3.9` (gelesen am 28.10.2026, davor `v0.3.8` am 27.10.2026; davor bis 0.3.4 nur als
Vorbild für Regeln und Zahlen, siehe `THIRD_PARTY_NOTICES.md`). Lizenz GPL-3.0; das Projekt selbst steht unter GPL-3.0-or-later
(`LICENSE` im Hauptordner). Portiert ist nur, was die Spiel-Updates der App brauchen; der Rest von PatchDL (eigener Webserver mit
libmicrohttpd, libcurl/OpenSSL, Kacheln, Cover-Zwischenspeicher, Einstellungsseite) entfällt, die App hat dafür ihre eigenen Teile.

| Original (0.3.9) | Hier | Änderung |
|---|---|---|
| `patchdl_websrv.c`: `write_local_manifest` (0.3.5) | `src/updates.c`: `ps5tm_updates_pkg_manifest` | baut die Paketliste bei jeder Installation neu (cJSON statt `jbuf`), aus der unverändert abgelegten offiziellen Liste `update.manifest.json`; die Stücke zeigen auf den Paket-Server der App (`/stream/install/p/<Versatz>/<Größe>/<Name>`), die Prüfdatei auf `/stream/install/<Name>.crc` |
| `patchdl_websrv.c`: `/api/pkg/<tid>/<off>/<size>/<file>` | `src/pkgstream.c`: `serve_one` (Paketstück) | im vorhandenen Paket-Server der App (Bereichsanfragen, Abdeckung, Lesefehler wie beim ganzen Paket) |
| `patchdl_install.c`: Reihenfolge `manifest`, dann `loop` | `src/pkginstall.c`: `install_run` | erst die Paketliste; lehnt die Konsole sie ab, im selben Auftrag das Paket selbst (einmal, zählt nicht als Versuch); danach Warten auf das Abholen der Prüfdatei (höchstens 60 s) |
| `patchdl_websrv.c`: `doc_field`, `origin_from_block`, `find_in`, `sc_offset_from_json`, `installed_origin`, `package_origin`, `update_fits_installed` (0.3.6/0.3.7) | `src/updates.c`: gleiche Namen, `fit_check` | liest `/user/app/<Titel-ID>/app.json` und `app.pkg` direkt statt über `patchdl_scan_*` |
| `patchdl_websrv.c`: `ensure_local_manifest` (0.3.7) | `src/updates.c`: `ensure_manifest` | nur aus der Warteschlange der App, Vergleich mit der Größe des Pakets |
| `patchdl_websrv.c`: `do_replace_install`, `replace_thread` (0.3.6), Downgrade (0.3.8), PS4-Downgrade, `replace_old_gone`, `REMOVE_WAIT_SECS` (0.3.9) | `src/updates.c`: `reinstall_action`, `fresh_allowed`, `fresh_room`, `fresh_install`, `old_gone`, `k_remove_wait_s` | dieselben Prüfungen (Inhalts-ID mit `-<Titel-ID>_`, Platz mit der alten Spielgröße aus `originalFileSize` bzw. bei PS4 der Größe von `patch.pkg`, zwei Entfernen-Versuche mit 60 s und 300 s, der zweite unter ShellCores Kennung); dazu: Bestätigung mit eingetipptem Spielnamen, Ablehnung bei laufendem Spiel, Markierung `fresh.json` zum Fortsetzen nach einem Abbruch, Updates des Spiels nach einem Downgrade in der App aus |
| `patchdl_install.c`: `patchdl_install_uninstall`, `patchdl_install_uninstall_patch`, `uninstall_call` (0.3.9) | `src/helper/pkginst_helper.c`: `PKGI_OP_UNINSTALL_GAME`, `PKGI_OP_UNINSTALL_PATCH`, `uninstall_call`; `src/pkginstall.c`: `ps5tm_pkginst_uninstall_game`, `ps5tm_pkginst_uninstall_patch` | im Installationshelfer der App; `sceAppInstUtilAppUnInstall` bzw. `…UnInstallPat`, auf Wunsch für den einen Aufruf unter ShellCores Kennung (`0x3800000000000010`), danach zurück |
| `patchdl_scan.c`: `patchdl_scan_app_installed`, `patchdl_scan_patch_size` (0.3.9) | `src/updates.c`: `app_installed`, `ps4_patch_size` | dieselben Ordner (`/user/app`, `/mnt/ext0|1/user/app`, `…/user/patch`) |
| `patchdl_websrv.c`: `handle_download_url`, Schalter pro Spiel (0.3.9) | `src/updates.c`: `download_url_action` | der Schalter hält nur aus „Alle aktualisieren“ heraus; ein Link von Hand geht weiter (nur bei offiziell installierten Spielen) |

Nicht übernommen: `fetch_cover`/`cover_from_pkg` (die App liest das Bild bei der Installation selbst aus dem Paket),
`hold_title_locked` (die App hält das Spiel über `fresh.json` in der Liste), das Mitwandern der Spieleliste zwischen den Reitern und
`pkg_log_note` (die App schreibt ins eigene Protokoll).
