# PS4Mount 0.7.0 (gasb07)

Quelle: <https://github.com/gasb07/ps4mount>, Archiv `ps4mount-0.5.1.zip` (vom User am 15.10.2026 bereitgestellt; davor 0.5.0 vom selben Tag, 0.4.4 vom 14.10. und 0.4.1 vom 13.10.), dazu am 18.10.2026 die Änderungen von 0.5.1 auf 0.6.0 (Quelltext des Tags `v0.6.0`, gelesen und nachgebaut), Lizenz GPL-3.0-or-later
(`LICENSE` hier). Der Quelltext ist unter `src/ps4m_*.c` übernommen:

| Original | Hier | Änderung |
|---|---|---|
| `src/pkg.c`, `pkg.h` | `src/ps4m_pkg.c`, `ps4m_pkg.h` | Funktionen mit Vorsatz `p4m_` |
| `src/build.c`, `build.h` | `src/ps4m_build.c`, `ps4m_build.h` | Vorsatz; `pdb_template.h` wird aus den `.bin` hier erzeugt (`tools/gen_p4m_templates.py`) |
| `src/crypto.c`, `crypto.h`, `src/keys.h` | `src/ps4m_crypto.*`, `src/ps4m_keys.h` | Vorsatz |
| `src/main.c` | `src/ps4m_core.c` (erzeugt aus dem Original, siehe Kopf der Datei) | Suche, Anmeldung, Einhängen, Klog-Ereignisse, Löschfenster und Einstellungen wie im Original; ersetzt sind der Dämon (Einzelinstanz, eigener Webserver, Klog-Socket, Hauptschleife) durch einen Faden der App, die Klog-Zeilen aus `ps5tm_klog_fetch`, die Datenbankabfrage aus dem App-Leser und die Texte (deutsch) |
| `src/logts.c` (neu in 0.5.1) | entfällt | Datum und Uhrzeit am Anfang jeder Protokollzeile schreibt `p4m_printf` in `ps4m_core.c` selbst, im selben Format („2026-10-15 18:08:35.284 “) |
| `src/db.c` | entfällt | Die Abfrage `tbl_contentinfo` liest `ps5tm_library_installed_ids` (ohne SQLite) |
| `src/web.c`, `web/index.html` | entfallen | Die Weboberfläche ist der Reiter „PS4 von USB“ der App (`web/app.js`); die Originalseite liegt als `panel-index.html` zum Vergleich |
| `test/unit.c` | `third_party/ps4mount/test/unit.c` | Vorsatz; 150 Prüfungen, laufen ohne Konsole |

