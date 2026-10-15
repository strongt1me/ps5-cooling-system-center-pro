# PS4Mount 0.5.1 (gasb07)

Quelle: <https://github.com/gasb07/ps4mount>, Archiv `ps4mount-0.5.1.zip` (vom User am 15.10.2026 bereitgestellt; davor 0.5.0 vom selben Tag, 0.4.4 vom 14.10. und 0.4.1 vom 13.10.), Lizenz GPL-3.0-or-later
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
| `test/unit.c` | `third_party/ps4mount/test/unit.c` | Vorsatz; 125 Prüfungen, laufen ohne Konsole |

Zusätzlich übernommen (15.10.2026), mit „(app)“ und „PS4Mount PR #18“ im Code markiert: die Korrekturen aus dem noch nicht
zusammengeführten Pull Request <https://github.com/gasb07/ps4mount/pull/18> (EzioRamesha, „Stability fixes“) für die Teile, die
die App vom Original nutzt: PlayGo-Tabellen in 64 Bit prüfen (in 32 Bit lief die Summe über und die Tabelle wurde weit außerhalb
der Datei gelesen), kein `memset` auf NULL bei einer unsinnigen PKG-Größe, ein Puffer ohne Speicher wird nie halb geschrieben
(auch die Einstellungen nicht), Einträge nur innerhalb der Datei lesen, `param.sfo`-Einträge vor dem Zeiger prüfen, ein
beiseitegelegtes Update wird nicht vom zweiten in derselben Sekunde überschrieben. Nicht übernommen: Log-Kürzung (die App
rotiert `log.txt` selbst), Zeitlimits des Webservers (die App hat einen eigenen), Titel-ID-Prüfung im Webserver (die App prüft
selbst).

Test: `clang -Igen -Isrc -o unit third_party/ps4mount/test/unit.c src/ps4m_build.c src/ps4m_crypto.c src/ps4m_pkg.c && ./unit`
(vorher `python tools/gen_p4m_templates.py gen/pdb_template.h`).
