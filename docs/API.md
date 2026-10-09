# Schnittstelle (API)

[README](../README.md) · [Handbuch](HANDBUCH.md) · [API](API.md) · [Entwicklung](ENTWICKLUNG.md)

Die Web-Oberfläche spricht mit der App über eine JSON-Schnittstelle unter
`http://<PS5-IP>:8086/api/v1/`. Alles, was die Oberfläche kann, geht damit auch
aus Skripten. Für Anfragen mit `Origin` gelten die Regeln aus
[HANDBUCH.md](HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz).

## Endpunkte

| Methode | Pfad | Zweck |
| --- | --- | --- |
| GET | `/api/v1/status` | Momentaufnahme (Temperaturen, Lüfter, Adapter) |
| GET/POST | `/api/v1/history` | Temperaturverlauf der letzten 24 Stunden (ein Punkt pro Minute) samt Spitzenwerten; `POST` setzt ihn zurück |
| GET | `/api/v1/cooling-health` | Langzeitauswertung der Kühlleistung je Woche (aus `thermal-health.csv`); seit 1.54.1 zählen nur Wochen unter den jetzt geltenden Einstellungen (Einstellungs-Stempel): zusätzlich `weeks_needed` (4), `since_ms` (seit wann diese Einstellungen gelten) und `older_weeks` (davon ausgeschlossene Wochen) |
| POST | `/api/v1/cooling-health` | „Vergleich neu beginnen“: erhöht den Zähler im Stempel, alle bisherigen Wochen bleiben in der Datei, zählen aber nicht mehr mit |
| GET | `/api/v1/channels` | Aufzeichnung aller SoC-Sensorkanäle samt Last und Lüfterdrehzahl |
| GET/PUT | `/api/v1/config` | Konfiguration lesen / schreiben |
| GET | `/api/v1/config/export` | komplette Konfiguration als JSON herunterladen (inkl. Spielprofile) |
| POST | `/api/v1/config/import` | komplette Konfiguration aus JSON übernehmen (inkl. Spielprofile) |
| POST | `/api/v1/fan/threshold` | Direktwert setzen (`{"threshold_c":55}`): in `automatic` als Zieltemperatur, in `observe` als feste Schwelle |
| POST | `/api/v1/tile/ensure` | Homescreen-Kachel installieren |
| POST | `/api/v1/tile/upload` | Kachel-Paket als Rohdaten hochladen (bis 64 MB, abgelegt als `/data/PS5-Cooling-Center/tile.pkg`) |
| POST | `/api/v1/power` | Konsole ausschalten, neu starten, in den Ruhemodus oder abgesicherten Modus versetzen (`{"action":"off"\|"reboot"\|"standby"\|"safemode"}`); 409 `standby_disabled`, wenn der Ruhemodus in den Energieeinstellungen aus ist |
| GET | `/api/v1/logs` | Ereignisse als JSON |
| GET | `/api/v1/logs/tail?count=25` | letzte Einträge für Live-Anzeige |
| GET | `/api/v1/klog?after=<n>&max=<m>` | Kernel-Log live: die Zeilen nach Nummer `after` (0 = die letzten `max`), höchstens `max` (Vorgabe 500, bis 1000), älteste zuerst. Antwort `{ok, newest, lost, more, lines:[{n, t, s, m?}]}`: `n` Zeilennummer (ab 1, wird nie neu vergeben), `t` Uhrzeit der Konsole in ms, wann die App die Zeile zuerst sah, `s` der Text, `m:1` ein Hinweis der App (etwa „Lücke“) statt einer Kernelzeile; `more`: es warten weitere Zeilen; `lost`: zwischen `after` und der ersten gelieferten Zeile fehlen welche (die App hält einige tausend) oder die Nummern begannen neu. Gelesen wird `kern.msgbuf` (eine Kopie, nicht `/dev/klog`), höchstens zweimal je Sekunde, egal wie viele fragen |
| GET | `/api/v1/klog/files` | der Ordner `/data/PS5-Cooling-Center/klog-live-log` (siehe [Handbuch](HANDBUCH.md#kernel-log-live)): `{ok, dir, count, bytes, files, record}`; `files` neueste zuerst, höchstens 200, je `{name, size, mtime, kind, active}` (`kind` `save` für `klog-….log`, `rec` für `aufnahme-….log`; `active` wahr bei der Datei, die gerade aufgenommen wird); `record` wie bei `/api/v1/klog/record`. Nur Dateien mit diesen beiden Namensformen werden gezählt |
| GET | `/api/v1/klog/file?name=<name>` | eine dieser Dateien als Download (`Content-Disposition: attachment`); 404 für jeden anderen Namen (nur `klog-` und `aufnahme-`, dann Buchstaben, Ziffern, `-`, `_`, und `.log`) |
| POST | `/api/v1/klog/save?tz=<min>` | Text als Auszug ablegen: Der Rumpf (`text/plain`, 1 Byte bis 8 MB) wird als `klog-<Datum>_<Uhrzeit>.log` gespeichert; `tz` ist die Zeitzone des Absenders in Minuten östlich von UTC (Vorgabe 0 = UTC; bestimmt den Dateinamen). Antwort `{ok, name, bytes}`. Ein vorhandener Name wird nie überschrieben (Nummer dahinter) |
| POST | `/api/v1/klog/files/clear` | löscht alle Dateien dieser Namensformen im Ordner, außer der Aufnahme, die gerade läuft; Antwort `{ok, deleted, bytes, kept}`. Unterordner, andere Namen und Verknüpfungen bleiben |
| GET/POST | `/api/v1/klog/record` | `GET` der Stand der Aufnahme: `{ok, recording, file, bytes, lines, max_bytes, elapsed_s, reason}` (`reason` nennt, warum die letzte endete: `angehalten`, `Größe erreicht …`, `Speicher der Konsole knapp`, `Schreibfehler …`). `POST {"on":true,"tz":<min>}` beginnt eine Aufnahme (die Konsole liest das Kernel-Log einmal pro Sekunde und schreibt es in `aufnahme-….log`; 409, wenn schon eine läuft oder weniger als 512 MB frei sind), `POST {"on":false}` beendet sie |
| GET | `/api/v1/playtime?max=<n>` | Spielzeit-Sitzungen (siehe [Handbuch](HANDBUCH.md#spielzeit)): die gespeicherten, neueste zuerst, höchstens `max` (Vorgabe und Grenze 3500), dazu die laufende. Antwort `{ok, now, enabled, live, titles, sessions, total, kept}`: `now` Uhr der Konsole in Sekunden seit 1970; `enabled` ist `false`, wenn die Spielerkennung aus ist (dann wird nichts mitgeschrieben); `live` die laufende Sitzung oder `null`: `{id, name, start, play, state, cpu_max, cpu_avg, soc_max, soc_avg, fan_avg, fan_max, flags}` mit `state` `front` (vorn), `back` (pausiert) oder `gone` (verschwunden, die 45 s Frist läuft); `titles` der Name je Titel-ID der gelieferten Sitzungen; `sessions` je `{id, start, end, play, cpu_max, cpu_avg, soc_max, soc_avg, fan_avg, fan_max, flags}`: Zeiten in Sekunden nach der Konsolenuhr (`end` ist der letzte Augenblick im Vordergrund), `play` die Sekunden im Vordergrund, Temperaturen in °C, Lüfter in % (`-1`: keine Messung), `flags` als Bits (1 Notfallmodus, 2 Temperaturwarnung, 4 Ende geschätzt); `total` die Zahl der gespeicherten Sitzungen (wirklich gezählt, auch wenn `sessions` höchstens die neuesten 3500 liefert), `kept` ihre Obergrenze (3000) |
| POST | `/api/v1/playtime/reset` | löscht die gespeicherten Sitzungen (`sessions.csv`); eine laufende zählt weiter |
| GET | `/api/v1/saves` | Spielstände (siehe [Handbuch](HANDBUCH.md#spielstände-sichern-und-zurückspielen)): was die Konsole hat, wohin gesichert werden kann, welche Sicherungen es gibt. Antwort `{ok, now, console, game_running, game_id, users, drives, backups, unfinished, job}`: `users` je `{uid, name, bytes, titles:[{id, name, installed, platform, bytes, files, mtime}]}` (`platform` `PS4` oder `PS5`; `name` leer und `installed` falsch, wenn der Titel nicht installiert ist; `mtime` die neueste Änderung in Sekunden seit 1970); `drives` je `{mount, label, free_bytes}` (der Konsolenspeicher heißt `/user`, gesichert wird dort unter `/data`); `backups` die 40 neuesten (nach dem Zeitstempel im Ordnernamen), neueste zuerst, je `{created, path, name, mount, label, kind, bytes, files, this_console, users:[{uid, name}], titles:[{uid, id, name, platform, bytes}]}` (`kind` `backup` oder `undo`; `this_console` falsch bei der Sicherung einer anderen Konsole und bei einer, deren Liste keine Konsole nennt); `unfinished` die Zahl der unfertigen Sicherungen (mit der Marke `.ps5cc-unfertig`), die nicht aufgelistet werden; `game_running` wahr, solange ein Spiel läuft, auch pausiert; `job` wie bei `/api/v1/saves/job` |
| GET | `/api/v1/saves/job` | der Vorgang mit den Spielständen. Antwort `{ok, state, active, can_cancel, kind, phase, percent, files_done, files_total, bytes_total, current, error, note, path, undo, ok_files, bad_files, elapsed_s, finished_ago_s}`: `state` `idle`, `scanning`, `copying`, `verifying`, `applying`, `done`, `failed` oder `cancelled`; `kind` `backup`, `verify` oder `restore`; `can_cancel` ist beim Zurückspielen ab dem Umbenennen der Dateien falsch (bis dahin sind alle neuen Dateien nur neben die alten geschrieben und der Vorgang lässt sich folgenlos abbrechen); `path` die Sicherung, die gemacht oder geprüft wird, `undo` die Sicherung des Stands davor; `ok_files`/`bad_files` das Ergebnis von Prüfen und Sichern; `finished_ago_s` Sekunden seit dem Ende, `-1` solange nichts zu Ende ging |
| POST | `/api/v1/saves/backup` | Sicherung starten: `{"target":"/mnt/usb0","users":["10000001"],"titles":["10000001/PPSA01001"]}`. `target` ist ein `mount` aus `drives`; `users` und `titles` (`<Benutzer>/<Titel-ID>`) sind wahlweise, ohne sie wird alles gesichert. Antwort ist der Vorgang. 404 unbekanntes Ziel, Benutzer oder Titel; 400 ein falsch geschriebener Titel; 409 ein Spiel läuft (auch pausiert), ein Vorgang läuft schon oder ein Kopieren, Konvertieren, Verschieben von Spielen oder das Aufteilen eines Pakets |
| POST | `/api/v1/saves/verify` | eine Sicherung prüfen: `{"path":"<path aus backups>"}`; 404, wenn es sie nicht (mehr) gibt; 409 bei laufendem Vorgang |
| POST | `/api/v1/saves/delete` | die Spielstände eines Titels für einen Benutzer löschen: `{"uid":"1ea2f4d9","id":"PPSA01001","mount":"/mnt/usb0"}`; `mount` ist das Ziel der Sicherung des jetzigen Stands (leer: ohne Sicherung). Antwort wie jeder Auftrag (`kind: "delete"`); 400 bei falschen Kennungen, 404 bei unbekanntem Benutzer, Titel oder Ziel, 409 wenn ein Spiel oder ein anderer Vorgang läuft (ab 1.51.0) |
| POST | `/api/v1/saves/restore` | einen Titel zurückspielen: `{"path":…,"user":"10000001","title":"PPSA01001","confirm":true}`. Ohne `"confirm": true` (der Wert `true`, kein Text) 400 `confirm_missing`: Niemand soll mit einer verirrten Anfrage Spielstände ersetzen. 400 bei einem Benutzer oder Titel, der keine Kennung ist, und bei `/saves/backup`, wenn `users` oder `titles` keine Liste aus Texten ist (oder mehr als 16 Benutzer, 400 Titel enthält: nichts wird stillschweigend gekürzt); 404, wenn Sicherung, Benutzer oder Titel fehlen; 409, wenn ein Spiel läuft, ein Vorgang läuft, die Sicherung unfertig ist oder von einer anderen Konsole stammt |
| POST | `/api/v1/saves/cancel` | laufenden Vorgang abbrechen; ab dem Umbenennen der Dateien beim Zurückspielen nimmt die Konsole das nicht mehr an (`can_cancel` falsch). Antwort ist der Vorgang |
| GET | `/api/v1/packages` | die Pakete der letzten Suche (siehe [Handbuch](HANDBUCH.md#pakete-finden-und-aufteilen)): `{ok, scanning, ever, seen, parsed, failed, current, scanned_at, now, drives, packages}`; `ever` falsch, solange noch nie gesucht wurde; `seen` die Zahl der Dateien, die nach einem Paket aussahen, `parsed` die der gelesenen Pakete, `failed` die der Dateien, die keins waren; `drives` je `{mount, label, free_bytes, count}`; `packages` je `{id, path, file, drive, title_id, content_id, name, version, kind, plat, size, total, mtime, has_icon, parts?}` (`id` eine Kennung aus 16 Hex-Zeichen, die nur für die Liste der letzten Suche gilt; `kind` `base`, `update` oder `dlc`; `plat` 5 für PS5, 4 für PS4, 0 unbekannt; bei einem geteilten Paket `file` der ursprüngliche Dateiname, `size` die Teile, die gefunden wurden, `total` das ganze Paket und `parts` `{total, found, missing:[…], complete}`) |
| POST | `/api/v1/packages/scan` | startet die Suche im Hintergrund (läuft eine, bleibt es dabei); Antwort wie `GET /api/v1/packages`, mit `scanning` wahr |
| GET | `/api/v1/packages/icon?id=<id>` | das Bild eines Pakets aus der Liste (PNG, 5 Minuten im Browser zu halten, `X-Content-Type-Options: nosniff`); 404 bei einer Kennung, die die Suche nicht vergeben hat, bei einem Paket ohne Bild und bei einem Bild, das kein PNG ist. Es kommt nie ein Pfad in der Anfrage vor |
| GET | `/api/v1/packages/job` | der Vorgang „Paket aufteilen“: `{ok, state, active, can_cancel, phase, name, file, dir, parts, part, bytes_total, bytes_done, percent, error, note, sha256, elapsed_s, finished_ago_s}`; `state` `idle`, `splitting`, `verifying`, `done`, `failed` oder `cancelled`; `bytes_total` ist doppelt so groß wie das Paket (Schreiben, dann Zurücklesen); `sha256` die Prüfsumme des Pakets nach dem Lesen; `finished_ago_s` Sekunden seit dem Ende, `-1` solange nichts zu Ende ging |
| POST | `/api/v1/packages/split` | ein Paket aufteilen: `{"id":"<id>","target":"/mnt/usb0","part_mb":4095}`; `target` ist ein `mount` aus `drives` (der Konsolenspeicher heißt `/user`, geteilt wird dort nach `/data/pkg`), `part_mb` die größte Größe einer Teil-Datei in MB (64 bis 65536, höchstens 256 Teile). Antwort ist der Vorgang. 400 bei fehlender oder unzulässiger Größe; 404 bei unbekanntem Paket oder Ziel; 409, wenn das Paket schon ein Teil ist oder nicht größer als ein Teil, im Ordner `pkg` des Ziels schon Teile dieses Namens liegen, der Platz nicht reicht, schon ein Vorgang läuft oder ein Kopieren, Konvertieren, Verschieben oder eine Sicherung der Spielstände |
| POST | `/api/v1/packages/cancel` | den laufenden Vorgang abbrechen; die Konsole entfernt, was geschrieben wurde. Antwort ist der Vorgang |
| GET | `/api/v1/packages/install/plan?id=<id>` | was bei der Installation dieses Pakets geschähe (siehe [Handbuch](HANDBUCH.md#pakete-installieren)): `{ok, can_install, blocked, warning, id, name, file, title_id, content_id, version, kind, plat, size, parts, installed, space}`; `blocked` ist leer, wenn nichts im Weg steht, sonst der Grund in Worten (dann ist `can_install` falsch); `warning` ein Hinweis, der nichts verhindert; `size` die Größe des ganzen Pakets (bei einem geteilten die Summe der Teile), `parts` die Zahl der Teil-Dateien (0 bei einer einzelnen); `installed` `{state, version}` (`state` 1: steht in der Datenbank der Konsole, 0: nicht, -1: nicht feststellbar); `space` `{known, free, needed}` in Bytes (der interne Speicher). 404 `packages_unknown` bei einer Kennung, die die letzte Suche nicht vergeben hat. Es kommt nie ein Pfad in der Anfrage vor |
| GET | `/api/v1/packages/install/job` | der Vorgang „Paket installieren“: `{ok, state, active, can_cancel, cancelling, helper_restarts, blind, started, verified, phase, name, file, title_id, content_id, version, kind, plat, bytes_total, bytes_done, bytes_sent, percent, remain_s, promote_percent, system_status, attempt, error, error_code, note, elapsed_s, finished_ago_s}`; `state` `idle`, `preparing`, `starting`, `installing`, `finishing`, `done`, `failed` oder `cancelled`; `bytes_done` das, was die Konsole als gelesen meldet (nie mehr, als die App gesendet hat), `bytes_sent` alles, was der Server ausgeliefert hat (ein Stück, das die Konsole zweimal liest, zählt zweimal, die Zahl kann `bytes_total` übersteigen); `system_status` das Wort der Konsole (`running`, `playable`, …); `attempt` der wievielte Versuch (bis 3); `helper_restarts` wie viele Hilfsprogramme nach dem ersten gestartet wurden (es war weg oder hing; die Installation der Konsole läuft dabei weiter); `blind` wahr, solange sich die Konsole nicht nach dem Fortschritt fragen lässt (kein Hilfsprogramm): `bytes_done` ist dann, was der Server zum ersten Mal ausgeliefert hat, und bei `done` heißt es: alles ausgeliefert, ob die Installation fertig ist, steht nicht fest (`verified` ist dann falsch); `error_code` der Code der Konsole als `0x…`, leer bei allem, was von der App kommt; `cancelling` wahr, wenn das Stoppen verlangt wurde und noch läuft (dann ist `can_cancel` falsch); `started` wahr, sobald die Konsole das Paket angenommen hat (ab da kann sie etwas angelegt haben, das bei einem Abbruch liegen bleibt); `verified` bei `done`: das Ergebnis wurde in der Liste der Konsole gefunden (falsch: „fertig gemeldet, nicht bestätigt“, die Gründe stehen in `note`); `remain_s` ist 0, wenn die Konsole keine Schätzung nennt; `elapsed_s` und `finished_ago_s` laufen nach einer Uhr, die nur vorwärts geht; `note` ein Hinweis (Abbruch, lange Wartezeit, „noch nicht in der Liste“); `remain_s` die Restzeit, die die Konsole schätzt; `promote_percent` der Fortschritt, den die Konsole für das Freischalten zum Spielen nennt; `finished_ago_s` Sekunden seit dem Ende, `-1` solange nichts zu Ende ging |
| POST | `/api/v1/packages/install` | eine Installation starten: `{"id":"<id>"}` (die Kennung aus der Liste; sonst nichts). Antwort ist der Vorgang. Die Konsole prüft dabei noch einmal alles, was der Plan zeigt. 404 bei einer unbekannten Kennung; 409 mit dem Grund in Worten (`install_refused`) bei allem, was im Weg steht: schon installiert, das Spiel läuft, ein anderer Vorgang läuft (Kopieren, Konvertieren, Verschieben, Sichern, Teilen, eine Installation), die Datei hat sich verändert. Gestartet wird nie ohne diesen Aufruf |
| POST | `/api/v1/packages/install/cancel` | die laufende Installation abbrechen (das Hilfsprogramm und der Server werden beendet; was die Konsole schon angelegt hat, bleibt unter Umständen liegen). Antwort ist der Vorgang; läuft keiner, bleibt er `idle` oder bei seinem Ergebnis |
| POST | `/api/v1/packages/live/init` | eine Übertragung vom PC beginnen: `{"name":"x.pkg","size":<Bytes>}`; Antwort ist der Zustand mit `id` (`live-N`), `seg_bytes` (1 MiB), `slots`, `send` (welche Stücke gewünscht sind). 400 bei ungültigem Namen oder unplausibler Größe, 409 wenn schon eine Übertragung oder ein anderer Vorgang läuft, 503 ohne Speicher (ab 1.52.0) |
| PUT, POST | `/api/v1/packages/live/segment?id=<id>&n=<Nr>` | ein Stück (Rohdaten, höchstens 1 MiB, das letzte so lang wie der Rest) in den Ring; Antwort ist der neue Zustand. 400 bei falscher Länge oder Nummer, 404 unbekannt, **410 die Übertragung ist beendet**, 503 `busy` (kein Platz im Ring: kurz warten und noch einmal), 408 wenn das Stück nicht vollständig ankam |
| GET | `/api/v1/packages/live/state?id=<id>[&since=<ver>&wait=<ms>]` | Zustand: `state` (`head`, `parsing`, `ready`, `failed`, `ended`), `send` (wunschgeordnet), `ver`, `bytes_in`, `resident`, nach dem Lesen des Kopfes `title_id`, `content_id`, `title`, `version`, `kind`, `plat`; `error` bei `failed` und `ended`. Mit `since` und `wait` (höchstens 5000 ms) antwortet die Abfrage erst, wenn sich `ver` geändert hat |
| POST | `/api/v1/packages/live/cancel` | die Übertragung beenden (`{"id":"live-N"}`). Installiert wird mit `POST /packages/install` und `{"id":"live-N"}`, der Plan kommt von `GET /packages/install/plan?id=live-N` |
| GET | `/api/v1/qr` | die Adresse dieser Weboberfläche als QR-Code: `{"ok":true,"url":"http://192.168.1.94:8086/","svg":"<svg …>"}`; der Server erzeugt ihn nur für die eigene Adresse und nimmt keinen Text entgegen; 404 `no_address`, wenn die Konsole keine Netzwerkadresse hat (ab 1.52.0) |
| GET | `/api/v1/system` | Modell, Firmware, Laufzeit, RAM, Laufwerke |
| GET | `/api/v1/payloads` | laufende Payloads (Prozesse) |
| POST | `/api/v1/payloads/kill` | ein laufendes Payload beenden (`{"pid":123}`) |
| GET | `/api/v1/payload-files` | Payload-Dateien: Ordner `/data/PS5-Cooling-Center/payloads` und `.elf`-Dateien auf USB-Sticks (Hauptverzeichnis und Ordner `payloads`); je Datei Größe, Datum, `valid` und bei Ungültigen der Grund (ab 1.46.0) |
| POST | `/api/v1/payload-files/start` | Payload an den ELF-Lader (Port 9021) senden: `{"source":"internal"\|"usb","mount":"/mnt/usb0","dir":""\|"payloads","name":"x.elf"}`; Antwort mit Bytes und den ersten Worten des Payloads |
| POST | `/api/v1/payload-files/copy` | von einem USB-Stick in den internen Ordner kopieren (`mount`, `dir`, `name`; vorhandene Dateien werden nie überschrieben) |
| POST | `/api/v1/payload-files/delete` | eine Datei aus dem internen Ordner löschen (`{"name":"x.elf"}`) |
| GET, POST | `/api/v1/payload-profiles` | Payload-Profile: GET liefert `{"startup":"<id>","profiles":[{"id","name","items":["a.elf","!2000"]}]}`; POST ersetzt das ganze Dokument (geprüft: höchstens 24 Profile, 64 Einträge, Einträge sind `.elf`-Namen ohne Pfad oder Pausen `!<ms>` mit 1…600000, Kennungen `a-z0-9-_`; Antwort 400 mit Grund) (ab 1.51.0) |
| POST | `/api/v1/payload-profiles/run` | Profil ausführen (`{"id":"…"}`), 202; 404 unbekannt, 409 läuft schon, 400 leer |
| POST | `/api/v1/payload-profiles/stop` | den laufenden Ablauf nach dem aktuellen Schritt anhalten |
| GET | `/api/v1/payload-profiles/status` | `running`, `id`, `name`, `step`, `total`, `startup`, `results[{item,ok,msg}]` |
| GET | `/api/v1/controller/diag` | Passive Controller/HID-Diagnose (nur Verfügbarkeit) |
| GET | `/api/v1/sensors/risky` | Risky-Telemetrie auf Abruf (Last/Takt/Leistung) |
| GET | `/api/v1/drives` | Optionale Laufwerkstelemetrie (`/dev/daN`, wenn freigeschaltet) |
| GET | `/api/v1/logs/export` | Ereignisse als Textdatei |
| GET | `/api/v1/library` | Spiele des Startbildschirms aus der App-Datenbank der Konsole: Titel, Titel-/Content-ID, Version, Spielzeit, Format/Speicherort, Backport/AMPR/PlayGo (ab 1.46.0) |
| GET | `/api/v1/library/cover?id=PPSA20396` | Cover eines Spiels aus der Liste (PNG, darf zwischengespeichert werden) |
| GET | `/api/v1/library/cache` | der Zwischenspeicher „Covers & Metadaten speichern“ (siehe [Handbuch](HANDBUCH.md#die-seite-spiele)): `{ok, enabled, dir, titles, covers, bytes}`; `dir` ist `/data/PS5-Cooling-Center/covers_and_more`, `titles` die Zahl der Spiele-Ordner darin, `covers` die der Bildkopien, `bytes` ihre Größe. Der Schalter selbst ist das Konfigurationsfeld `library_cache` |
| POST | `/api/v1/library/cache/clear` | leert den Ordner (nur, was die App selbst angelegt hat: Ordner mit einer Titel-ID als Namen, darin `meta.json`, `icon0.png` und deren Zwischendateien). Antwort `{ok, titles, bytes}`; die nächste Liste wird neu gelesen und füllt den Ordner wieder, wenn der Schalter an ist |
| POST | `/api/v1/library/launch` | Start (`{"id":"PPSA20396"}`): Läuft das Spiel schon, wird es nach vorn geholt; läuft keines, startet es direkt wie der Homebrew Launcher. Beides schließt dabei den Browser der Konsole, wenn von dort ausgelöst. Lehnt der Starter ab (etwa ein Abbild, das ShadowMountPlus noch nicht eingehängt hat), kommt eine Meldung mit Taste „Starten“. **Derselbe Titel wird innerhalb von 30 s nie ein zweites Mal gestartet oder angeboten** (`already_starting`): Einen laufenden Titel erneut zu starten, bringt die Konsole laut Elf Arsenal zum Absturz. **Läuft ein anderes Spiel**, wird nichts gestartet: Antwort 409 `other_running` mit `running` und `target` (je `id`, `name`). Ob ein Spiel läuft, fragt die App auch den Kernel (auch pausierte Spiele und solche hinter dem Browser). Mit `{"id":…,"close":"<Titel-ID des laufenden Spiels>"}` beendet die Konsole jenes Spiel im Hintergrund und startet danach das neue (202; Verlauf unter `GET /api/v1/library/close`); die Seite nutzt das seit dem 03.10.2026 nicht mehr, sie beendet mit `/library/close` und startet erst auf den nächsten Tipp |
| GET/POST | `/api/v1/library/close` | `POST {"id":"<laufendes Spiel>"}` beendet das laufende Spiel im Hintergrund (SIGKILL auf dessen `eboot.bin`, wie „Close App“ in Elf Arsenal; nur der Titel, den die Seite nennt; nicht Gespeichertes geht verloren). `GET` meldet den Auftrag: `state` (`idle`, `closing`, `starting`, `done`, `failed`), `closing`, `then`, `message`. `done` erst, wenn das System den Bereich des Spiels (`/mnt/sandbox/<ID>_…`) entfernt hat und eine weitere Sekunde vergangen ist (so wartet auch ShadowMountPlus, bevor es aushängt): Das nächste Spiel startet dann sofort |
| GET | `/api/v1/library/copy/plan?id=PPSA20396` | Kopierplan: Laufwerke und Ziele (A Homebrew, mit Warnung vor ShadowMountPlus-Doppelfund / B Sicherung) |
| GET/POST | `/api/v1/library/copy` | Status / Kopieren starten (`{"id":…,"target":…,"mode":"homebrew"\|"backup"}`) |
| POST | `/api/v1/library/copy/cancel` | laufendes Kopieren abbrechen |
| GET | `/api/v1/library/convert/plan?id=PPSA20396` | Konvertierungsplan: mögliche Zielformate für diesen Titel, `raw_bytes` (Größe als exFAT-Abbild) und `ffpkg_bytes` (Größe des `.ffpkg`; 0, wenn sie sich nicht berechnen lässt, dann nennt `ffpkg_error` den Grund, z. B. einen Ordner mit zu vielen Einträgen), je Ziel, ob der Platz reicht und ob FAT32 die Datei nicht aufnimmt (`fat32_too_big` nach der Größe als exFAT-Abbild, `fat32_too_big_ffpkg` nach der des `.ffpkg`) |
| GET/POST | `/api/v1/library/convert` | Status / Konvertieren starten (`{"id":…,"op":"exfat"\|"ffpkg"\|"ffpfsc","target":…,"mode":"homebrew"\|"backup"}`) |
| POST | `/api/v1/library/convert/cancel` | laufende Konvertierung abbrechen |
| GET | `/api/v1/library/storage/plan?id=PPSA20396&op=move` | Verschiebe-/Entpackplan über ShadowMountPlus (`op` `move` oder `unpack`) |
| GET/POST | `/api/v1/library/storage` | Status / Verschieben oder Entpacken starten (`{"id":…,"op":"move"\|"unpack","target":…,"delete_source":false}`), beides über ShadowMountPlus |
| POST | `/api/v1/library/storage/cancel` | laufendes Verschieben/Entpacken abbrechen |
| POST | `/api/v1/library/delete/plan` | was Löschen täte: Body `{kind:"game"\|"backup", id: Titel-ID oder Pfad einer Sicherung}`; Antwort `{ok, kind (installed\|smp\|backup\|other), id, name, path, format, version, size_bytes, saves_kept, items[], can_delete, why?, confirm?}`; `confirm` ist das Kennwort für den Start (einmal, 10 min) |
| GET/POST | `/api/v1/library/delete` | `GET` der Löschauftrag `{ok, state (idle\|deleting\|done\|failed), active, kind, id, name, phase, error, note, files, bytes, percent, bytes_total, bytes_done, elapsed_s, finished_ago_s}`; `percent` der Fortschritt 0–100 (100 nur bei `done`), `bytes_total`/`bytes_done` die Bytes des laufenden Schritts (Spieldaten oder Sicherung beim Löschen; bei einem installierten Spiel seine Ordner, die die Konsole leert; 0, wenn nichts zu zählen ist); `POST {kind, id, confirm}` startet ihn (403 ohne gültiges Kennwort, 409 wenn es gerade nicht geht) |
| GET | `/api/v1/files/places` | die Orte des Dateimanagers: `{ok, places:[{label, path, free_bytes, writable}]}` |
| GET | `/api/v1/files/list?path=` | ein Ordner (Pfad percent-kodiert): `{ok, path, parent, writable, free_bytes, total, truncated, entries:[{name, type (dir\|file\|dirlink\|link\|other), size, mtime, writable}]}`; höchstens 3000 Einträge |
| GET | `/api/v1/files/download?path=` | eine Datei als Download (nur gewöhnliche Dateien) |
| GET | `/api/v1/files/view?path=&max=` | eine Datei zum Ansehen im Browser (`Content-Disposition: inline`): ein Bild (PNG, JPG, GIF, WebP, BMP, ICO; bis 16 MB, sonst `413`) mit seinem Typ oder der Anfang einer Textdatei als `text/plain` (`max` Bytes, Standard 256 KB, höchstens 1 MB; Kopf `X-Fm-Size` = ganze Größe, `X-Fm-Truncated` = 1, wenn gekürzt). Binärdateien: `415 not_text`. Die Antwort trägt `Content-Security-Policy: default-src 'none'; sandbox` und `nosniff` |
| GET | `/api/v1/files/size?path=` | Größe einer Datei oder eines Ordners samt allem darunter: `{ok, path, bytes, files, folders, partial}`. Die Konsole zählt höchstens 8 Sekunden; ein zu großer Ordner kommt mit `partial: true` und dem bisher Gezählten zurück (untere Grenze). Verknüpfungen werden nicht verfolgt, ein anderes Laufwerk darunter nicht betreten |
| POST | `/api/v1/files/upload?path=&name=` | Rohdaten als neue Datei im Ordner `path` (nie überschreiben; 409 wenn der Name vergeben ist, 403 außerhalb der Laufwerke und `/data`) |
| POST | `/api/v1/files/mkdir` | `{path, name}`: neuer Ordner |
| POST | `/api/v1/files/rename` | `{path, name}`: neuer Name im selben Ordner |
| POST | `/api/v1/files/copy`, `/api/v1/files/move` | `{paths[≤100], dest}`: startet den Auftrag; 409 mit `conflicts[]`, wenn Namen im Ziel vergeben sind, oder bei zu wenig Platz |
| POST | `/api/v1/files/delete/plan` | `{paths}`: `{items, folders, files, bytes, paths, confirm}`; `confirm` ist das Kennwort (einmal, 10 min, genau diese Liste) |
| POST | `/api/v1/files/delete` | `{paths, confirm}`: startet das Löschen (403 ohne gültiges Kennwort) |
| GET | `/api/v1/files/job` | der Auftrag: `{state (idle\|counting\|copying\|deleting\|done\|failed\|cancelled), active, kind (copy\|move\|delete), dest, current, error, note, percent, bytes_total, bytes_done, files_total, files_done, items, items_done, skipped, elapsed_s, finished_ago_s}` |
| POST | `/api/v1/files/job/cancel` | bricht den laufenden Auftrag ab (409 wenn keiner läuft) |
| GET | `/api/v1/library/backups` | die Sicherungen der App: `{ok, backups:[{path, name, drive, type, size_bytes, modified, unfinished, sums}]}` |
| GET/PUT | `/api/v1/games` | Spielprofile (Regeln je Spiel) lesen / schreiben |
| GET | `/api/v1/games/export` | Spielprofile als JSON exportieren |
| POST | `/api/v1/games/import` | Spielprofile aus JSON importieren |
| POST | `/api/v1/profile/username` | Anzeigenamen des Benutzers ändern (`{"name":"…"}`) |
| GET | `/api/v1/profile/avatar/current` | aktuelles Profilbild (PNG) |
| POST | `/api/v1/profile/avatar/file?name=avatar.png` | eine Datei des Profilbild-Satzes als Rohdaten in den Zwischenbereich laden (`avatar64.dds` … `avatar440.dds`, `picture64.dds` … `picture440.dds`, `avatar.png`, `picture.png`, `online.json`) |
| POST | `/api/v1/profile/avatar/apply` | Zwischenbereich als Profilbild übernehmen; vor der ersten Übernahme wird das bisherige gesichert |
| POST | `/api/v1/profile/avatar/restore` | das gesicherte vorherige Profilbild zurückholen |
| GET | `/api/v1/profile/avatar/library` | gespeicherte Avatar-Pakete aus `/data/PS5-Cooling-Center/Avatars` auflisten |
| POST | `/api/v1/profile/avatar/library/save` | aktuelles Avatar-Set als Paket speichern (`{"name":"Mein Avatar"}`) |
| POST | `/api/v1/profile/avatar/library/load` | Paket in den Avatar-Stagingbereich laden (`{"name":"Mein Avatar"}`) |
| POST | `/api/v1/profile/avatar/library/delete` | gespeichertes Paket löschen (`{"name":"Mein Avatar"}`) |

## Kompression und Sprachen

- **Komprimierte Auslieferung.** Die Dateien der Oberfläche (HTML, CSS, JS, JSON, SVG) liegen in der App bereits gzip-komprimiert
  und gehen so an Browser, deren Anfrage `Accept-Encoding: gzip` enthält (Antwort mit `Content-Encoding: gzip` und `Vary:
  Accept-Encoding`). Alle anderen bekommen den Text unkomprimiert: Die App entpackt ihn dazu beim Senden. Bilder (PNG, JPG)
  bleiben unverändert. Die JSON-Antworten der Schnittstelle selbst werden nicht komprimiert.
- **Wörterbücher.** `GET /lang/<xx>.json` (`en`, `it`, `es`, `fr`, `ru`) liefert das Wörterbuch einer Sprache der Oberfläche:
  `{"v":1,"lang":"xx","t":{"deutscher Text":"Übersetzung", …}}`. Texte mit Platzhaltern (`{0}`, `{1}`, …) stehen mit den
  Platzhaltern darin. Für Deutsch gibt es kein Wörterbuch; die Seite wählt die Sprache selbst (gemerkte Wahl, sonst Sprache
  des Browsers, sonst Englisch) und lädt nur das eine Wörterbuch. `GET /handbuch.<xx>.html` und `/faq.<xx>.html` sind Handbuch und
  FAQ in der Sprache (Deutsch ohne Kürzel: `/handbuch.html`, `/faq.html`).

## Konfiguration

Konfiguration liegt unter `/data/PS5-Cooling-Center/config.json`.

Bereiche, die `GET /api/v1/config` meldet (seit 07.10.2026; die Werte setzt die App, nicht der Client): `target_min_c` /
`target_max_c` (Zieltemperatur der Automatik, 60–91 °C), `threshold_min_c` / `threshold_max_c` (Lüfterschwelle, 45–91 °C: der
feste Wert der Betriebsart „Beobachten“ und das, was die Regelung einstellen darf) und `safety_min_c` / `safety_max_c`
(Notfallgrenze, 72–95 °C; sie liegt immer mindestens 4 °C über dem Ziel und wird beim Speichern dorthin angehoben). Werte
außerhalb werden auf den Bereich begrenzt, nicht abgelehnt.

Neue Konfigurationsfelder:

- `warning_countdown_s` (30-900): Dauer bis Eskalationshinweis bei aktiver
   Warnlage.
- `telemetry_retention_days` (7-365): Aufbewahrung für
   `history.csv` und `thermal-health.csv` beim Laden.
- `lightbar_enabled` (0/1): Prospero-Lights-Status aktiv.
- `lightbar_warn_c` / `lightbar_hot_c`: Schwellwerte für GELB/ROT.
- `fan_reapply_sec` (1-300): Reapply-Intervall für die Lüfterschwelle
   (wichtig, weil die Firmware den Wert bei Spielstarts zurücksetzt).
- `library_cache` (0/1, ab Werk 0): „Covers & Metadaten speichern“ auf der Seite Spiele; die
   Liste legt dann Titelbilder und langsam zu ermittelnde Angaben im Ordner
   `/data/PS5-Cooling-Center/covers_and_more` ab und nutzt sie. Teil von Export und Import
   der Einstellungen.

Probe-Maske (`probe_mask`) Bits:

- `0x01` (`PS5TM_PROBE_GAME`): Spielerkennung.
- `0x02` (`PS5TM_PROBE_PAD`): Controller-Akku.
- `0x04` (`PS5TM_PROBE_NETDISP`): Netzwerk und Bildschirm.
- `0x08` (`PS5TM_PROBE_RISKY`): optionale Last-/Takt-/Leistungsabfragen.
- `0x10` (`PS5TM_PROBE_DRIVE`): optionale Laufwerkstelemetrie.
- `0x20` (`PS5TM_PROBE_FPS`): Bilder pro Sekunde.

Zeitlich begrenzte Abfragen („Diagnose 2/5/10 Min."): `PUT /api/v1/config` mit
`probe_mask` und `probe_revert_after_s` (10–900) setzt die Maske sofort, merkt
sich die bisherige und stellt sie nach Ablauf selbst wieder her — auch wenn die
Seite längst geschlossen ist. Auf der Platte steht dabei immer die bisherige
Maske, ein Neustart der App beendet die Diagnose also von selbst. Ein
gewöhnliches Setzen von `probe_mask` bricht sie ab. `GET /api/v1/config` meldet
solange `probe_revert_in_s` und `probe_revert_mask`.

Einstellungen, die abgelehnt werden (`400`/`409`), statt sich unbemerkt auf die
Erreichbarkeit der Oberfläche auszuwirken: `http_port` nur 1024–65535 und nur,
wenn dort kein anderes Programm lauscht (`409 port_in_use`); `bind_address` nur
als IPv4-Adresse.

## Hinweise zu einzelnen Endpunkten

Hinweis zu den neuen Endpunkten:

- `/api/v1/sensors/risky` ist bewusst on-demand und nicht Teil des 1-s-Livepfads.
- `/api/v1/drives` liefert nur dann Daten, wenn `PS5TM_PROBE_DRIVE` aktiv ist;
   sonst antwortet der Endpunkt mit `409`.
- `/api/v1/drives` listet Volumes als Telemetrieobjekte mit Typ-Hinweisen
   (`internal_ssd`, `m2_expansion`, `usb_storage`, `block_da`) und zeigt,
   ob eine Temperaturquelle direkt oder nur als Proxy verfügbar ist.

Kopieren und Konvertieren (`/api/v1/library/copy`, `/api/v1/library/convert`):
Der Status eines Auftrags (GET) enthält seit 03.10.2026 zusätzlich, weil jedes
Ergebnis nach dem Schreiben vom Laufwerk zurückgelesen und geprüft wird:

- Kopie: `checked_bytes` — wie viel der kopierten Bytes schon zurückgelesen und
  mit dem Original verglichen ist. Fortschritt, Tempo (`bytes_per_s`) und
  Restzeit (`eta_s`) zählen beide Durchgänge zusammen; die Arbeit ist doppelt so
  groß wie `total_bytes`. `done_bytes` sagt weiter nur, wie viel kopiert ist.
- Konvertierung: Im Zustand `verifying` (das Ergebnis wird zurückgelesen)
  `check_total_bytes` und `check_done_bytes`, dazu Tempo und Restzeit dieses
  Durchgangs. Bei `ffpkg` umfasst die Summe zwei Durchgänge: die Dateiinhalte
  gegen ihre Prüfsummen, dann die ganze Datei für die SHA-256.
- `sums` — Pfad der Prüfsummen-Datei `<Ziel>.sha256`, sobald sie geschrieben ist
  (Format von `sha256sum`; bei einer Ordner-Kopie je Datei eine Zeile mit dem
  Pfad ab dem Namen des Zielordners). Fehlt sie, steht der Grund in `note`.
- `note` — etwas, das den Auftrag nicht aufgehalten hat, zum Beispiel dass die
  Prüfsummen-Datei sich nicht schreiben ließ.
- Eine abweichende Datei oder ein Fehler beim Zurücklesen beendet den Auftrag mit
  `state: "failed"` und einer Meldung im Feld `error`; das Ergebnis und die
  Prüfsummen-Datei sind dann entfernt.

Controller-Akku in `GET /api/v1/status` (`controller`-Objekt):

- `from_log: true` bedeutet: Der Wert stammt aus dem Kernel-Protokoll
   (last-known), nicht aus einem Live-Poll gegen `scePad`.
- `restored: true` bedeutet: Der Wert wurde nach einem App-Neustart aus
   `pad-battery.txt` wiederhergestellt und in dieser Laufzeit noch nicht durch
   einen neuen Log-Eintrag aktualisiert.
- `age_s` ist die Altersangabe zum Messzeitpunkt und bleibt auch nach Neustart
   konsistent, weil der Zeitstempel mit gespeichert wird.

Die passive Backend-Diagnose aus `/api/v1/controller/diag` wird auch auf der
Systemseite unter **Controller-Diagnose** angezeigt.

Spielzeit-Dateien (`playtime.c`):

- `sessions.csv`: eine Kopfzeile (`# start_s,end_s,play_s,cpu_max,cpu_avg,soc_max,soc_avg,fan_avg,fan_max,flags,title,name`),
   danach eine Zeile je beendeter Sitzung, die älteste zuerst; der Name steht zuletzt und darf Kommas
   enthalten. Neue Zeilen werden angehängt; wächst die Datei über 450 KB, bleiben die letzten 3000.
   Zeilen, die sich nicht lesen lassen (auch solche mit unsinnigen Zahlen oder Titeln, wie eine
   an eine andere geklebte Zeile sie ergibt), und eine noch halb geschriebene letzte Zeile werden
   übergangen. Vor dem Anhängen endet eine Zeile ohne Zeilenumbruch (Rest eines abgebrochenen
   Schreibens) auf eigener Zeile; ein abgebrochener Schreibvorgang lässt keine halbe Zeile zurück.
- `session-open.csv`: eine Zeile mit dem Stand der laufenden Sitzung (`v2,…`, mit Beginn, letzter
   Sichtung, letztem Moment im Vordergrund und der Angabe, ob das Spiel vorn war; ältere `v1,…`-Zeilen
   werden weiter gelesen), alle 30 Sekunden neu geschrieben (erst in eine `.tmp`-Datei, dann
   umbenannt) und beim Ende der Sitzung gelöscht — erst, wenn ihre Zeile in `sessions.csv` steht.
   Ein Rest nach einem Absturz oder Ausschalten wird beim nächsten Start fortgeführt (gleicher Titel
   läuft, zuletzt vor höchstens 180 s gesehen) oder als Sitzung mit `flags` 4 abgeschlossen.

Sicherungen der Spielstände (`savebackup.c`), je ein Ordner `PS5-Sicherung/Spielstaende/<JJJJ-MM-TT_HH-MM-SS>`
(beim Stand vor dem Zurückspielen mit dem Zusatz `_vor-Zurueckspielen_<Titel-ID>`) im Hauptverzeichnis des
Laufwerks (`/mnt/usbN`, `/mnt/extN`) oder unter `/data` (Konsolenspeicher), Ordner `0700`, Dateien `0600`:

- `manifest.json`: `{format: 1, kind, created, app, console, users:[{uid, name}], titles:[{uid, id, platform, name,
   bytes, files:[{p, s, h}]}], system:[{p, s, h}], files, bytes}`. `kind` ist `backup` oder `undo`; `console` Modell und
   maskierte Seriennummer der Konsole, die sie gemacht hat; `p` der Pfad unter dem Ordner, `s` die Größe, `h` die SHA-256
   (hex). Beim Zurückspielen und Prüfen werden die Pfade einzeln gegen eine Liste erlaubter Orte geprüft (kein `..`, nur
   `home/<Benutzer>/(savedata|savedata_prospero|…_meta)/<Titel-ID>/…` und die Datenbankdateien); ein anderer Eintrag
   lässt den Vorgang scheitern, ehe etwas geändert wird.
- `pruefsummen.sha256` im `sha256sum`-Format: Im Ordner `sha256sum -c pruefsummen.sha256` prüft die Sicherung am PC.
- `home/<Benutzer>/savedata/<CUSA…>/`, `home/<Benutzer>/savedata_meta/user/<CUSA…>/`,
   `home/<Benutzer>/savedata_prospero/<PPSA…>/`, `home/<Benutzer>/savedata_prospero_meta/user/<PPSA…>/`: die Ordner
   der Konsole unter `/user/home/<Benutzer>/`, Datei für Datei. `system/<Benutzer>/db/user/savedata.db` und
   `system/<Benutzer>/game_setting.dat` (aus `/system_data/savedata/<Benutzer>/`) gibt es nur bei einer Sicherung, nicht
   beim Stand vor dem Zurückspielen, und sie werden nie zurückgespielt.
- `.ps5cc-unfertig`: die Marke, solange die Sicherung geschrieben wird. Eine Sicherung mit ihr wird weder aufgelistet noch
   geprüft noch zurückgespielt; bricht der Vorgang ab, wird der ganze Ordner entfernt.
- Beim Zurückspielen entsteht zuerst für **jede** Datei `<Name>.ps5cc-neu` neben der alten, geprüft gegen die Sicherung;
   erst wenn alle da sind, werden sie umbenannt. Reste eines abgebrochenen Laufs räumt der nächste Lauf ab. Liste eines Titels
   mit mehreren Einträgen in `titles` (derselbe Benutzer und dieselbe Titel-ID) werden zusammengefasst; eine Datei, die doppelt
   genannt wird, lässt den Vorgang scheitern, ehe etwas geändert wird.

Langzeitdatei `thermal-health.csv`:

- Das Format wurde um eine sechste Spalte erweitert:
   `sum_activity` (Anzahl foreground-aktiver Samples).
- Alte fünfspaltige Dateien bleiben kompatibel und werden weiterhin eingelesen.
- Die Wochenauswertung bevorzugt Wochen mit nennenswerter Aktivität,
   fällt bei zu wenig Daten aber automatisch auf die klassische Auswertung zurück.