Seit 0.6.1 (21.10.2026, Quelltext des Tags `v0.6.1`, Vergleich `v0.6.0...v0.6.1`): ein unter anderem Namen wiedergekommenes Laufwerk behält seine angemeldeten Spiele (`skip_drive`, `marker_matches` mit gespeichertem Marker, `relocate` in `src/ps4m_core.c`: nur die Verknüpfungen und Dateien werden auf den neuen Ordner umgeschrieben, keine neue Anmeldung); eine Spiel-PKG wird nur aus dem Ordner ihrer eigenen Titel-ID genommen (in der App schon seit PR #18 so). Nicht übernommen: Protokoll unter 10 MB halten (`logts.c`, die App rotiert `log.txt` selbst) und der Hinweis der README auf zusammengeführte PKGs (steht im Handbuch).

Seit 0.6.0 (18.10.2026): **Anmeldung nur der neuen Spiele** (`main.c`: `mask_user_app`, `unmask_user_app`, `unmask_leftover`, `masked_path`, die
Änderungen an `install_all`, `mount_dir` und `unmount_all_layers`). Während `AppInstallAll` zeigt `/user/app` nur die neuen Spiele (ein leerer
Ordner `/system_data/ps4mount/stage` darüber, auf jedem neuen Spiel sein Ordner), damit die Konsole nicht jeden Ordner durchgeht. In der App ist das
**ausgeschaltet** und ein Schalter in den Einstellungen („Neue Spiele schneller anmelden“, Schlüssel `fast_reg=` in `config.txt`), weil es an der
Konsole noch nicht erprobt ist; dazu weist die App Hinzufügen/Entfernen/Zurückverschieben ab, solange `/user/app` verborgen ist (im Original
bedient der Webserver in dieser Zeit niemanden), und räumt beim Start des Fadens auf (`unmask_leftover`). Nicht übernommen: die Änderung an
`logts.c` (der Dämon schloss `stdout`; die App hat ihr eigenes Protokoll) und die Zahlenfelder der Originalseite (die Felder der App sind schon
`type=number`). Diese Teile sind von Hand in `src/ps4m_core.c` eingefügt (mit „0.6.0“ im Kommentar); die Übernahmeskripte der früheren Fassungen
wurden dafür nicht erneuert, die Datei wird seitdem unmittelbar gepflegt.

Zusätzlich übernommen (15.10.2026), mit „(app)“ und „PS4Mount PR #18“ im Code markiert: die Korrekturen aus dem noch nicht
zusammengeführten Pull Request <https://github.com/gasb07/ps4mount/pull/18> (EzioRamesha, „Stability fixes“) für die Teile, die
die App vom Original nutzt: PlayGo-Tabellen in 64 Bit prüfen (in 32 Bit lief die Summe über und die Tabelle wurde weit außerhalb
der Datei gelesen), kein `memset` auf NULL bei einer unsinnigen PKG-Größe, ein Puffer ohne Speicher wird nie halb geschrieben
(auch die Einstellungen nicht), Einträge nur innerhalb der Datei lesen, `param.sfo`-Einträge vor dem Zeiger prüfen, ein
beiseitegelegtes Update wird nicht vom zweiten in derselben Sekunde überschrieben. Nicht übernommen: Log-Kürzung (die App
rotiert `log.txt` selbst), Zeitlimits des Webservers (die App hat einen eigenen), Titel-ID-Prüfung im Webserver (die App prüft
selbst).

Seit 0.7.0 (26.10.2026, Quelltext des Tags `v0.7.0`, Vergleich `v0.6.1...v0.7.0` per `gh api repos/gasb07/ps4mount/compare`;
Archiv `ps4mount-0.7.0.zip` vom User bereitgestellt): Ein gefundenes Update wird jetzt nach Versionsnummer behandelt (`APP_VER` aus
`param.sfo` des schon vorhandenen und des neu gefundenen Pakets): ist das neu gefundene Update **älter** als das benutzte, legt es
sich selbst beiseite statt das benutzte zu verdrängen (vorher ersetzte jedes andere Update das vorhandene, auch ein älteres). Beim
Schließen eines Spiels vor dem Zurückverschieben wartet die App zusätzlich, bis die PFS-Einhängungen des Spiels
(`/mnt/sandbox/pfsmnt/<TID>-app0` u. a.) wirklich freigegeben sind (`pfs_mounts` in `src/ps4m_core.c`), bis zu 20 s — vorher konnte
ein zu früh neu gestartetes Spiel seine eigenen Dateien kurz nicht lesen (Fehler `0x80020060`).

**Nachträglich übernommen (26.10.2026, auf Wunsch des Users):** „Add-ons intern kopieren“ — Add-ons werden statt eingehängt auf
den internen Speicher kopiert (wahlweise mit allen Daten oder nur zur Freischaltung), dazu Update wählen und einzelne Add-ons
an/aus. `accopy.c`/`.h` sind als `src/ps4m_accopy.c`/`.h` mit `p4m_`-Vorsatz übernommen (Dateiname, Funktions- und Typnamen
geändert, sonst unverändert); der rund 1200 Zeilen große Rest aus `main.c` liegt verteilt in `src/ps4m_core.c` (Zustandsautomat
für die Kopien direkt nach `dlc_ready`, `ac_reconcile` nach `addcont_installed`, die Panel-Funktionen nach `find_root`), in
deutscher Sprache für `notify()` und mit `ps4m_`-Vorsatz für die von außen aufgerufenen Funktionen (`ps4m_game_json`,
`ps4m_choose_update`, `ps4m_choose_dlc`; dazu `ps4m_title_managed`, `docs/API.md`). Bewusst nicht übernommen: das Spiel direkt aus
dem Panel starten (`ps4m_launch`/`launch_now`/`launch_after_close`) — die Bibliothek der App deckt das schon ab. Eigene Bedien-
oberfläche: zwei neue Knöpfe („Updates“, „Zusatzinhalte“) in der schon vorhandenen Liste „PS4 von USB“ (`web/index.html`,
`web/app.js`), kein eigener Seitenleisten-Knopf — eine eigene Seite wäre nur dieselbe Liste noch einmal gewesen. Der Schalter
`dlc_copy` liegt bei den übrigen PS4Mount-Einstellungen auf der Spiele-Seite. Zusätzlich, nicht im Original: Die Spiele-Update-Seite (`src/updates.c`)
legt ein heruntergeladenes Update für ein PS4Mount-Spiel jetzt selbst auf dessen Laufwerk (`ps4m_place_update`), statt es wie
jedes andere Spiel zu installieren — das schrieb sonst in einen Ordner, den PS4Mount nie liest (gefunden 26.10.2026, CUSA44880).
Noch nicht übernommen: `logts.c`/`web.c`/`db.c` entfallen wie bisher, der Hinweis zur lokalen IP-Adresse in der Start-Meldung (das
eigenständige `ps4mount.elf` hat einen eigenen Webserver auf Port 8770, die App nicht) und der Sonderfall „dasselbe Spiel auf zwei
Laufwerken“ beim Zurückverschieben (`restore_collect`, `other`-Markierung; seltener Randfall, nicht nachgebaut).

Test: `gcc -Igen -Isrc -o unit third_party/ps4mount/test/unit.c src/ps4m_build.c src/ps4m_crypto.c src/ps4m_pkg.c src/ps4m_origin.c && ./unit`
(vorher `python tools/gen_p4m_templates.py gen/pdb_template.h`; unter WSL, nicht mit dem PS5-Toolchain).
