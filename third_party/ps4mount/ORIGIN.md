# PS4Mount 0.7.3 (gasb07)

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
| `src/web.c`, `web/index.html` | entfallen | Die Weboberfläche ist der Reiter „PS4 PKG von USB“ der App (`web/app.js`); die Originalseite liegt als `panel-index.html` zum Vergleich |
| `test/unit.c` | `third_party/ps4mount/test/unit.c` | Vorsatz; 150 Prüfungen, laufen ohne Konsole |

Seit 0.7.3 (06.11.2026, Vergleich `v0.7.2...v0.7.3`): bis zu 768 Spiele je Laufwerk (`MAX_TITLES`, vorher 256; darüber meldet `scan_root` die Zahl der nicht hinzugefügten Spiele) und bis zu 64 Suchordner (`MAX_SCAN_PATHS`, vorher 8; `config_load` meldet ignorierte Ordner); die Zusatzinhalte eines Spiels sind nicht mehr auf 64 begrenzt: `dlc_open` hält je Zusatzinhalt nur noch einen kleinen Eintrag (`dlc_pkg_t`: Kennungen, Größe, Prüfsumme, Titel, Version) statt des geparsten Pakets und öffnet das Paket erst beim Einbauen (`place_dlc`); `<TID>.dlcoff` wird so lang gelesen, wie die Datei ist; reine Freischaltungen (ohne Daten) zählen nicht gegen die Grenze von 29 (`dlc_open`, `ps4m_choose_dlc`); ein Kopierstapel fasst höchstens 64 Zusatzinhalte (`AC_MAX_JOBS`, der Rest kommt im nächsten). `src/api.c` reicht die Liste der Zusatzinhalte ohne Zwischenpuffer weiter, damit lange Listen nicht abgeschnitten werden; die Oberflächen (Browser, Vollbild-App) zählen nur Zusatzinhalte mit Daten gegen die Grenze, und „Alle an“ schaltet alle Freischaltungen und die ersten 29 mit Daten ein. Nicht übernommen: `web.c` (größere Anfrage- und Pufferlängen für das Panel; die App bekommt die Liste über ihre eigene Schnittstelle) und die Änderungen am Panel (`web/*.html`) und den Tests des Originals.

Seit 0.7.2 (31.10.2026, Vergleich `v0.7.1...v0.7.2`): Ausschalten hängt vorher jedes Spiel aus (`unmount_games` vor `ps4m_set_enabled` in `src/ps4m_core.c`, nie erzwungen), damit der Ruhemodus kein eingehängtes Spiel vom abgeschalteten Laufwerk findet; läuft ein Spiel, wird abgelehnt und es genannt; bleibt ein Ordner belegt, wird alles wieder eingehängt und abgelehnt. Anders als im Original prüft die App nicht in einer zweiten Abfrage (`/api/stop/check`), sondern die Antwort des Ausschaltens nennt den Grund; ist die Prozessliste nicht lesbar, wird trotzdem (nicht erzwungen) ausgehängt. Nicht übernommen: `web.c`/Panel-Texte und die README-Hinweise (stehen im Handbuch).

Seit 0.7.1 (29.10.2026, Vergleich `v0.7.0...v0.7.1`): `p4m_sfo_check` in `src/ps4m_pkg.c` prüft die Kennungen der param.sfo (TITLE_ID, CONTENT_ID, APP_VER, VERSION und jedes `SERVICE_ID_ADDCONT_ADD_*`, nicht nur die ersten sieben); `src_open` in `src/ps4m_core.c` meldet ein Spiel mit beschädigter param.sfo nie an und lässt ein beschädigtes Update liegen (Meldungen deutsch). Zusätzlich in der App: das beschädigte Spiel steht unter „Auf USB gefunden, nicht eingebunden“ (Grund `damaged`). Die sieben neuen Prüfungen aus `test/unit.c` sind übernommen (157 Prüfungen).

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
oberfläche: zwei neue Knöpfe („Updates“, „Zusatzinhalte“) in der schon vorhandenen Liste „PS4 PKG von USB“ (`web/index.html`,
`web/app.js`), kein eigener Seitenleisten-Knopf — eine eigene Seite wäre nur dieselbe Liste noch einmal gewesen. Der Schalter
`dlc_copy` liegt bei den übrigen PS4Mount-Einstellungen auf der Spiele-Seite. Die Spiele-Update-Seite (`src/updates.c`) legte
ab dem 26.10.2026 heruntergeladene Updates für PS4Mount-Spiele auf deren Laufwerk (`ps4m_place_update`); seit dem 28.10.2026 nicht
mehr: Die Updates der offiziellen Server sind Retail-PKGs, PS4Mount hängt nur Fake-PKGs ein („Retail PKGs are not supported“).
PS4Mount-Spiele werden dort nur noch geprüft, ein Update bringt man als Fake-Update-PKG selbst auf das Laufwerk.
Noch nicht übernommen: `logts.c`/`web.c`/`db.c` entfallen wie bisher, der Hinweis zur lokalen IP-Adresse in der Start-Meldung (das
eigenständige `ps4mount.elf` hat einen eigenen Webserver auf Port 8770, die App nicht) und der Sonderfall „dasselbe Spiel auf zwei
Laufwerken“ beim Zurückverschieben (`restore_collect`, `other`-Markierung; seltener Randfall, nicht nachgebaut).

Test: `gcc -Igen -Isrc -o unit third_party/ps4mount/test/unit.c src/ps4m_build.c src/ps4m_crypto.c src/ps4m_pkg.c src/ps4m_origin.c && ./unit`
(vorher `python tools/gen_p4m_templates.py gen/pdb_template.h`; unter WSL, nicht mit dem PS5-Toolchain).
