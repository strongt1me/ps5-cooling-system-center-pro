# Erweiterungen — laufende Liste

Sammlung möglicher Funktionen für PS5 Cooling & System Center - Pro. Wird fortgeschrieben.
Stand: **02.10.2026**, Programmfassung **1.46.0**.

**Spalte „Aufwand"** ist eine ehrliche Schätzung, keine Werbung.
**Spalte „Sicher?"** sagt, ob die Machbarkeit belegt ist oder nur vermutet.

Status: `[ ]` offen · `[>]` ausgewählt · `[x]` eingebaut · `[-]` verworfen

---

## 0 — Prüfschulden: gebaut, aber nicht belegt

Das Wichtigste zuerst. Diese Punkte kosten keine Entwicklungszeit, nur einen
Test — und ohne sie wissen wir nicht, ob etwas hält. Stand nach dem
Konsolentest vom 24.09.2026 (Einzelheiten im nächsten Abschnitt).

| | Was | Wie zu prüfen |
|---|---|---|
| `[x]` | ~~**Spielerkennung**~~ — **24.09.: erkennt Start, Ende und Title-ID** bei vier Spielen. Dabei zwei Fehler gefunden, behoben in 1.43.1 | — |
| `[x]` | ~~**Zwei zu Unrecht verdächtigte Abfragen**~~ — **24.09.: alle Abfragen an** (`probe_mask` 31), die App überstand vier Spiele und einen Ruhemodus. Netzwerk liefert, Bildschirm nur ohne Spiel | — |
| `[x]` | ~~**SoC-Kanäle 1 und 2**~~ — Hypothese aus dem XDPE14286A-Datenblatt: Kanal 1 = Grafikschiene (VGFX), Kanal 2 = CPU-Schiene (VCORE). **27.09.: widerlegt.** Beide verhalten sich wie Sensoren auf dem Chip. Der Kanal an der Grafik ist **Kanal 7**, siehe „Kanaltest 27.09.2026". Verlauf: 24.09. unter leichter Spiellast alle acht im Gleichschritt (r ≥ 0,98); 26.09. unter fordernder Spiellast Kanal 1 am kühlsten, Kanal 7 am wärmsten; 27.09. bei festem Lüfter folgt nur Kanal 7 der Grafikleistung stärker als das Mittel | — |
| `[x]` | ~~**1.43.1 an der Konsole bestätigen**~~ — ✅ **(a) 24.09., 23:31:** PS-Taste im Spiel → Fokus `0x2007` → 3 s später „kein Spiel" (1.43.0 meldete hier „läuft: Unbekannter Titel (0x2007)"). Neuer Protokolltext aktiv, Bildschirmdaten ohne Spiel weiter da (3840×2160). ✅ **(b) 25.09., 16:31:** Das Spiel (CUSA08519) lief seit 64 Minuten. Der Puffer wurde mit einem Testprogramm künstlich gefüllt (7000 Zeilen, rund 900 KB), bis die Fokuszeile hinausfiel. Die App meldete „läuft", `focus_remembered: true`, Vordergrund, Titel vom Kernel bestätigt. Künstlich deshalb, weil die Konsole nach dem Neustart kaum protokollierte (≈ 24 Byte/s statt 290, die Speicherberichte alle 2 s fehlten). Die Zeile wäre sonst erst nach Stunden gefallen. ✅ **(c) 25.09., 14:58:** Ein beendetes PS5-Spiel steht nicht mehr als „pausiert" da | — |
| `[x]` | ~~**Werkswert der Lüfterschwelle**~~ — ✅ **gemessen am 25.09.2026: 91 °C.** Neustart um 21:05, danach weder Spiel noch Ruhemodus (laut ShadowMountPlus-Protokoll), ShadowMountPlus schreibt nicht. Die um 22:11 gesendete App las beim Start 91 °C. Die Konsole startet also mit demselben Wert, den sie bei jedem Zustandswechsel setzt, und genau den gibt die App seit 1.43.2 beim Abschalten zurück | — |
| `[x]` | ~~**Anzeige per Mikrofon-Taste (1.46.0).**~~ — ✅ **02.10.2026 bestätigt:** Der Doppeldruck des Users auf der Konsole wurde erkannt (`ps5tm_msgbuf_read()` sieht die Zeile, Protokoll `mic_button_shown`: „Prozessor 40 °C, Lüfter 20 %"). Offen bleibt nur, ob die Meldung neben den eigenen „Mikrofon an/aus"-Meldungen der Konsole gut sichtbar war | — |
| `[x]` | ~~**ELF mit den 30 eingebauten Profilbildern (4,4 MB) auf der Konsole starten.**~~ — ✅ **02.10.2026 bestätigt:** Die App startete ohne Warnung, bediente die Seite und lieferte alle 30 Bilder byte-gleich (1,3 MB in 0,9 s). Offen bleibt nur das Anwenden eines Bildes über die Seite (die Umrechnung ist die bisherige) | — |
| `[x]` | ~~**Payload von der Konsole aus starten.**~~ — ✅ **02.10.2026 bestätigt mit der App selbst:** Der Lader nimmt die Verbindung von `127.0.0.1:9021` an, startet die Datei, und das Payload lebt weiter, obwohl die sendende Verbindung abriss (die alte Ausführung wurde vom neuen Start beendet). **Offen:** ein fremdes Payload (z. B. `ftpsrv` oder ein Prüfprogramm) aus Ordner und von USB starten und beobachten, ob es nach dem Schließen unserer Socket-Seite weiterläuft (die App schließt nach höchstens 1,5 s; `nc` macht es ebenso); Kopieren USB → intern an der echten Konsole | ein harmloses fremdes Payload aus der Liste starten, dann unter „Laufende Payloads" nachsehen |

---

## Neu in 1.44.0 — Anzeige per PS-Taste, neue Kachel (26.09.2026)

| | Was | Stand |
|---|---|---|
| `[x]` | **Anzeige per PS-Taste.** Jeder Druck zeigt oben rechts „Prozessor 37 °C · Lüfter 20 %" als System-Meldung, ohne Web-Oberfläche. Ein Payload darf den Controller nicht öffnen. Das System schreibt aber für jeden Druck der PS-Taste eine Zeile `[LoginMgr] [onPSButtonPressed] value={…"ClickCount":0}` ins Kernelprotokoll (beim schnellen Doppeldruck eine zweite mit `"ClickCount":1`). `psbutton.c` zählt diese Zeilen jede Sekunde. Höchstens eine Meldung alle 5 s: Die zuerst gewählten 20 s schluckten acht bewusste Drücke hintereinander. Schalter unter Kühlung, Vorgabe „an" **Seit 1.46.0 liegt die Anzeige auf der Mikrofon-Taste (zweimal drücken), nicht mehr auf der PS-Taste; die Datei heißt jetzt `micbutton.c`, siehe 1.46.0.** | an der Konsole bestätigt |
| `[-]` | **Dauerhafte Anzeige in der Leiste oben rechts** (neben den Einstellungen) — erneut geprüft und weiter verworfen: Das geht nur per Einschleusen in SceShellUI (siehe Projektnotiz „Overlay"), rund 35.000 Zeilen bei etaHEN, firmwareabhängig, ein Fehler reißt den Startbildschirm mit. Neu seit der damaligen Entscheidung: etaHEN läuft auf dieser Konsole nicht mehr, dessen Overlay steht also nicht mehr als Ersatz bereit | — |
| `[x]` | **Neue Kachelbilder** (Icon und Hintergrund, Entwürfe des Users). Die Schrift des Hintergrunds steht nicht mehr hinter der Kachelreihe. Das Icon ist zugleich das Zeichen oben in der Seitenleiste der Web-Oberfläche | installiert und bestätigt |
| `[x]` | **LIESMICH korrigiert:** Sie behauptete, der Installer ersetze eine vorhandene Kachel. Er lässt sie aber stehen. Neue Bilder kommen nur auf die Konsole, wenn die alte Kachel vorher gelöscht wird | — |
| `[x]` | **Makefile:** `WEB_FILES` sah nur `web/*`, nicht die Bilder in `web/img/`. Ein ausgetauschtes Bild landete deshalb nicht im ELF. Jetzt wird `web/img/*` mit beobachtet | — |

---

## Neu in 1.45.0 — Stromschienen, 16 CPUs, Takt live, FPS, Meldungen rechts (27.09.2026)

Grundlage ist die Webrecherche vom 26.09.2026. Der Bericht liegt außerhalb des
Repos unter `PS5 SDK usw/Recherche PS5-GPU 2026-09-26.md`. Die wichtigste
Quelle ist drakmor/ps5-hwinfo (GPL-3.0). Übernommen sind nur Fakten wie
Aufrufe, Pufferaufbau und ioctl-Nummern, kein Code, denn dieses Projekt steht
unter MIT. ⚑ **Die Konsole ist eine PS5 Pro (CFI-7021 B01Y)**, so meldet es die
App selbst. Alle Messwerte hier und im SoC-Kanaltest stammen von diesem Modell.

| | Was | Stand |
|---|---|---|
| `[x]` | **Stromschienen** (`telemetry.c`). `sceKernelGetSocPowerConsumption` hat **ein** Argument und schreibt 0x70 Byte: 8 Schienen × {mW, mV, mA}, dazu 4 Zusatzwörter. Bisher war der Aufruf mit zwei Argumenten deklariert, und das erste Feld wurde als u64 gelesen. Die Plausibilitätsprüfung scheiterte deshalb immer, **die App hat nie einen Verbrauch gezeigt**. Gemessen im Leerlauf: GPU 2,2–2,5 W, CPU+SoC 9,4 W, GDDR6 25–27 W (größter Verbraucher), gesamt 37–39 W. Zusatzwort `u32[25]` = 38000, passt als m°C zur Prozessortemperatur (38 °C). `u32[26]` = 9369 entspricht fast genau der CPU+SoC-Schiene. Die Bytes 96–99 (45/43/42/43) sind ungedeutet. Anzeige: Kacheln in der Übersicht, GPU-Liste, Risikotelemetrie; im Kanal-Ring alle 5 s `gpu_w`/`cpu_w`/`mem_w` und die Zusatzbytes. ⚑ **Nicht alles davon ist live** (eine Spielstunde Assassin's Creed Shadows, 27.09.): Die GPU-Schiene nahm 40 verschiedene Werte an (97–101,6 W), der Speicher bewegte sich leicht. Die beiden CPU-Schienen standen dagegen eine Stunde lang bei exakt 111,4 W, im Leerlauf bei 9,4 W, und die Zusatzwörter blieben ebenso unverändert (67/63/74/74, 63250, 111394). Das ist also kein Messwert, sondern eine Vorgabe des Modus oder ein beim Moduswechsel eingefrorener Wert. Die App zeigt die CPU-Schienen deshalb erst, wenn sie sich dreimal in einer Minute ändern (`power.cpu_live`), und `live_w` enthält sonst nur GPU und Speicher. Die Summe aller acht Schienen (259 W im Spiel) liegt über der Steckdosenleistung einer Pro (214–235 W laut Messungen). Auch das spricht gegen einen echten Messwert | an der Konsole bestätigt |
| `[x]` | **Last aller 16 logischen CPUs.** Leerlauf-Threads `SceIdleCpu0`–`15`; CPU 13 ist `SceIdleCpuRv`. Geprüft: Rv sammelte 1,99 s Leerlauf in 2,1 s, `SceIdleCpu13` 0,00 s. Die eigene CPU-Menge `0xea00` ergibt die Aufteilung Spiel/System. „Kern 1–8" sind jetzt Paare (2k, 2k+1), deren Reihenfolge auf dem Chip ungeklärt ist: Energiemodus-Tabelle und Live-Takt nennen die schnellen vier Kerne in umgekehrter Reihenfolge. Messfenster 2 s | bestätigt |
| `[x]` | **Fehler seit Einführung der Leerlauf-Methode: Zeiteinheit.** Die Thread-Zeiten sind `timeval` (µs), gelesen wurden sie als `timespec` (ns). Nur ganze Sekunden zählten, deshalb zeigte jede CPU 0 oder 100 %. Belegt mit Rohwerten: 18383 s + 184702 µs → 18384 s + 227014 µs → 96 % Leerlauf. Das erste Feld ist die Prozess-ID, bisher `lo_data` genannt | behoben |
| `[x]` | **Fehler: Die Risikotelemetrie störte die Lastmessung.** Ihre Einzelabfrage rechnete Millisekunden nach dem Lüfter-Thread neu. Jetzt misst nur der Lüfter-Thread, die Abfrage bekommt sein letztes Ergebnis | behoben |
| `[x]` | **Wochenstatistik unverändert.** Die Kühlleistungs-Aufzeichnung nimmt weiter das Mittel der CPUs 0–7 über den letzten Schritt (`cpu_load_legacy_pct`). Ihr Fenster 25–75 % ist in dieser Einheit festgelegt, die aufgezeichneten Wochen ebenso | — |
| `[x]` | **Aktivste Threads** (10 s, mit Prozess-ID, „diese App" markiert) in der Risikotelemetrie. Im Leerlauf: SceShellUIMain 11 %, SceJSCdMain 5 %, Audio 3 %. ⚠ Eine frühe Messung mit dem Zeiteinheit-Fehler hatte SceJSCdMain bei 93 % gezeigt, das war falsch | bestätigt |
| `[x]` | **Takt live** über `sceKernelGetSocClock` (26 Bereiche) und `sceKernelGetCpuCoreClock`. Bereich 20 = Grafik (im Leerlauf 500–900 MHz), 25 = Grenze (2340–2348 MHz auf der Pro), 23 = UCLK, 24 = FCLK. Die 2350 MHz der Energiemodus-Tabelle sind der Wert des Modus, nicht der Takt | bestätigt |
| `[x]` | **Fehler in `clocks.c`: F und U vertauscht.** F ist FCLK (Fabric), U ist UCLK (Speichercontroller). Belegt durch die Live-Bereiche 24 = 750 und 23 = 225 zur selben Zeit wie F 750 / U 225 in der Tabelle | behoben |
| `[x]` | **FPS über `/dev/dce`** (ioctl `0x80308217`, Protokoll nach onionHEN). **Mit einem Spiel im Vordergrund: 29,9 Bilder/s** in Assassin's Creed Shadows, stabil. Ohne Spiel meldet der ioctl `ENOENT`, und der Treiber schrieb dann alle 5 s `[dce-ft:1013 ERR]` unter unserer PID ins Kernelprotokoll. Deshalb fragt die App nur bei einem Spiel im Vordergrund; sonst lautet der Status „kein Spiel". Eigenes Schalterbit `0x20` „Bilder pro Sekunde", ab Werk aus | an der Konsole bestätigt |
| `[-]` | ~~**Anzeige per Touchpad oder R3 statt PS-Taste**~~ (Wunsch des Users, 26.09.). Ein Payload darf den Controller nicht lesen, es braucht also eine Protokollspur wie `[onPSButtonPressed]`. Drei Mitschnitte, im Spiel und im Kontrollzentrum: **Touchpad und R3 hinterlassen nichts.** Spieltasten gehen still an die App im Vordergrund. Spuren hinterlassen nur Tasten, die das System selbst verarbeitet: die Stummschalttaste (`[Umm] MicMuteKeyPressed`, eindeutig, schaltet aber das Mikrofon um) und die Create-Taste (nur über ihre Wirkung, Bildschirmfoto oder Aufnahmemenü). Entscheidung des Users: **Es bleibt bei der PS-Taste** **Nachtrag 02.10.2026: der User hat anders entschieden — die Anzeige liegt jetzt auf der Mikrofon-Taste, zweimal gedrückt. Zwei Umschaltungen heben sich auf, das Mikrofon bleibt, wie es war (siehe 1.46.0).** | verworfen; am 02.10. durch die Mikrofon-Taste ersetzt |
| `[x]` | **Meldungen oben rechts, mit eigenem Symbol.** `sceKernelSendNotificationRequest` ist der Weg aus PS4-Zeiten und landet auf der PS5 oben **links**. Jetzt geht jede Meldung über `sceNotificationSend` (libSceNotification, per `ps5tm_dynsym`) mit der Vorlage `InteractiveToastTemplateB` (Feldaufbau nach dem SDK-Beispiel und ShadowMountPlus, nur Fakten). Das Symbol (Thermometer und Lüfter, Entwurf des Users, 256×256) steckt in den Web-Dateien, wird nach `/data/PS5-Cooling-Center/notify-icon.png` geschrieben und über **`/user/data/…`** referenziert. Mit `/data/…` zeigte die Meldung nur einen leeren Platzhalter, denn die Systemoberfläche löst nur den `/user`-Pfad auf. Der alte Weg bleibt als Rückfall | an der Konsole bestätigt |
| `[x]` | **Kachelhintergrund, vierte Fassung**: dasselbe Motiv ohne die vier Kästen am unteren Rand. Die bisherige Fassung liegt als `pkg/background_original_v3_kaesten.png` | installiert |
| `[x]` | ~~**Kanalfrage per Leistung**~~ (Kanal 7 GPU oder CPU?). In der Spielstunde lief die GPU am Leistungsdeckel (97–101 W), ohne Schwankung, mit der sich ein Kanal vergleichen ließe; die Korrelationen lagen alle unter 0,2. **27.09.: beantwortet** mit einem zweiten Spiel (Two Point Hospital, GPU 2–15 W bei festem Lüfter): **Kanal 7 sitzt an der Grafik**, siehe „Kanaltest 27.09.2026" | in 1.45.1 eingebaut |

---

## Neu in 1.45.1 — Grafiktemperatur, drei Beschriftungen korrigiert (27.09.2026)

| | Was | Stand |
|---|---|---|
| `[x]` | **Grafiktemperatur** = SoC-Kanal 7 (`gpu_c` in `platform.c`, `temperatures.gpu_c`/`gpu_valid` in der API). Dritte Zeile „Grafik" in der Sensorkarte der Übersicht, dritte Linie im Temperaturverlauf (Farbe `--s4`), Zeile in der Grafik-Karte der Systemseite, bestätigter Hinweis in den Rohsensoren. **Nur auf der PS5 Pro** (`ps5tm_sysinfo_gpu_channel_known()`: Modellnummer beginnt mit „CFI-7"). Nur dort ist die Zuordnung vermessen; auf anderen Modellen bleibt die Zeile weg | an der Konsole zu bestätigen |
| `[x]` | **„Hauptchip, gedämpft" (Kanal 0) entfernt.** Der Kanal fiel beim Spielende in 15 s um 15 °C wie alle anderen und lief die ganze Zeit innerhalb 0,2 °C neben Kanal 2. Der Befund vom 01.08.2026 ließ sich nicht wiederholen. „Hauptchip" bleibt das Maximum der Kanäle 1–7, die Regelung ist unverändert | — |
| `[x]` | **„Stromversorgung" (Kanal 1) entfernt.** Im Leerlauf liegt er mit den Kanälen 0, 2, 3 und 4 in einem Band von höchstens 0,6 °C; unter 100 W Grafiklast wird er am wenigsten warm. Ein Spannungswandler der Grafik müsste am stärksten heizen. Die Felder `vrm_c`/`vrm_valid` sind aus der API entfallen | — |
| `[x]` | **„Zusatz-SSD" (Kanal 2) entfernt.** Der Kanal fällt beim Spielende in 15 s um 14 °C, das kann keine SSD, und er zeigt Chip-Temperaturen auch ohne M.2-Laufwerk. Die Felder `m2_c`/`m2_valid` sind entfallen, `/api/v1/drives` meldet für M.2 `not_exposed`, der Diagnosehinweis „M.2 als Proxy-Sensor" ist weg | — |
| `[x]` | **Kanal-Heuristiken ersetzt.** Die unbestätigten Hinweise „Kanal 1 = Grafikschiene", „Kanal 2 = CPU-Schiene" und „Kanal 5 = Kandidat" sind widerlegt und entfernt. An ihrer Stelle steht der gemessene Hinweis zu Kanal 7 | — |
| `[x]` | **Fehler in der Lastspitzen-Auswertung** (Rohsensoren): Sie nimmt Kanal 0 als Bezug, zählte ihn aber auch als Kandidaten. Weil der Bezug bei jedem Ereignis per Definition sofort steigt, gewann er fast immer („Kanal 0 zieht zuerst an", 13 von 14 an der Konsole). Jetzt zählen nur die Kanäle 1–7 | behoben |

---

## Neu in 1.46.0 — Seite „Spiele" (28.–30.09.2026)

Wunsch des Users: ein Knopf „Spiele" unter „Profil", der alle Spiele des
Startbildschirms zeigt — mit Cover, Titel-ID, Content-ID usw. —, dazu
Backport, AMPR EMU und PlayGo, ein Knopf zum Starten, sowie (später
dazugekommen) Kopieren, Verschieben und Konvertieren in andere Formate.

| | Was | Stand |
|---|---|---|
| `[x]` | **Quelle: die App-Datenbank der Konsole** (`/system_data/priv/mms/app.db`, SQLite, Rollback-Journal). Der Startbildschirm liest dieselben Tabellen: `tbl_iconinfo_<Benutzer>` (je Kachel `visible`, Reihe `dispLocation`, zuletzt gespielt, Spielzeit in Sekunden, Reihenfolge `lastAccessIndex`) und `tbl_contentinfo` (Content-ID, lokalisierter Name, Größe, Installationszeit, Coverpfad `icon0Info`, Startlink `pprDeeplinkUri`, `AppInfoJson` mit Version, SDK und benötigter Firmware). `dispLocation` 138 ist die Spiele-Reihe, 188 die Medien-Reihe, 146/148/154 die Systemkacheln. `<Benutzer>` ist die Benutzer-ID dezimal, zehnstellig. Gemessen: 27 sichtbare Spiele, identisch mit dem Startbildschirm | an der Konsole bestätigt |
| `[x]` | **Eigener SQLite-Leser** (`src/sqlite_ro.c`, nur lesen). Das SDK hat kein SQLite, Module zur Laufzeit zu laden hat die App früher eingefroren, und die Amalgamation wäre fast ein Megabyte. Unterstützt Rowid-Tabellen: Kopf, B-Baum-Seiten, Überlaufketten, Datensätze, Spaltennamen aus `CREATE TABLE`. Jeder Versatz wird vor der Benutzung geprüft; ein halb geschriebener Stand endet in -1, nicht in einer falschen Zeile. **Gegen Pythons sqlite3 geprüft**: alle 18 Tabellen von `app.db` und `appinfo.db` Zeile für Zeile, Spalte für Spalte, dazu keine offenen Speicherblöcke. Dafür wurde das Modul ohne C-Laufzeit als Windows-DLL gebaut | geprüft |
| `[x]` | **Lesen ohne Sperren**: Die Datei wird in einem Zug gelesen. Kein Lesen, solange ein Journal einen Schreibvorgang anzeigt; der Änderungszähler im Kopf muss vorher und nachher gleich sein. Ergebnis 15 s zwischengespeichert. Erste Abfrage an der Konsole: 256 ms | — |
| `[x]` | **Cover** über `/api/v1/library/cover?id=` — nur für Titel der Liste, nur PNG in den eigenen Metadaten-Ordnern des Titels, gestreamt in 8-KB-Stücken, im Browser-Cache erlaubt (URL trägt den Zeitstempel der Konsole). Eine andere ID oder ein Pfadversuch ergibt 404 | geprüft |
| `[x]` | **Backport, AMPR EMU, PlayGo** für PS5-Spiele, die aus einem Ordner eingehängt sind (`/user/app/<ID>/mount.lnk` nennt ihn). Gemessen an drei Spielen: `fakelib/libSceAmpr.sprx` = AMPR-Emulation (daneben entsteht `ampr_emu.index`, Kennung „AMPRIDX3"), `fakelib/libScePlayGo.sprx` = PlayGo-Emulation (schreibt `playlgo.log`). **Backport**: Die SDK-Version in den Prozessparametern der `eboot.bin` liegt unter der aus `param.json` — Arkanoid: param.json 5.00, eboot 4.00; Fishing 4.00/4.00 und AC Shadows 10.00/10.00 sind unverändert —, oder fakelib enthält weitere Systembibliotheken. Die Prozessparameter liegen in der fake-signierten SELF: Eintragspaare Prüfblock/Daten, Segment `0x61000001` in einem LOAD-Segment, Kennung „ORBI", SDK als u32 bei +0x14. Zwei kleine Lesezugriffe, nie die ganze Datei (212 MB). Ergebnis je Titel zwischengespeichert, neu nur bei geänderter eboot.bin oder fakelib. Abbilder von ShadowMount (`/mnt/shadowmnt/…`) sind nur prüfbar, solange eingehängt | an der Konsole bestätigt |
| `[x]` | **Benötigte Firmware** je Spiel (PS5: `SYSTEM_VER_PPR`, PS4: `SYSTEM_VER` = PS4-Firmware), dazu das SDK. Liegt sie über der Konsole, steht sie gelb; der Hinweis sagt, ob die eboot.bin trotzdem startet (AC Shadows: verlangt 12.60, eboot SDK 10.00, läuft auf 12.00) | an der Konsole bestätigt |
| `[x]` | **Starten über eine Meldung** (Entscheidung des Users, 28.09.2026, zunächst als einziger Weg). Sonys Starter nimmt laut ps5upload (4.3.2, FW 9.60–12.x: „caller-pid check") Aufrufe nur aus ShellUI an; ps5upload umgeht das per ptrace in ShellUI — derselbe Eingriff, der beim Dauer-Overlay verworfen wurde. Deshalb zunächst: `InteractiveToastTemplateB` mit einer `DeepLink`-Aktion, `actionUrl` = der Link der Kachel (`psgm:play?id=…`, beim Spielekompressor `http://127.0.0.1:5910/`). Die Systemoberfläche öffnet ihn über `LinkingPS.openURLArg` wie einen Kachel-Druck, die Meldung bleibt in der Mitteilungsliste. **29.09.: eigener Test widerlegt ps5uploads Annahme** für diese Konsole/Firmware (siehe „Direkter Start" unten) — die Meldung bleibt seither nur noch **Rückfallweg**, wenn ein *anderes* Spiel schon läuft oder die Kachel gar kein Spiel ist. Die PS-Taste zeigt nach jedem Start-Angebot 30 s keine Temperatur-Meldung, damit sie die Start-Meldung nicht verdeckt | Meldung an der Konsole angenommen; Start durch den User bestätigt |
| `[x]` | **Direkter Start und Nach-vorn-Holen** (29.09.2026, Wunsch des Users nach einem schnelleren Browser-Schluss brachte die genauere Untersuchung). Läuft das Spiel schon, holt `sceShellUIUtilLaunchByUri("psgm:play?id=…")` es nach vorn — derselbe Weg, den die Kachel selbst nutzt. Läuft keines, startet `sceSystemServiceLaunchApp(id, NULL, &ctx)` es direkt, genau wie der Homebrew Launcher (websrv, `src/ps5/sys.c`): App-ID im Erfolgsfall, `0x80940005` ohne Parameterblock, `0x80940010` wenn schon ein Spiel läuft. Geprüft mit zwei Wegwerf-Payloads unter der App-Identität (`0x4801000000000013`): `sceLncUtilLaunchApp` kam zwar an, blieb aber hinter dem Startbildschirm; `sceSystemServiceLaunchApp` kam nach vorn — **das widerlegt ps5uploads „nur aus ShellUI" für diese Konsole/Firmware**. Anders als der Homebrew Launcher beendet dieser Weg kein laufendes Spiel zuerst, das bleibt der Person an der Konsole überlassen | an der Konsole bestätigt |
| `[x]` | **Browser schließt sich von selbst, direkt bei Spielstart** (Wunsch des Users, 29.09.2026: „geht es auch noch etwas früher"). Der PS5-Browser ist Teil von SceShellUI (`SceNKWebProcess`, Titel `NPXS40087`), lässt sich also nicht wie eine App beenden. Stattdessen, wie bei ps5-unified-autoloader (`app_killer.c`): `sceShellUIUtilLaunchByUri("pshomeui:navigateToHome?bootCondition=psButton")`, danach derselbe Kachel-Link wie oben. `libSceShellUIUtil.sprx` wird fest eingebunden (eigener Stub, siehe Makefile), nicht zur Laufzeit geladen — das SDK bringt dafür keinen mit. Gemessen mit einem Wegwerf-Payload: Tetris hinter dem Browser gestartet, 0,5 s nach dem Home-Sprung war der Browser-Prozess weg, danach hatte Tetris den Eingabefokus. Ohne Wartezeit vor dem Schließen (ausdrücklicher Wunsch des Users) vergehen vom Knopf „Starten" bis der Browser weg ist rund 1,5 s | an der Konsole bestätigt |
| `[x]` | **Kopieren** (`gamecopy.c`) auf ein anderes Laufwerk, mit Fortschritt und Abbrechen-Knopf. Zwei Ziele zur Wahl: **A Homebrew** — ShadowMountPlus findet das Spiel dort automatisch, mit Warnhinweis, falls dieselbe Titel-ID am Ziel schon existiert (Mount-Konflikt); **B Sicherung** — ein reiner Ablageordner, den nichts einhängt. `param.json`/`param.sfo` werden zuletzt geschrieben, ein `.ps5cc-kopie-unfertig`- bzw. `.ps5cc-teil`-Zeichen markiert einen eigenen, noch unfertigen Versuch (unterscheidbar von einer fremden Kopie), 256 MB Platzreserve, FAT32-Grenze geprüft | an der Konsole bestätigt |
| `[x]` | **Verschieben und Entpacken** über die ShadowMountPlus-API 1.7beta2 (`gamemove.c`, `smp.c`), für Titel, die ShadowMountPlus verwaltet: `/games/move` an ein anderes Laufwerk oder einen anderen Ordner (Homebrew, `etaHEN/games`, Laufwerkswurzel außer `/data`), `/games/unpack` aus einem Abbild in einen Ordner. Fortschritt über `/games/storage/status` (idle/preparing/measuring/transferring/deleting/finalizing/completed/failed/cancelled), abbrechbar | an der Konsole bestätigt |
| `[x]` | **Konvertieren** in exFAT oder ffpfsc — **eigener, nach C übertragener MkPFS-Konverter** (PSBrew, GPL-3.0), weil der zunächst gewünschte PS5 Game Compressor keine erkennbare Lizenz hat und nicht übernommen werden durfte (siehe Ablehnung unten). Deckt exFAT ab (64-KiB-Cluster, feste Zeitstempel, wie MkPFS) und den einteiligen PFS/PFSC-Container (Kopf, Inode-Tabelle, `flat_path_table`, `uroot`, komprimierte Blöcke ab 64 KiB). Kompression mit **libdeflate 1.26** (MIT) statt zlib: 4 Arbeiter, Stufe 6 — der Kernel verweigert Payloads die Leerlauf-Priorität, deshalb läuft ein Arbeiter weniger und jeder gibt nach jedem Block die CPU ab, damit ein Kern frei bleibt. **Tempo:** von rund 70–80 MB/s (zlib, Stufe 7) auf im Schnitt 193–198 MB/s, Spitzen bis 220 MB/s (User-Test „Instant Sports Plus.ffpfsc", 2,1 GB), bei gleichem oder kleinerem Ergebnis (2073 MB gegen 2077 MB bei einem Testspiel). **Geprüft gegen MkPFS:** exFAT bytegleich, ffpfsc mit MkPFS entpackt, alle Dateien byte- und metadatengleich | an der Konsole bestätigt, User-Test 220 MB/s |
| `[x]` | **Lizenzwechsel auf GPL-3.0-or-later** (Entscheidung des Users, 29.09.2026, nach eigener Recherche zu lizenzierten Alternativen). Grund: Der Konverter stammt aus MkPFS (GPL-3.0), und das PS5-Payload-SDK selbst steht unter GPLv3+ — beides war mit der bisherigen MIT-Lizenz nicht vereinbar. `LICENSE` enthält jetzt den vollständigen GPL-3.0-Text, das README hat einen Abschnitt „Lizenz", `THIRD_PARTY_NOTICES.md` ist neu (MkPFS, libdeflate, cJSON, SDK; ShadowMountPlus/ps5-unified-autoloader/websrv nur als Vorbild, kein Code übernommen) | umgesetzt |
| `[-]` | **PS5 Game Compressor direkt einbinden** (Hochladen der `.elf` und Start aus dem Tool heraus) — vom User abgelehnt: „aber es macht so keinen Sinn". Das Werkzeug hat zudem keine erkennbare Lizenz, sein Code durfte ohnehin nicht übernommen werden | verworfen, siehe Lizenzwechsel oben |
| `[x]` | **Einheitliche Spielekarten (02.10.2026, Wunsch des Users nach einem Bildschirmfoto: die Knöpfe lagen versetzt).** Der Versatz kam aus zwei Quellen: Karten mit Marken (AMPR EMU, PlayGo, „unbekannt") hatten eine Zeile mehr, und Karten ohne Kopieren/Verschieben/Konvertieren hatten weniger Knöpfe. Jetzt sitzen alle Bedienelemente einer Karte am **unteren Rand** (`.gm-actions` mit `margin-top: auto`), und jede Karte hat dieselben vier Knöpfe in fester Anordnung: Starten über die ganze Breite, Kopieren und Verschieben nebeneinander, Konvertieren darunter — unabhängig von der Kartenbreite (vorher brach eine Flex-Reihe je nach Breite anders um). Was für ein Spiel nicht geht, bleibt **ausgegraut sichtbar** (Tooltip nennt den Grund, zum Beispiel „Installierte Spiele (PKG) lassen sich nicht kopieren"). Gemessen mit den 26 echten Spielen der Konsole in 7 Reihen und drei Fensterbreiten: Infos, Starten, Kopieren und Konvertieren liegen in jeder Reihe auf derselben Höhe, auch wenn die Markenzeile bei schmalen Karten zweizeilig wird (48 statt 21 px) | in der Vorschau mit echten Daten bestätigt |
| `[x]` | **Format als Marke (02.10.2026).** In derselben Reihe wie Backport, AMPR EMU und PlayGo steht jetzt das Format: **Dump-Ordner, exFAT, ffpkg, ffpfs, ffpfsc, PS4 PKG, PS5 PKG** (installierte Pakete nach der Plattform; ein unbekannter Abbildtyp heißt „Abbild"). Die lange Marke „Backport/AMPR/PlayGo unbekannt" heißt jetzt „Anpassungen unbekannt" (der Grund steht im Tooltip), damit sie neben dem Format in eine Zeile passt. ⚑ **„fpkg" ist nicht erkennbar**: Ein echt installiertes und ein als fpkg installiertes Paket liegen beide als `/user/app/<ID>/app.pkg` und haben dieselben Angaben in `app.db` (gemessen an den 14 PKG-Titeln der Konsole: gewöhnliche Content-IDs `EP…`/`UP…`, nichts Unterscheidendes). Wer ein verlässliches Merkmal kennt, kann es ergänzen | bestätigt |
| `[x]` | **Neuer Name (02.10.2026): „PS5 Cooling & System Center - Pro".** Wunsch des Users, überall: Seitenleiste (vorher fälschlich „Cooling Center" mit dem Zusatz „PS5 · inoffizielles Tool", der entfällt), Fenstertitel, Fußzeile, Meldungen auf der Konsole, Kachel-Installer, GitHub-Release-Titel, alle Dokumente und die Kachel selbst (`param.json` und das Paket neu gebaut; eine schon installierte Kachel zeigt den Namen erst nach Löschen und erneutem Installieren). Dateinamen der Auslieferung, der Datenordner `/data/PS5-Cooling-Center/` und die Kennungen der Kachel bleiben (Bezeichner, keine Titel; der Datenordner trägt den Verlauf und die Wochenstatistik und wird nie umbenannt) | umgesetzt, nicht auf der Konsole |
| `[x]` | **Anzeige per Mikrofon-Taste statt PS-Taste (02.10.2026, Wunsch des Users: „2x Drücken, nicht mehr auf dem PS Knopf").** Mitschnitt des Kernelprotokolls (Port 3232) mit echten Drücken, einzeln, doppelt und dreifach: **ein Druck = genau eine Zeile** `[Umm] MicMuteKeyPressed D=0x50301 U=… PT=0` (dazu `mbusSetUserMuteStatus`, `onPostUmmStatusChanged` mit `"state"` 1/2 im Wechsel und die eigene Meldung der Konsole „Post7 … NUC58"); schnelle Drücke lagen 0,4–0,7 s auseinander. Jeder Druck schaltet das Mikrofon um, zwei Drücke lassen es also, wie es war — deshalb taugt der Doppeldruck als Auslöser (am 26.09. war genau das der Einwand gegen die Stummschalttaste). `micbutton.c` (vorher `psbutton.c`) liest den Puffer weiter jede Sekunde. Ein Doppeldruck sind zwei neue Zeilen in einer Lesung oder je eine in zwei Lesungen, die höchstens 2,5 s auseinanderliegen; ein einzelner Druck zeigt nichts. Höchstens eine Meldung alle 5 s; eigene Zeilen (`[ps5tm.elf]`) zählen nicht mit. Der PS-Tasten-Auslöser ist ganz entfernt, ebenso die 30 s Stille nach einer Start-Meldung (die gab es nur, weil die PS-Taste das Menü öffnet). Der Schlüssel `ps_button_status` behält seinen Namen (alte Einstellungen und API bleiben gültig), der Schalter unter Kühlung heißt „Anzeige per Mikrofon-Taste" | an der Konsole bestätigt: der Doppeldruck wurde erkannt und die Meldung ausgelöst (Protokoll `mic_button_shown`, 02.10.2026 nach dem Senden) |
| `[x]` | **Fertige Profilbilder zur Auswahl (02.10.2026, Wunsch des Users: die Bilder aus dem Ordner `Profilbilder` im Bereich „Profil" wählbar machen, damit man keinen PC mit Netzwerkverbindung zur PS5 braucht).** 30 Bilder (1024 × 1024, je rund 2,5 MB, zusammen 75 MB) liegen jetzt als **440 × 440 JPEG** (Qualität 82, progressiv, zusammen 1,3 MB) in `web/avatars/`, dazu `index.json` mit den deutschen Namen — gebaut von `tools/build_avatars.py` (Pillow; schneidet den einen Pixel breiten, leicht durchsichtigen Rand der Vorlagen ab; bricht ab, wenn die Summe über 2 MB geht). 440 px ist die größte Kante, die die Seite je erzeugt (vier Texturen 64/128/260/440 und ein 440-px-PNG), die 1024-px-Originale einzubetten brächte also nichts. **Die Größe zählt:** alles unter `web/` steckt im ELF (2,97 → 4,39 MB), und das Makefile warnt, dass ein 10,5-MB-Abbild der Konsole zu wenig Speicher ließ, um eine einzige Anfrage zu bedienen; 4,4 MB sind weniger als die Hälfte davon, aber an der Konsole nicht gemessen (Abschnitt 0). Die Seite „Profil" zeigt über der Vorschau ein Raster (auf dem Handy fünf je Zeile). Ein Tipp lädt das Bild in dieselbe Vorschau wie eine Datei (`adopt()` in `wireAvatar`), danach unverändert: vier Größen im Browser, Sicherung des bisherigen Bildes, zweiter Klick zum Bestätigen. **Es gibt keinen zweiten Weg zur Konsole, und die App bekam keine neue Schnittstelle** — die Bilder sind gewöhnliche Dateien der Oberfläche. Geladen werden nur schlichte Dateinamen aus der Liste; fehlt die Liste, bleibt das Raster verborgen. Geprüft: der echte C-Server liefert alle 30 JPEG und die Liste byte-gleich aus (404 für Unbekanntes, kein Pfad nach oben, keine Sanitizer-Meldung); Browser-Prüfstand mit der echten CSP (Szenario „AV"): Raster, Auswahl, zweite Auswahl, Übernehmen (11 Dateien, ein Apply-Aufruf), danach eine Datei, fehlendes Bild — 0 Verstöße; zusätzlich in der Browserfläche angesehen, ohne etwas an die Konsole zu senden. Herkunft: vom User selbst generiert (Auskunft vom 02.10.2026); die Bilder gehören mit zum veröffentlichten Stand, sobald das Repo öffentlich wird | gebaut; App mit den Bildern läuft an der Konsole (02.10.), das Anwenden eines Bildes ist dort noch nicht gemeldet |
| `[x]` | **Seite „Payloads" statt „Übersicht" (02.10.2026, Wunsch des Users, mit dem Bild einer fremden Payload-Verwaltung als Beispiel für die Darstellung).** Neues Modul `src/payloads.c`. **Ordner:** `/data/PS5-Cooling-Center/payloads` wird beim Start der App angelegt, wenn er fehlt (`mkdir` 0777 mit `chmod`, weil die Umask es sonst verengt und FTP dort schreibt); ein vorhandener Ordner bleibt unberührt (kein `mkdir`, kein `chmod`, kein Protokolleintrag), und eine **Datei** dieses Namens wird gemeldet (`payload_dir_blocked`), nicht überschrieben. **Starten** = was ein PC macht: Verbindung zum Payload-Lader (elfldr, `127.0.0.1:9021`), Datei in 64-KiB-Stücken schicken (nie ganz im Speicher), Sendeseite schließen, bis zu 1,5 s auf die ersten Worte des Payloads hören, bereinigt (gültiges UTF-8 bleibt, alles andere `?`). **Quellen:** der interne Ordner und `/mnt/usbN` (aus der Laufwerksliste von `sysinfo.c`): Hauptverzeichnis des Sticks und dessen Ordner `payloads` in der Schreibweise des Sticks (`Payloads` von einem PC formatiert); nicht rekursiv, keine versteckten Dateien (`._name.elf` von macOS), keine Links, nur `.elf` (auch `.ELF`). **Nichts Eigenes im Pfad:** die Seite nennt Ort, Ordner und Dateiname, nie einen Pfad; die Konsole prüft alle drei erneut (Name ohne `/`, `\`, Steuerzeichen, gültiges UTF-8, ≤ 199 Bytes; Mount nur aus der eigenen USB-Liste; Ordner nur Wurzel oder `payloads`), öffnet mit `O_NOFOLLOW` und verlangt eine gewöhnliche Datei mit dem Kopf eines 64-Bit-little-endian-x86-64-ELF (Typ EXEC oder DYN), 64 Byte bis 128 MB — Ungültiges steht markiert mit Grund in der Liste, der Knopf bleibt gesperrt. **Eins nach dem anderen:** Start, Kopieren und Löschen teilen eine Sperre (zweiter Aufruf: 409 `busy`), dieselbe Datei innerhalb 1,5 s zweimal: 409 `just_started`; die Liste liest USB nur einmal gleichzeitig und merkt sich ihre Antwort 3 s (ein hängender Stick bindet höchstens einen Thread). **Kopieren** USB → intern über `.name.part`, `fsync`, `rename`, nie überschreibend (409 `exists`, auch bei einem gleichnamigen Link), mit Platzprüfung (+ 16 MB Reserve) und 120-s-Frist; **Löschen** nur im internen Ordner. **Oberfläche:** Karten wie im Beispielbild (Symbolkachel, Name, Version in Blau aus dem Dateinamen, Pfad, „Starten", Mülleimer mit zwei Klicks), je Quelle eine Liste mit Zähler; die „Laufende Zusatzprogramme"-Karte der Systemseite zog hierher (Beenden wie bisher mit zwei Klicks), damit man nach dem Start gleich sieht, ob es läuft; USB-Karten haben zusätzlich „In den internen Speicher kopieren" (statt „verschieben" im Beispiel: das Original bleibt). Nicht übernommen vom Beispiel: Cloud-Repository und „Payload hochladen" (nicht verlangt; Letzteres bräuchte wieder einen PC). **Offen / Deutung:** Der Satz des Users zu den USB-ELF-Dateien war nicht zu Ende geschrieben; umgesetzt ist: anzeigen, starten und in den internen Ordner kopieren. **Geprüft:** Host-Test (ASan/UBSan, falscher Lader auf anderem Port, USB als Ordner): 104 Prüfungen — Ordner anlegen/belassen/Datei statt Ordner, Liste (gültig/ungültig, Links, Unterordner, versteckte, falsche Namen, Sortierung, ungültiges UTF-8), Starten von intern und USB (Bytes kommen unverändert an), 30 Ablehnungen (Pfade, fremde Mounts, falsche Ordner, ungültige ELF), Doppelklick, Sperre, stummer Lader, abbrechender Lader, fehlender Lader, Kopieren (auch 8 MB), Löschen, Zwischenspeicher, und dass ein Dateiname mit Marker-Text (`Battery level : …`, `[onPSButtonPressed] …`) bereinigt im Protokoll steht (das Protokoll spiegelt in den Kernelpuffer, den `dualsense.c` ohne Ausnahme eigener Zeilen liest); Fuzz mit den neuen Routen: keine Abstürze; Browser-Prüfstand Szenario „PL" mit echter CSP: 0 Verstöße **An der Konsole bestätigt (02.10.2026 abends):** Beim Start legte die App den Ordner an (`payload_dir_created`); die Liste fand den USB-Stick (`/mnt/usb0`) mit den 17 `.elf`-Dateien des Users, alle gültig; eine per FTP in den Ordner gelegte Datei (der Ordner ist für alle schreibbar) erschien sofort in der Liste; **Starten** der App-eigenen ELF aus dem Ordner über `127.0.0.1:9021`: der Lader nahm die Verbindung vom eigenen Rechner an, die neue Ausführung (Nr. 211) beendete die alte (Nr. 210, deshalb riss die Antwort nach 0,3 s ab) und lief danach gesund weiter (Automatik, Sensoren, Lüfter); **Löschen** über die Schnittstelle entfernte die Datei, FTP sah den Ordner wieder leer. Noch nicht erprobt: ein fremdes Payload starten (Verhalten beim Schließen unserer Socket-Seite, `stdout`), Starten von USB und Kopieren von USB an der echten Konsole | gebaut, im Host getestet und an der Konsole bestätigt |
| `[x]` | **„Übersicht" und „Kühlung" sind eine Seite (02.10.2026, Wunsch des Users: Inhalt der Übersicht nach „Kühlung", ohne doppelte Angaben).** Die Seite öffnet jetzt auf „Kühlung" (vorher „Übersicht"). **Von der Übersicht übernommen:** Kopf mit dem wärmsten Punkt, Lüfterkachel, „Läuft gerade"-Kachel, Temperaturverlauf, Sensorkarte, und im Expertenmodus Lüfterdrehzahl, Prozessorlast, Langzeitverlauf und weitere Messwerte (jetzt ein Expertenbereich statt zwei). **Als Doppelung entfernt:** der kleine Kopf der Kühlungsseite (wärmster Punkt, Lüfter, Ziel — dieselben Zahlen wie der große Kopf), die zweite Voreinstellungsreihe („Kühl/Ausgewogen/Leise" gab es in der Übersicht und in der Zieltemperatur-Karte), die Karte „Lüftersteuerung" (Modus und Ziel stehen in „Zieltemperatur" und „Betriebsart"), die Karte „Konsole" (Modell, Firmware, Laufzeit stehen auf der Systemseite, die Version in der Fußzeile und in der Systemliste) und der „Schnellzugriff" (die Seitenleiste ist der Schnellzugriff). Das Ziel selbst steht nur noch in „Zieltemperatur"; unter dem Ring sagt der Kopf, wie weit der wärmste Punkt davon entfernt ist („3 °C über dem Ziel", im Beobachtungsmodus „… der Schwelle", mit „(Spielregel)" wenn eine Regel das Ziel ändert). Der Hinweis „Voreinstellung … aktiv / Eigener Wert" zog unter die Schnellwahl. In app.js: `renderReactor` schreibt nur noch, was es gibt, `OV_PRESETS` heißt `PRESETS`, `[data-goto]` ist weg, Diagramme und Verlauf laden beim Öffnen der Kühlungsseite. CSS: tote Regeln der Übersicht und des kleinen Kopfes entfernt, ebenso `banner-cooling.jpg` und `icon-apps.png` (zusammen rund 90 KB weniger im ELF). **Geprüft:** alle 21 bisherigen Browser-Szenarien (Seitennamen angepasst) plus „AV" und „PL": keine Skriptausführung, keine Seitenfehler, keine CSP-Verstöße, keine Fehltexte (`undefined`, `NaN`) auf irgendeiner Seite; Kühlungsseite, Systemseite und Payloads in der Browserfläche angesehen (Handybreite 375 px ohne seitliches Überlaufen) | gebaut und im Browser geprüft |

---

## Durchsicht vor der Veröffentlichung von 1.46.0 (02.10.2026)

Auf ausdrücklichen Wunsch des Users („komplette Durchsicht, Fehler und Probleme
beheben") wurde der gesamte Code (rund 21.000 Zeilen C, 7.700 Zeilen Oberfläche,
Makefiles und Skripte) von sieben unabhängigen Prüfdurchgängen gelesen und
getestet: Web-Server und API, Spieleliste/SQLite-Leser/ShadowMountPlus,
Kopieren/Verschieben/Konvertieren, die drei Abbild-Schreiber, die Weboberfläche,
Lüfter-/Sensorkern sowie Spielstatus/Kachel/Installer/Build. Die Prüfer haben
nicht nur gelesen, sondern jeden Fund mit einem eigenen Testprogramm
nachgestellt (Speicher- und Überlaufprüfung, Fuzzing, unabhängige Gegenparser
für exFAT, PFSC und UFS2, ein Headless-Browser mit nachgebauter Konsole), und
jede Korrektur gegen denselben Test geprüft. Die Testprogramme liegen im
Scratchpad der Sitzung, nicht im Repo. ⚑ Alles unten steht **nur im
Arbeitsbaum**: nichts davon ist committet, veröffentlicht oder auf der Konsole
getestet — Letzteres gilt besonders für die neuen Importe (`pthread_mutex_trylock`,
`inet_pton`, `fchmod`, `getsockopt`; sie stehen alle im libkernel-Stub des SDK,
sind also Exporte der Konsole, aber der erste Start muss es bestätigen).

| | Was | Stand |
|---|---|---|
| `[x]` | **Fremde Webseiten konnten die Konsole steuern (hoch).** Die API hat keine Anmeldung, und manche Aufrufe sind zerstörerisch: ausschalten, ein Spiel verschieben „mit Quelle löschen", Zusatzprogramme beenden, Einstellungen überschreiben. Eine Seite, die im Browser *irgendeines* Geräts im Heimnetz offen war, konnte per „einfachem" Cross-Origin-POST (kein Preflight, der Inhalt wird als JSON gelesen, gleich welcher Content-Type) genau diese Aufrufe schicken, und per DNS-Rebinding sogar Antworten lesen. Jetzt prüft `http.c` bei jeder Anfrage den **Host** (IP-Adresse, `localhost`, Einzelname ohne Punkt oder Heimnetz-Endung `.local .lan .home .home.arpa .internal .localdomain .intranet .fritz.box`) und bei allem außer GET/HEAD den **Origin** (muss dieselbe Adresse nennen; `null` und fremde Quellen gehen nicht). Werkzeuge ohne Origin (curl, die Kachel) bleiben unberührt. Dazu: höchstens 24 gleichzeitige Verbindungen (sonst 503; eine Seite braucht sechs), feste Zeitbudgets (15 s Kopfzeilen, 20 s Inhalt, Uploads 10 min / 2 min) statt eines Zeitlimits je Lesevorgang — ein Tropf-Client hielt vorher seinen Thread, so lange er wollte —, und abgelehnte Anfragen werden höchstens alle 30 s protokolliert, mit bereinigtem Text (der Text landet auch im Kernelpuffer, den Spielerkennung und PS-Taste lesen: ein Pfad `/[onPSButtonPressed]` hätte dort einen Tastendruck vorgetäuscht) | im Host-Build bestätigt (19 Fälle, Verbindungsflut, Tropf-Client) |
| `[x]` | **Gespeichertes XSS über Namen (hoch).** Spielnamen, Protokollzeilen, WLAN- und Fernsehnamen, Prozessnamen, Importdateien gelangten ungeschützt in `innerHTML`. Ein präparierter Spieldump konnte beim Öffnen von Protokoll oder Kühlung Skript in der Oberfläche ausführen, und dieses Skript hätte alle Aufrufe oben ausführen können (derselbe Ursprung). Jetzt wird jede Stelle über `esc()` oder `textContent` geführt (jede Vorlage wurde auf Zeichenketten aus der Konsole durchsucht, nicht nur die gemeldeten), und die Seite kommt mit `Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'` sowie `nosniff` und `Referrer-Policy`. Die Seite hat kein Inline-Skript und redet nur mit ihrem Ursprung, die Richtlinie kostet also nichts: 21 Szenarien mit Spieldaten, Importen, Exporten, Avatar-Pipeline und feindlichen Namen im Headless-Browser, 0 Verstöße, nichts ausgeführt | bestätigt |
| `[x]` | **Ein Anführungszeichen im Spielnamen löschte alle Einstellungen (hoch).** `ps5tm_config_save()` schrieb `title_name` mit `%s`; ein Profil für `Spiel "Deluxe"` machte `config.json` unlesbar, der nächste Start lud Standardwerte. Jetzt JSON-Escape (`put_json_string`), Namen werden an Zeichengrenzen gekürzt (`ps5tm_copy_utf8`: japanische Titel sind 3 Byte je Zeichen), die Titel-ID eines Profils darf nur `[A-Za-z0-9_-]` sein. Das Speichern prüft `fflush`, Fehlerflag, `fsync` und `fclose`, ehe die Datei die alte ersetzt (eine volle Platte ersetzte sonst die gute Datei durch eine kurze; ein Ausschalten direkt nach einer Änderung hinterließ eine leere), und es nimmt unter dem Config-Lock nur einen Schnappschuss: Der Lüfter-Thread greift mehrmals je Sekunde nach diesem Lock und wartete bisher auf jede Festplattenschreibung. Folge: **`ps5tm_config_save()` darf nie mit gehaltenem Lock aufgerufen werden** (steht in `ps5tm.h`) | bestätigt |
| `[x]` | **Zahlen aus JSON.** `{"http_port":1e30}`, `-5` in einem vorzeichenlosen Feld oder `NaN` waren Umwandlungen außerhalb des Wertebereichs (undefiniertes Verhalten; der Sanitizer-Build brach ab, auf der Konsole kam heraus, was die CPU-Anweisung hinterließ). Jetzt sättigende Umwandlung (`ps5tm_num_u32/i32`). Außerdem wird abgelehnt statt unbemerkt gespeichert: Port unter 1024 (400; eine leere Eingabe wurde als 0 gesendet, auf 1 geklemmt und machte die Oberfläche dauerhaft unerreichbar), ein Port, auf dem schon etwas lauscht (409 — sonst verlor die App ihren eigenen Listener), `bind_address` die keine IPv4-Adresse ist (400; ein Anführungszeichen darin brach auch das JSON von `GET /config`), Spielprofil-Ziele werden überall auf 60–72 °C begrenzt (500 °C hielt den Lüfter bis zur Notgrenze auf dem Boden, 1e8 lief im Regler über). Ein importierter Port wird nur übernommen, wenn er brauchbar und frei ist | bestätigt (46 Funktionstests) |
| `[x]` | **„Diagnose 2/5/10 Min." stellt sich selbst zurück (mittel).** Die alte Maske lag nur im Speicher der Seite; Seite zu oder Browser nach „Starten" geschlossen = die riskanten Zusatzabfragen blieben dauerhaft an, obwohl die Meldung die Rückstellung versprach. Jetzt macht es die Konsole: `PUT /config` mit `probe_mask` + `probe_revert_after_s`; auf der Platte steht dabei immer die bisherige Maske (ein Neustart beendet die Diagnose also von selbst), die Seite findet einen laufenden Timer nach dem Neuladen wieder | bestätigt |
| `[x]` | **Lüfterregler wartete auf Sonys Dienste (hoch).** Der Lüfter-Thread nahm jede Sekunde den Sony-Lock, den andere Threads über Aufrufe halten, die laut Notizen in `probe.c`/`tile.c` stundenlang nicht zurückkehren konnten (Spiel starten, Kachel installieren, Controller öffnen); die Temperaturwarnungen (`ps5tm_show_dialog`, `ps5tm_notify`) liefen auf demselben Thread, gerade dann, wenn geregelt werden muss. Jetzt nimmt er den Lock nur noch mit `trylock` und zeigt bei Besetzung den letzten Wert; Warnungen gehen über einen Hilfsthread (ein Platz je Quelle, neueste gewinnt). Temperaturen, Drehzahlregister und ICC-Schreiben brauchen den Lock nicht | im Harness mit echtem `fan.c` bestätigt (ein Thread hält den Lock für immer: vorher steht der Regler im ersten Durchlauf, jetzt läuft er) |
| `[x]` | **Weitere Fehler im Regler.** Die Überwachung der Schwelle gab nach *einem* Lesefehler für den Rest der Laufzeit auf (Fehler beim Kachel-Install, ein EBUSY): Korrektur nach einem Spielstart dann 15–300 s statt 1 s; jetzt erst nach 5 Fehlern in Folge, alle 30 s ein neuer Versuch. **Sensor meldet 0** galt als gültig und zog den Mittelwert in den Keller; jetzt ungültig (wie in `thermalog.c`), und nach 10 s ohne gültigen Wert hält der Lüfter mindestens 70 %, bis wieder Werte kommen (Wiederkehr: Mittelwert beginnt neu, die Drehzahl sinkt schrittweise). Ist das Drehzahlregister nicht lesbar (Firmware ohne den Aufruf), fror der Servo ein; jetzt folgt die Schwelle dem Wunsch. `pthread_create` wurde nicht geprüft („Automatik läuft", obwohl nichts lief). Alle Schwellenschreibungen bleiben auf 30–91 geklemmt: Keine App-Lage lässt weniger Lüfterleistung zurück als die Konsole allein | im Harness (Regler gegen Wärmemodell) bestätigt; auf der Konsole nicht |
| `[x]` | **Log-Ausgabe konnte den Regler anhalten.** `stdout` ist der Socket, über den das Payload gesendet wurde: Ein Sender, der verbunden bleibt, aber nicht mehr liest, füllte irgendwann den Puffer, und jedes `printf` blockierte — im Lüfter-Thread mit. Jetzt Sendezeitlimit 500 ms, nach dem ersten Fehlschlag ruht die Spiegelung 60 s (Meldung im Protokoll), Kernel-Log und Protokollring laufen weiter | im Harness bestätigt |
| `[x]` | **Aufzeichnungen konnten durch eine volle Platte verkürzt werden.** `thermalog.c` (die mehrwöchige Kühlleistungs-Aufzeichnung, vier Wochen für das Urteil!), `history.c` und der Controller-Akkustand ersetzten die gute Datei durch eine zu kurze; jetzt wie bei den Einstellungen geprüft, die alte Datei bleibt, die Daten bleiben im Speicher. Lightbar: nach einem abgelehnten Versuch 60 s Pause (vorher jede Sekunde drei Sony-Aufrufe unter dem Lock); Controller-Diagnose lädt keine Module mehr im Anfrage-Thread (`dlopen` dort ist die in `dualsense.c` selbst beschriebene Einfrier-Ursache) und meldet `hid.resolved`; Zählergrenzen in `platform.c` (zwei echte Pufferüberläufe, die ASan zeigt) | bestätigt |
| `[x]` | **Spieleliste.** `refresh()` hielt den Lock während der Ordnergrößen-Läufe (ein `lstat` je Datei, ungecacht, alle 15 s, auf USB) und der Anfragen an ShadowMountPlus (bis 5,5 s): Cover, Starten und Kopieren hingen dahinter. Auf der Konsole dauert ein Lesen dank warmer Zwischenspeicher nur 0,3 s — gemessen —, aber das Gerüst war falsch. Jetzt: ein Lesen zugleich, alles Langsame ohne den Lock, ein Aufrufer, der ein Lesen laufen findet, nimmt die vorhandene Liste; Ordnergrößen 10 min gemerkt, nie als Teilsumme (ein Lesefehler mitten im Lauf zeigte 12 statt 280 GB); mehr als 256 Einträge in `tbl_contentinfo` ließen die Liste leer erscheinen (Kachelreihenfolge ≠ Inhaltsreihenfolge) — jetzt gekürzt **mit** Hinweis (`truncated`); die Änderungsprüfung des Journals auch nach dem Lesen; `..` nur als Pfadbestandteil abgelehnt (ein Titel „Foo... Edition" verlor sonst alle Speicherangaben); nach Verschieben/Kopieren/Konvertieren liest die Liste sofort neu; ein zweiter „Starten"-Druck binnen 10 s löst keine falsche „anderes Spiel läuft"-Meldung mehr aus. `smp.c`: Überlauf in `dechunk` (`FFFFFFFFFFFFFFFE` als Blockgröße — ASan bestätigt) | bestätigt (400 gleichzeitige Abfragen unter ASan/UBSan **und** TSan; 300 Titel umgekehrt: vorher 0, jetzt 256 mit Hinweis) |
| `[x]` | **Kopieren, Verschieben, Konvertieren.** Eine Kopie meldete „fertig", obwohl sich die Quelle während des Kopierens geändert hatte und Dateien fehlten (reproduziert: 4501 von 6001 Dateien) — und die Seite fordert danach auf, das Original zu löschen. Jetzt endet sie nur als „fertig", wenn alles gelesen wurde, sonst als Fehler mit den echten Zahlen. Eigene Reste eines früheren Versuchs werden **vor** der Platzprüfung gelöscht und zählen als freier Platz, je Ziel; ein Pfad, der am Ziel zu lang würde, scheitert schon im Plan; die drei Aufträge (Kopie, Konvertierung, ShadowMountPlus-Verschieben) laufen nie gleichzeitig (409); Konvertieren schreibt `.ps5cc-konv-teil` statt des Kopier-Suffixes (eine Kopie und eine Konvertierung desselben Namens konnten sich die Teildatei gegenseitig löschen); die Markierung „unfertige Kopie" muss entfernt werden können, sonst scheitert der Auftrag statt eine fertige Kopie für die nächste Runde als löschbar stehen zu lassen; Abbrechen wirkt auch während Vermessen und Prüfen; Fehlertexte unterscheiden Quelle und Ziel; Verschieben prüft beim Start erneut (Name am Ziel schon da → 409, Platz, FAT32). Alle Zeitspannen laufen auf der monotonen Uhr (`ps5tm_mono_ms()`): Die Wanduhr springt, wenn die Konsole ihre Zeit abgleicht, und die Differenz war dann eine Zahl von 10^16 Sekunden | bestätigt (200 Prüfungen im Harness, davon 38 gegen den Altstand fehlschlagend) |
| `[x]` | **Die drei Abbild-Schreiber (exFAT, ffpfsc, ffpkg) — zwölf Punkte, jeder mit einem eigenen Gegenprogramm nachgestellt** (Prüfer „Schreiber"). **ffpkg/UFS2:** Der Gruppenkopf konnte größer werden als sein Block (8 × 120 MiB scheiterte erst beim Prüfen mit „Inode 2 …"; jetzt wird die Geometrie begrenzt, und das Abbild besteht die Prüfung); Ordner über 12 Blöcke (etwa 12.000 Einträge bei 20-Zeichen-Namen, etwa 770 bei 255) wurden still beschädigt, jetzt lehnt der Plan sie ab (genau 12 Blöcke gehen); Bäume mit vielen kleinen Dateien (300 × 1000 B, 8000 × 1 Byte) scheiterten, weil das Abbild nicht für den Blockbedarf reichte — jetzt wächst es passend (verglichen wird mit dem exakten Bedarf aus dem Größenlauf, nicht mit `disk_size/32768`), und Bäume fast nur aus leeren Dateien werden im Plan mit klarer Meldung abgelehnt; eine zu große Datei (über rund 128 MB) wird in 0,02 s im Plan abgelehnt statt erst nach 250 MB geschriebener Daten; eine Datei, die beim Kopieren schrumpft, oder ein Ordner, der nach dem Plan wächst, ergab ein „gültiges" Abbild (einmal 455 MB Inhalt für ein 114-MB-Abbild, Inodes außerhalb der Gruppen) — jetzt ein Fehler; der Rückgabewert des zweiten `pack_dir_blocks` wird geprüft. **exFAT:** Pfade ab 256 Zeichen wurden abgeschnitten geöffnet (jetzt der volle Pfad, nur die Anzeige wird gekürzt; ab 1024 Zeichen weiter Ablehnung im Plan); Namen, die unter der Up-case-Tabelle gleich sind („Readme.txt"/„README.TXT"), erzeugten ein beschädigtes Volume (jetzt im Plan abgelehnt, beide Namen und der Ordner genannt; 1 Mio. Einträge kosten 0,4 s); Vorzeichenüberlauf in Hash und Prüfsumme (jetzt unsigned); der Up-case-Speicher (128 KiB) wird im Plan angelegt statt mitten im Strom — sonst wurde bei Speichermangel ein anderer Hash geschrieben. **Alle drei:** Abbrechen wird atomar gelesen (TSan sauber bis auf das bekannte Race in libdeflates `adler32.c`); die Prüfung nach dem Schreiben ist schärfer (UFS2: alle 30 gezielt beschädigten Abbilder erkannt, vorher 5, bei zufälligem Schaden 1,2 % statt 12,9 % Ablehnung; PFS: ganze Blocktabelle und acht Stichprobenblöcke, Rohgröße ≠ 0). **Der Satz „jedes Ergebnis wird gelesen und geprüft" in der Oberfläche stimmte für exFAT nicht** (es gibt dort kein Zurücklesen) und steht jetzt je Format wahrheitsgemäß da. **Gültige Ausgaben unverändert:** UFS2 (63 Bäume), exFAT (39), PFS (22 Eingaben) byte-gleich zum Altstand, Monte-Carlo über 36.000 Bäume: alle 15.557 vorher gültigen Geometrien unverändert, nie ein ungültiges Abbild; Ausnahme nur die UFS2-Geometrie, wenn der Gruppenkopf vorher über seinen Block hinausging (8 × 110 MiB: Gruppen kleiner). Bewusst nicht gebaut: zeitgesteuertes Warten auf die Worker beim Abbrechen (ein Worker mitten in der Kompression lässt sich nicht unterbrechen; es bräuchte abgekoppelte Worker, kein kleiner Eingriff). Offen: `gameconvert.c` nimmt für ffpkg den exFAT-Plan (Größe, Dateizahl), die exFAT-Namensregeln sperren damit auch ffpkg, obwohl UFS2 solche Namen speichern kann — ein kleines `ufs2_plan_tree()` würde das lösen und die ffpkg-Grenzen schon in der Oberfläche zeigen | bestätigt (Regressionssuite 44 Prüfungen, 0 Fehler; nicht an der Konsole) |
| `[x]` | **Weboberfläche.** Ein später eintreffender Plan überschrieb den *gerade offenen* Dialog (Kopierplan landete im Konvertieren-Fenster; bei „Verschieben" mit Löschen der Quelle: ein Plan unter dem falschen Titel); `mvPoll` hörte nach einem verlorenen Abruf für immer auf; die Abfrage ließ eine hängende Antwort *nach* den Kopfzeilen unbegrenzt warten (die Ein-Sekunden-Schleife blieb stehen, Anzeige „verbunden"); ein fehlgeschlagener erster Einstellungsabruf wurde nie wiederholt, danach speicherte „Erweitert" Nullwerte (auch Port 0); „Entfernen" in der Lüfterkurve traf nach dem Umtippen die falsche Zeile; „Zurücksetzen" ließ die alte Verlaufskurve stehen; das Live-Protokoll fragte alle 2 s weiter, auch von anderen Seiten aus, und nichts ruhte im Hintergrundreiter; der Vollbild-Knopf merkte sich eine Ablehnung für immer. Die neuen Planfelder (`other_busy`, je Ziel `enough_space`/`unfinished_bytes`/`path_too_long`, `target_exists` für Verschieben) sind eingebaut | bestätigt (21 Szenarien) |
| `[x]` | **Kachel, Profilbild, Spielstatus, Build** (Prüfer „Kern B"). Profilbild übernehmen: kein Sicherungsordner, wenn die Konsole keinen hatte (der zweite Durchlauf sicherte dann die eigenen Dateien als „Original", und „Vorheriges zurückholen" lieferte unser Bild); es wird nur noch der vollständige Satz von 11 Dateien mit DDS-/PNG-Kennung übernommen, über Teildatei + `rename` statt `O_TRUNC` auf die laufenden Dateien (ein Abbruch hinterließ ein halbes Bild im System-Cache), zwei gleichzeitige Aufrufe sperren sich. Kernelpuffer (Spielerkennung, PS-Taste, Akku, Takt): NUL-Bytes (die ungeschriebene Hälfte eines noch nicht umgelaufenen Rings) verbargen alles dahinter; `SetControllerFocus(` ohne Schließklammer galt als Zahl 0; der Größenrahmen von 512 KiB machte einen Ring dieser Größe unlesbar (stiller Ausfall aller vier Funktionen); eigene Protokollzeilen (`[ps5tm.elf]`) werden nicht als Tastendruck gezählt. Sperr-Umkehr zwischen Benutzerdienst und Sony-Lock (Controller-Probe + Namensabfrage: beide Threads hängen für immer, mit dem Lüfter) beseitigt; die Anzeige-Abfrage (VideoOut) lief bei abgeschalteter Spielerkennung alle 5 s *während* des Spiels; Prozesstabelle mit Reserve und Wiederholung (ein Spielstart ließ sie über den Puffer hinauswachsen → „kein Spiel", Profil des Spiels für einen Durchlauf aus); Kachel-Paket: Kennung `7F 46 49 48` und Größe werden vor dem Einbetten und vor `AppInstallPkg` geprüft, ein Fehlschlag von `pthread_create` ließ den Kachel-Install für immer scheitern. Makefile: `.inc`-Dateien, libdeflate-Header und das Makefile selbst gehören zu den Abhängigkeiten (eine Änderung allein an der Up-case-Tabelle endete in „Nothing to be done" mit der alten Tabelle im Release), das eingebettete Kachel-Paket folgt dem gewählten Paket (ein einziges `make TILE_PKG=x.pkg` ließ es für immer eingebettet), `release` löscht nichts mehr (Abbruch mit Liste, wenn Fremdes im Ordner liegt), `build-windows.sh` und `pre-release-check.ps1` prüfen die LLVM-Version (21) und melden einen Leerzeichen-Pfad verständlich, die CI-Prüfung der LIESMICH konnte nie fehlschlagen (verglich null Ordner), `diagnose.ps1 -FanTest` stellt die Schwelle wieder her | je Fund gegen den Altstand nachgestellt und gegen den neuen Stand bestanden |
| `[x]` | **Zwei Rücknahmen und eine Entscheidung.** (1) Die Prüfer wollten die ShellCore-Identität bei der Kachel-Installation gleich nach `AppInstallPkg` zurückgeben, um die Lüfterpause zu verkürzen; das ist auf der Konsole nie probiert worden (der Auftrag läuft laut Notiz im Installationsdienst, nicht im Payload), und es geht um eine einmalige Aktion — **zurückgenommen**, die Identität bleibt wie bisher durch das Warten. (2) Der Sony-Lock bleibt während `sceSystemServiceLaunchApp` gehalten (der Regler wartet jetzt nicht mehr darauf; ein hängender Start würde aber weiter andere Sony-Aufrufe aufhalten). (3) `sqlite_ro.c` kennt Randfälle des Schemas nicht (Kommentare mit `'`, `GENERATED`-Spalten, Spalten per `ALTER TABLE`): Die aktuelle `app.db` ist nicht betroffen (alle 18 Tabellen belegt), also nicht angefasst | dokumentiert |
| `[x]` | **Geprüft und in Ordnung befunden:** SQLite-Leser (360.000 mutierte Dateien unter ASan/UBSan: kein Absturz, kein Hänger; Ausgabe byteidentisch zu Pythons sqlite3), Regler-Mathematik (20.000 Zufallsfolgen gegen ein Referenzmodell, 2 Mio. Zufallsaufrufe), alle Pfadbildungen der Datei-Aufträge (kein Anfragetext wird ungeprüft zum Pfad), `fsync` vor `rename` in allen vier Schreibern, Aufräumen nach Fehler/Abbruch löscht nur, was der Auftrag selbst angelegt hat | — |
| `[ ]` | **Bekannte Restpunkte (bewusst nicht angefasst):** die pro-Spiel-Profile ändern nur Komfortgrenze und Haltezeit, nicht Band, Schritt und Fenster (die Voreinstellungen gelten global); `history.c`/`thermalog.c` schreiben weiter auf dem Lüfter-Thread (gepuffert, ohne `fsync`); eine Datenbank-Version mit WAL würde stillschweigend einen alten Stand zeigen; die ShadowMountPlus-Sperre ist best effort (ihre eigene Oberfläche kann jederzeit einen Auftrag starten) | offen |

---

## SoC-Kanaltest 26.09.2026 — Spiel gegen Rechenlast

Frage: Welcher der acht Sensorkanäle folgt der Grafik, welcher dem Rechenwerk?
App 1.44.0, Aufzeichnung jede Sekunde plus Kanal-Ring (5 s). Rohdaten
**außerhalb** des Repos unter `Messungen/2026-09-26 SoC-Kanaltest/`, samt der
sieben Lastprogramme.

### Spiel: Kanal 7 heizt am stärksten, Kanal 1 am wenigsten

Assassin's Creed Shadows (PPSA20396), 25 min. Eingeschwungen: Prozessor 66 °C,
Lüfter 52 %, die Kanäle liegen zwischen 53,6 und 61,2 °C. Gezeigt ist der
Abstand jedes Kanals zum Mittel aller acht, damit die Lüfterdrehzahl, die alle
gleich trifft, herausfällt:

| Kanal | Leerlauf | Spiel | Unterschied |
|---|---|---|---|
| **7** | +0,2 | **+4,7** | **+4,4** |
| 0 | −0,7 | +1,4 | +2,1 |
| 2 | −0,5 | +1,2 | +1,7 |
| 5 | +0,9 | +0,6 | −0,4 |
| 4 | −0,1 | −1,1 | −1,0 |
| 3 | −0,6 | −2,6 | −2,0 |
| 6 | +0,9 | −1,1 | −2,1 |
| **1** | −0,1 | **−3,0** | **−2,8** |

Gegen die Hypothese aus Abschnitt 0 (Kanal 1 = Grafikschiene): Unter einem
grafiklastigen PS5-Spiel wird Kanal 1 von allen am **wenigsten** warm.

### Reine Rechenlast: für Payloads nicht erzeugbar

Gemessen mit sieben Fassungen eines Lastprogramms (`last*.c` im Messordner):

| Befund | Beleg |
|---|---|
| Payloads laufen auf **5 von 16** logischen CPUs: 9, 11, 13, 14, 15 (Maske `0xea00`). Das sind die CPUs des Systems, der Startbildschirm läuft dort ebenfalls | `cpuset_getaffinity`, Wurzelmenge `0xffff` |
| Die übrigen elf (`0x15ff`, die der Spiele) sind gesperrt. Eine eigene CPU-Menge lässt sich zwar anlegen, jede Bindung eines Threads dorthin scheitert mit **EPERM** | Version 4 |
| Threads starten mit fester Priorität **700, FIFO** (`rtprio` Typ 10). Gleichrangige Threads teilen sich die CPU nicht reihum | Version 2, siehe Vorfall |
| Leerlaufklasse (`RTP_PRIO_IDLE`) **EINVAL**, Zeitscheibenklasse abgelehnt. Die niedrigste feste Priorität **767** wird angenommen und wirkt: Die App antwortete daneben im Mittel in 10–16 ms | Versionen 3, 5, 6 |
| `cpuset_*affinity` akzeptiert nur 16 Byte als Größe | Version 2 |
| **Last ohne Wärme:** 4 Threads auf 4 der 5 CPUs, je 6 min Ganzzahl- und AVX2-FMA-Last, gemessen 3,5 Kerne (1264 bzw. 1261 s Rechenzeit in 360 s). Kein Kanal und nicht der Prozessor bewegte sich um mehr als 0,6 °C, das Kanalmuster blieb das des Leerlaufs | Versionen 6 und 7 |

Damit ist der geplante Vergleich „Spiel gegen Busy-Loop" nicht durchführbar.
Ob Kanal 7 an der GPU oder am Rechenwerk sitzt, bleibt offen. Naheliegend ist
die GPU, weil sie im Spiel den Großteil der Leistung zieht, aber belegt ist
das nicht.

⚠ **Vorfall:** Version 2 startete 16 Threads mit der Vorgabe-Priorität. Sie
belegten die fünf System-CPUs vollständig. Die App, der Lader, klog, FTP und
der eigene Haupt-Thread, der die Last nach 6 min beenden sollte, bekamen keine
Rechenzeit mehr, und am Ende fror auch der Startbildschirm ein. Nur hartes
Ausschalten half. Seitdem gilt für jedes Lastprogramm:
- Jeder Thread beendet sich an der Frist selbst.
- Jeder Thread stuft sich vor dem Rechnen auf Priorität 767 herunter, liest das nach und beendet sich sonst.
- Eine der fünf CPUs bleibt frei.
- Zuerst läuft eine Probe von 10 s.

### Nebenbefunde zur App

Beide sind in 1.45.0 erledigt (16 CPUs, Takt live).

- **Kernlast zeigt nur die CPUs 0–7.** `platform.c` wertet die
  Leerlauf-Threads `SceIdleCpu0`…`7` aus. Die Last der Programme auf 9, 11, 13
  und 14 erschien dort nie, die Anzeige blieb bei 0 %. Zu prüfen ist, ob es
  `SceIdleCpu8`…`15` gibt. Wenn ja, sind die „Kerne 1–8" der Oberfläche in
  Wahrheit die logischen CPUs 0–7, also die Spielkerne 0–3.
- **Takt ist ein Schnappschuss:** `clocks.age_s` stand bei 489 s. Die
  Taktwerte sind also nicht live.

### Nächster Schritt

Den Vergleich andersherum führen: **reine Grafiklast gegen das Spiel.**
`ps5-agc-gears` (siehe „Reine Grafiklast ist jetzt vorhanden") rendert
dauerhaft über AGC und belastet die CPU kaum. Folgt das Kanalmuster unter
Gears dem des Spiels, sitzt Kanal 7 an der GPU.

⚑ **Erledigt ohne Gears:** Seit 1.45.0 liegt die Grafikleistung neben den
Kanälen im Ring. Ein zweites Spiel mit wechselnder Grafiklast hat die Frage
beantwortet, siehe nächster Abschnitt.

---

## Kanaltest 27.09.2026 — zweites Spiel: Kanal 7 sitzt an der Grafik

App 1.45.0, PS5 Pro (CFI-7021), FW 12.00. Aufzeichnung 01:29–03:27 Uhr: jede
Sekunde der Status, alle 5 s der Kanal-Ring mit Grafik- und Speicherleistung.
Rohdaten, Auswerteskripte und Bericht liegen **außerhalb** des Repos unter
`Messungen/2026-09-27 Kanaltest zweites Spiel/`.

| Zeit | Zustand | GPU | Lüfter |
|---|---|---|---|
| 01:29–02:03 | Assassin's Creed Shadows (PS5) | 100 W am Deckel | 52–55 % |
| 02:03–02:05 | Startbildschirm | 3 W | 20 % |
| 02:05–02:07 | Tetris Ultimate (PS4) | 12–15 W, 60 Bilder/s | 34 % |
| 02:07–03:27 | Two Point Hospital (PS4) | 2–15 W, 30 Bilder/s | 34 % |

In Two Point Hospital stand der Lüfter 80 min fest bei 34 %, die Spiel-Last
lag bei 1–10 %. Damit bewegt fast nur die Grafikleistung die Temperaturen: die
saubere Bedingung, die der Vergleich mit einem Busy-Loop liefern sollte.

### Drei Sichten, eine Reihenfolge

**1. Muster.** Abstand jedes Kanals zum Mittel der acht. Der Kühlkörper hebt
alle gleich an und fällt heraus:

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | **7** | CPU |
|---|---|---|---|---|---|---|---|---|---|
| AC Shadows 27.09. | +1,3 | −3,2 | +1,1 | −2,9 | −1,1 | +0,7 | −0,7 | **+4,9** | +10,1 |
| AC Shadows 26.09. | +1,1 | −2,9 | +1,1 | −2,2 | −1,0 | +0,6 | −0,8 | **+4,2** | +9,0 |
| Startbildschirm | −0,8 | −0,9 | −0,8 | −0,9 | −0,5 | +1,4 | +1,6 | +0,9 | +1,6 |
| Two Point Hospital | −0,8 | −0,9 | −0,6 | −1,0 | −0,5 | +1,4 | +1,4 | +1,1 | +1,5 |

**2. Sprung beim Beenden von AC Shadows.** GPU 102 → 3 W, eine Minute vorher
gegen eine Minute nachher. Alle Kanäle fielen innerhalb von 15 s, keiner ist
träge:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | **7** | CPU |
|---|---|---|---|---|---|---|---|---|
| 14,8 | 9,9 | 14,3 | 10,5 | 11,5 | 11,5 | 10,0 | **16,5** | 20,8 |

**3. Regression in Two Point Hospital.** Kanal minus Kanalmittel gegen die
Grafikleistung (15 s geglättet), 942 Proben, Fehler nach Newey-West:
**Kanal 7 +0,115 ± 0,019 K/W**, sechs Standardfehler. Alle übrigen Kanäle
und der Prozessor bleiben innerhalb von etwa zwei Standardfehlern um null.
Der GPU-Sprung um 02:08:10 (+12 W, Lüfter fest) zeigt dasselbe: Kanal 7
+2,3 °C, Kanal 0 +2,0, Kanal 2 +1,9, die übrigen +1,1 bis +1,6.

**Reihenfolge der Grafiknähe: 7 > 0 ≈ 2 > 4, 5 > 3, 1, 6.**

### Weitere Befunde

- **Der Prozessor folgt der Grafik nicht** (Regression wie oben: nicht
  stärker als das Mittel) und liegt im fordernden Spiel trotzdem 10 °C über
  den Kanälen. Der heißeste Punkt des Chips sind die Rechenkerne, selbst in
  einem grafiklastigen Spiel. Die Regelung auf `max(CPU, Hauptchip)` sitzt
  damit richtig.
- **Kanal 0 ist nicht gedämpft** (Sprung wie alle, deckungsgleich mit
  Kanal 2). **Kanal 1** liegt im Leerlauf im selben Band wie die Kanäle 0, 2,
  3 und 4 und heizt unter Grafiklast am wenigsten: ein Chip-Sensor, kein
  Spannungswandler. **Kanal 2** zeigt ohne M.2-Laufwerk Chip-Temperaturen und
  fällt in 15 s um 14 °C: keine SSD. Folge: 1.45.1.
- **Kanäle 5 und 6** sind im Leerlauf die wärmsten und heizen unter
  Grafiklast unterdurchschnittlich. Sie liegen vermutlich auf der Seite der
  Rechenkerne oder des immer aktiven Teils. Belegt ist das nicht.
- **Die CPU-Schiene steht auch in PS4-Spielen still.** Sie bewegt sich nur in
  den ersten rund 3 min nach einem Spielstart und bleibt dann bei 17,5 W
  stehen, die Zusatzwörter ebenso. Das bestätigt die Prüfung `cpu_live`.
- **Grafikspeicher:** Nach einem PS5-Spiel bleibt der GDDR6 einige Minuten
  bei 42 W, danach 36 W.

Offen: Gilt die Zuordnung auch für die Standard- und die Slim-Konsole? Deren
Chip ist ein anderer. Bis dahin zeigt die App die Grafiktemperatur nur auf
der Pro.

---

## Konsolentest 24.09.2026 — v1.43.0 an der Hardware, Fixes in 1.43.1

74 Minuten auf der Testkonsole (FW 12.00): Leerlauf, vier Spiele (alles PS4-Titel über
ShadowMountPlus), mittendrin ein Ruhemodus. Die Rohdaten liegen **außerhalb**
des Repos unter `Messungen/2026-09-24 Konsolentest v1.43.0/`, weil sie
WLAN-Namen und Benutzerkennungen enthalten.

### Was belegt ist

| Thema | Befund |
|---|---|
| **Wächter** | ✅ **Elf** Überschreibungen erkannt, alle auf **91 °C**, jede beim ersten Versuch zurückgeholt: der Zähler stieg je Ereignis nur einmal, und beim nächsten Rücklesen stand wieder der eigene Wert. Protokolliert wird höchstens einmal pro Minute, wie vorgesehen |
| **Wann die Firmware zurücksetzt** | ⚑ **Bei jedem Zustandswechsel**, nicht nur beim Spielstart: Spielstart, Spielende, Wechsel in den Ruhemodus und zurück. Abgeglichen mit dem Zustandsprotokoll von ShadowMountPlus. Der eigene Wert der Firmware war jedes Mal **91 °C** |
| **ShadowMountPlus** | Läuft auf der Konsole (v1.7beta1) und bringt eine eigene Lüftersteuerung mit, steht aber auf `fan_target_temperature=system` und schreibt **nicht**. Es regelt also kein zweites Programm mit |
| **28-Byte-Block** | ✅ Beantwortet, siehe „Der Lesebefehl der Lüftersteuerung": nur Byte 5 bewegt sich, der Rest sind Einstellungen |
| **Vollbild-Knopf** | ✅ Beantwortet: Der PS5-Browser bietet die Vollbild-Schnittstelle **nicht** an. Der Knopf blendet sich wie vorgesehen aus, der automatische Versuch beim ersten Tastendruck wird gar nicht erst scharf geschaltet. Hilft nur im PC-Browser |
| **Ruhemodus** | ✅ Die Korrektur vom 07.09. hält: `http_accept_failed` mit Fehler 163, und **5 ms** später war der Zugang neu geöffnet. Die Lüfterregelung lief bis ins Einschlafen hinein weiter und korrigierte dabei noch eine Überschreibung. Eingefroren war die App nur rund 14 s, also unter der 20-s-Schwelle der Aufwach-Erkennung, und eine neue Rechteausweitung war auch nicht nötig. klog-Verbindungen sterben im Ruhemodus still und müssen neu aufgebaut werden |
| **Spielerkennung** | ✅ Start, Ende und Title-ID (`appinfo`) bei allen vier Spielen. ⚠ Aber zwei Fehler, siehe unten |
| **Zusatzabfragen** | ✅ Alle an (`probe_mask` 31), die App hat Spiele und Ruhemodus überstanden. Netzwerk liefert (WLAN, 2,4 GHz, 100 %). Bildschirm nur ohne Spiel (3840×2160), denn während eines Spiels lässt sich der Bildausgang nicht öffnen |
| **Sendeverbindung** | ⚑ Die App **überlebt** inzwischen, wenn die Sendeverbindung abreißt. Windows kappt eine halb geschlossene Verbindung nach 120 s Stille, und die App lief weiter. Die Notiz vom 01.08. („stirbt mit der Verbindung") gilt mit dem heutigen Lader nicht mehr |
| **Schwarzes Bild** | Beim ersten Spielstart verhandelte die Konsole HDMI neu: 4K, HDR, Spielmodus (ALLM), Samsung-Fernseher von 2019. Es war das einzige HDMI-Ereignis der Sitzung, nach 25 h ungestörter Verbindung. Ruhemodus und Aufwecken halfen. Die App taucht im Protokoll der Bildsteuerung nicht auf. Seit 1.43.1 fasst sie den Bildausgang während eines Spiels ohnehin nicht mehr an |

### Gefunden und in 1.43.1 behoben

| Fundstelle | Was falsch war | Behebung |
|---|---|---|
| `gamestate.c` | Die Systemoberfläche wurde fest als Fokus `0x7` erkannt. Nach dem Ruhemodus hieß sie `0x2007`, denn Anwendungskennungen tragen oberhalb der unteren 13 Bit eine Generation (Spiele: `0x18`, `0x2018`, `0x4018`, `0x6018`). Folge: Der Startbildschirm galt als laufendes Spiel „Unbekannter Titel (0x2007)" | Vergleich nur auf den unteren 13 Bit. Zusätzlich fragt die App den Kernel, ob der Fokusinhaber überhaupt ein Spiel ist (Title-ID PPSA/CUSA). Wenn nicht: „kein Spiel" |
| `gamestate.c` | Die Fokuszeile fiel nach **etwa 30 Minuten Spiel** aus dem 512-KB-Kernelpuffer (im Spiel rund 290 Byte/s, vor allem Speicherberichte alle 2 s). Ab da hieß es „kein Spiel", obwohl eines lief, und ein **Spielprofil wäre mitten im Spiel abgeschaltet** worden. ⚑ Wann das eintritt, hängt allein von der Protokollmenge ab: Am 25.09., nach einem Neustart, schrieb die Konsole nur ≈ 24 Byte/s, dann wären es Stunden | Der letzte gesehene Fokus wird gemerkt. `/api/v1/status` meldet dann `game.focus_remembered: true`. An der Konsole bestätigt (Abschnitt 0) |
| `gamestate.c` | `SetControllerFocus(-1)`, der Moment zwischen zwei Besitzern, wurde als `0xFFFFFFFF` gelesen und damit als „Spiel" | wird übersprungen |
| `gamestate.c` (Nachtrag 25.09.) | Manche PS5-Spiele hinterlassen doch `/user/app/<TITLEID>` im Kernelpuffer (Double Dragon Revive, PPSA23000). Die Notiz vom 01.08. hatte das nie gesehen, die PS4-Titel vom 24.09. taten es auch nicht. Die Zeile bleibt stehen, bis sie hinausrutscht. Das Spiel war um 14:49 beendet, und sechs Minuten später meldete die App es noch als „pausiert" | Wo der Kernel antworten kann, entscheidet er in jedem Fall, ob ein Spielprozess lebt. Protokolltitel und Fokus-ID sind nur noch der Rückfall ohne Kernelweg. An der Konsole bestätigt: vorher „pausiert PPSA23000", danach „kein Spiel" |
| `netdisp.c` | Die Bildschirmabfrage öffnete alle 5 s den Bildausgang, auch während eines Spiels, wo sie nie antwortete | Solange ein Spiel im Vordergrund läuft, wird nicht gefragt |
| `fan.c` | Protokolltext „das passiert beim Start eines Spiels" | „… das tut sie bei jedem Wechsel, etwa beim Starten oder Beenden eines Spiels und beim Ruhemodus" |

### Offen aus diesem Test

- **1.43.1 an der Konsole bestätigen** und **Werkswert beim Einschalten**: siehe Abschnitt 0.
- ~~**Neutralwert 65 °C oder 91 °C?**~~ ✅ **Entschieden am 25.09.2026, eingebaut
  in 1.43.2:** Beim Abschalten der Automatik bekommt die Konsole 91 °C zurück,
  ihren eigenen Wert, statt der 65 °C der Referenz-Payloads. Dafür musste die
  unterste Schreibgrenze von 90 auf 91 steigen, sonst wären still 90 angekommen.
  Nutzer und Regler bleiben weiter auf 45–80 °C begrenzt. An der Konsole
  geprüft: Automatik aus → die Lüftersteuerung liest 91 zurück; Automatik
  wieder an → die App übernimmt sofort.
- **Wächter beim Einschlafen:** Er korrigierte noch während
  `SUSPEND_ON_GOING`. Harmlos, aber genau der Fall für die
  **Kernel-Ereignisflaggen** (siehe offene Funde).
- ~~**Titelnamen für PS4-Titel**~~ ✅ **Eingebaut in 1.43.3:** PS4-Titel haben
  keine `param.json`, das System legt ihre Daten als `param.sfo` unter
  `/system_data/priv/appmeta/<CUSA…>/` ab. Die App liest daraus `TITLE` und
  `APP_VER`. Geprüft an allen vier PS4-Titeln der Konsole, live mit Red Dead
  Redemption 2.

### Bauen ohne WSL

`tools/build-windows.sh` baut unter Windows mit Git Bash, GNU Make und
**LLVM 21**, siehe README. Gegenprobe: Der Stand v1.43.0 wurde damit
nachgebaut und ist bis auf die zufällige Build-ID (16 Byte) **bytegleich** mit
dem Release aus WSL. ⚠ Nicht LLVM 18, obwohl die alte SDK-Doku das nennt.
Damit entsteht ein ELF mit OS/ABI „System V" und anderem Dynamik-Layout, genau
die Abweichung, die in der README als Startfehler der allerersten Fassung
steht.

---

## Behoben am 07.09.2026 — vier Fehler aus dem Quellenrundgang

Keiner stand auf dieser Liste. Sie fielen auf, während achtzehn Auswerter
fremde Quellen **gegen den eigenen Code** prüften. Drei davon waren stumm: sie
sahen aus wie ein fehlender Wert, nicht wie ein Fehler — genau die Sorte, die
sonst jahrelang stehenbleibt.

| Fundstelle | Was falsch war | Wirkung und Behebung |
|---|---|---|
| `http.c`, `ps5tm_http_run()` | `select()` auf einem toten Zugang wartet nicht, es kehrt **sofort** mit −1 zurück. `rc <= 0 → continue` war damit keine Sekundenpause mehr, sondern eine leere Schleife | **Ein Kern auf 100 % — in einer Kühlungs-App**, während die Oberfläche stumm blieb und die Regelung weiterlief. Jetzt wird der Zugang zurückgegeben; die Bindeschleife in `main.c:317` öffnet einen neuen. Die Maschinerie dafür stand längst, es fehlte allein die Ausstiegsbedingung |
| `web/app.js`, `api()` | `fetch` ohne Frist | Nach Ruhezustand oder Netzabriss lief **kein** Aufruf je in den Fehlerzweig: alte Zahlen blieben stehen, die Ampel meldete weiter „Verbunden". Falsche Werte sehen überzeugender aus als gar keine. Jetzt 8 s Frist über `AbortController`, mit `timeoutMs` übersteuerbar |
| `web/app.js`, Sekundentakt | `setInterval(refresh, 1000)` startete jeden Takt unabhängig vom vorigen | Hängende Abfragen stapelten sich, und weil jede für sich noch wartete, wurde der Fehlerzweig nie erreicht. Jetzt verkettet: ein Durchgang zur Zeit, ein überzähliger Takt fällt aus |
| `web/app.js`, Region | `Number(region_code)` gegen eine Zahlen-Tabelle — die Konsole liefert aber **Text** | Die Zeile „Region (Best-Effort)" wurde seit ihrer Einführung **kein einziges Mal** gezeichnet. Deutungstabelle entfernt statt auf Text umgestellt: was die Konsole hineinschreibt, ist unbelegt, und ein falscher Name lädt zu Entscheidungen ein. Der Rohwert bleibt sichtbar |
| `sysinfo.c` + `ps5tm.h` | nur `usb0`/`usb1`, nur `/mnt/ext1`, dazu `PS5TM_MAX_VOLUMES 6` | Ein Datenträger an `usb2` oder aufwärts (Hub, zweites Laufwerk) und eine Erweiterung auf `ext0` waren unsichtbar. Jetzt `usb0`–`usb7`, beide M.2-Steckplätze, Grenze auf 12. Ein nicht vorhandener Pfad kostet ein `statfs()`, das sofort scheitert |

**Merksatz:** Der Rundgang hat mehr über den eigenen Code verraten als über die
fremden Quellen. Wer fremde Projekte gegen das eigene prüfen lässt, bekommt die
Gegenprobe geschenkt.

---

## Quellenrundgang 07.09.2026 — 120 Funde, 22 belegt

Achtzehn Quellcluster im Materialarchiv, jeder Fund von zwei unabhängigen
Prüfern mit **Widerlegungsauftrag** gegengelesen: einer öffnet die zitierte
Datei und prüft das Zitat wörtlich, der andere sieht im eigenen Quelltext nach,
ob die App es längst kann oder ob es auf der Verworfen-Liste steht. 98 Funde
sind daran gescheitert — das ist die eigentliche Leistung des Verfahrens.

### Beantwortete Fragen

| Offene Frage | Stand |
|---|---|
| Ist-Zustand der Lüftersteuerung lesen und rückprüfen | **beantwortet** — `0xC01C8F08`, siehe unten |
| Region und Sprache deuten | **beantwortet, noch nicht gebaut** — `USER_01_16_NP_country_code` und `NP_language_code` sind Text, nicht Zahlen: Schlüssel `125874190 + (n-1)·65536` bzw. `125874191 + (n-1)·65536`, Puffer **genau** 3 bzw. 6 Byte. Den Steckplatz `n` findet man über `sceUserServiceGetForegroundUser` und `USER_01_16_user_id` (`125829376 + (n-1)·65536`). ⚠ Der Fund belegt die Schlüsselnummer, **nicht** dass die Werte gefüllt sind — genau wie bei den leeren SSD-Zählern |
| Zeitzone deuten | **nicht beantwortet** — bleibt eine Zahl |
| Takt drosseln statt kühlen | **halb** — die drei gesuchten Namen sind echte Exporte, dazu eine Pstate-Familie und `ChangeNumberOfGpuCu`. Semantik, Parameter, Wertebereiche: **null Beleg**, kein einziger Prototyp |
| Trennt ein Sensorkanal Grafik- von Rechenlast? | **beantwortet am 27.09.2026, per Messung statt Fund:** Kanal 7 sitzt an der Grafik (PS5 Pro), siehe „Kanaltest 27.09.2026". 26.09.: Die Rechenlast-Hälfte ist für Payloads nicht machbar, siehe „SoC-Kanaltest 26.09.2026" |
| Die sieben Sensoraufrufe schwach linken | **nicht berührt** |

### Der Lesebefehl der Lüftersteuerung (eingebaut 07.09.)

`0xC01C8F08` liegt direkt neben dem bekannten Schreibbefehl `0xC01C8F07`:
gleiche Gerätedatei, gleiche 28 Byte, gleiches Byte 5. Zwei voneinander
unabhängige Projekte liefern ihn aus — `fan_target` 0.1 (`main.c:21`, läuft
alle zwei Sekunden) und ShadowMountPlus, **unverändert über drei Fassungen**
(alpha10 bis alpha13, `src/sm_fan.c:10-19`). Die Kodierung bestätigt sich
selbst: `_IOWR`, Längenfeld `0x1C` = 28, Gruppe `0x8F`.

⚠ **Kein Nur-Lese-Befehl.** `_IOWR` heißt, der Kern kopiert 28 Byte **hinein**
und heraus, genau wie beim Schreiben. Also derselbe Umgang: Puffer exakt 28
Byte, vorher genullt.

Eingebaut ist vorerst **nur die Diagnose**: `ps5tm_fan_init()` liest einmal,
schreibt alle 28 Byte ins Protokoll und tut sonst nichts damit. Der Zeitpunkt
ist der Punkt — es ist der einzige Moment im ganzen Lauf, in dem der Block noch
den Werkswert enthält; die Sondierung zwei Zeilen später überschreibt ihn, und
bisher geschah das, **bevor je jemand hingesehen hatte**. Genau deshalb stellt
das Beenden fest verdrahtete 65 °C wieder her.

Was danach möglich wird, in dieser Reihenfolge: geschriebene Schwelle
bestätigen statt annehmen · den wirksamen statt den zuletzt gesendeten Wert
anzeigen · das blinde Nachschreiben alle 15 s durch einen Vergleich ersetzen ·
beim Beenden den echten Ausgangswert zurückgeben.

#### ✅ An der Hardware gemessen — 07.09.2026, FW 12.00, Testkonsole

Der Befehl **antwortet**. Damit ist die Frage beantwortet, und zwar positiv:

```text
icc_fan_config_read
00 00 00 00 00 50 00 00 6E 29 00 00 1F 00 00 00 FF FF FF 0F FF FF FF 0F FF FF FF 0F
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27
```

Was daraus **belegt** ist:

- **Byte 5 = 0x50 = 80** — die Schwelle, zum ersten Mal von der Leseseite her
  bestätigt. Bisher war die Bedeutung dieses Bytes nur aus dem Schreiben
  erschlossen.
- **Es ist ein echter Registerinhalt, keine Konstante und kein Puffermüll.**
  Der Beweis fällt nebenbei ab: die App wollte in dieser Sekunde 65 °C
  schreiben (`fan_threshold_c` in der Konfiguration), gelesen wurden aber 80 —
  genau der Wert, an dem der eigene Regler oben anschlägt und den die
  **vorherige Ausführung** hinterlassen hatte.
- **Byte 2 kommt als 0 zurück** — der Wert, den ShadowMountPlus vor dem Lesen
  hineinschreibt und „Selektor" nennt. Über die Bedeutung sagt das nichts.

⚠ **Korrektur an der eigenen Meldung.** Der Text sagte zuerst
„Werkseinstellung meldet 80 °C". Das ist falsch, und die Messung hat es selbst
widerlegt: die Firmware behält den zuletzt geschriebenen Wert bis zum nächsten
Spielstart, „vor dem Schreiben" heißt also nur „bevor **dieser** Prozess
schreibt". Den unberührten Wert der Konsole sieht man nur **nach einem
Neustart, bevor die App in dieser Sitzung je lief**. Einmal nachzuholen.

Noch unbekannt, aber jetzt billig zu klären — die übrigen Bytes:

| Bytes | Roh | Als Zahl | Vermutung |
|---|---|---|---|
| 8–9 | `6E 29` | 10606 (16 bit) bzw. 110 und 41 | offen. Byte 9 = 41 entsprach der CPU-Temperatur im Moment des Lesens — reizvoll, aber **ungeprüft** |
| 12–15 | `1F 00 00 00` | 31 | offen |
| 16–27 | dreimal `FF FF FF 0F` | dreimal 0x0FFFFFFF | sieht nach drei unbelegten Feldern auf Maximalwert aus |

**Der nächste Schritt kostet fast nichts:** den Block zweimal lesen, einmal im
Leerlauf und einmal unter Last. Bewegen sich Bytes 8–9 oder 12 mit Temperatur
oder Drehzahl, enthält die Lüftersteuerung **Messwerte**, die das Projekt bisher
nirgends herbekommt. Bleiben sie stehen, sind es Einstellungen — auch das eine
Antwort.

#### ✅ Beantwortet am 24.09.2026 — es sind Einstellungen

Seit 1.43.0 hängt die Kanalaufzeichnung den ganzen Block an jeden Messpunkt.
Ergebnis: 881 Messpunkte über 74 Minuten, Leerlauf und vier Spiele, CPU 37–48 °C,
Lüfter 20–34 %, kein einziger fehlgeschlagener Lesevorgang. Es bewegt sich
**nur Byte 5** (65–91 °C, einschließlich eines festgehaltenen 91-°C-Moments der
Firmware). Byte 8–9 (`6E 29`), Byte 12 (`1F`) und die drei `FF FF FF 0F` stehen
die ganze Zeit still, mit genau den Werten vom 07.09. Byte 9 = 41 traf damals
zufällig die CPU-Temperatur. Diesmal lag die CPU bei 37–48 °C, und das Byte
blieb 41.

In der Lüftersteuerung steckt also **kein** zusätzlicher Messwert. Einschränkung:
Es war nur leichte Last. Ein Byte, das erst jenseits von 60 °C reagiert, fiele
erst unter Volllast auf. Das ist unwahrscheinlich, aber nicht ausgeschlossen.

### Die stärksten noch offenen Funde

| | Fund | Aufwand | Sicher? |
|---|---|---|---|
| `[ ]` | **Web-Oberfläche komprimiert einbetten** — gemessen 246.510 → 62.356 Byte, das ELF von 444 KB auf rund 260 KB. Betrifft `tools/gen_assets.py` und eine Stelle in `http.c`. ⚠ Der Referenzcode liefert die Kompression bedingungslos aus, ohne den `Accept-Encoding`-Kopf zu prüfen | klein | belegt |
| `[ ]` | **Kachel-Paket schrumpft von 9,65 MB auf 1,39 MB**, wenn `pic0.png`/`pic1.png` entfallen — byte-genau nachgerechnet. Damit fällt der einzige dokumentierte Grund für den getrennten Installer-Payload weg. Einbuße: kein eigenes Hintergrundbild in der Startoberfläche | klein | belegt, Heap-Test steht aus |
| `[ ]` | **Kachel ganz ohne Paket** — `sceAppInstUtilAppInstallTitleDir` (NID `Wudg3Xe3heE`). Ein lauffähiges Beispiel liegt **im eigenen SDK**: `PS5_PAYLOAD_SDK/samples/install_app/payload.c`. Kein .NET, keine Paketkette, kein zweiter Payload. ⚠ Die Referenz ruft **vor** der Installation `sceAppInstUtilAppUnInstall` — bricht es dazwischen ab, ist die vorhandene Kachel weg | mittel | belegt, auf FW 12.00 ungeprüft |
| `[ ]` | **Freigabeprüfung am gebauten ELF** — `llvm-readobj --needed-libs` zeigt, welche `.sprx` das fertige ELF wirklich anfordert. Hätte den Startfehler von 1.7.1 gefangen (fehlendes `libSceIpmi.sprx`, in der archivierten Fassung nachgewiesen). ⚠ Den *zweiten* Startfehler — feste Symbolbindung in `tile.c` — hätte es **nicht** gefangen | klein | belegt |
| `[ ]` | **Kernel-Ereignisflaggen** — echter Systemzustand (`WORKING` / `SUSPEND_ON_GOING` / `SHUTDOWN_ON_GOING`) ohne blockierendes IPC. Nach reinem Nutzen der zweitstärkste Fund: erlaubt, die Aufzeichnung sauber zu pausieren, die Verschleißerkennung von Standby-Phasen zu trennen und die Schwelle **vor** dem Ausschalten zurückzugeben. Setzt den Lesebefehl oben voraus, sonst weiß niemand, worauf zurückzustellen wäre. **24.09.2026:** Worauf zurückzustellen wäre, ist jetzt bekannt, nämlich 91 °C, den eigenen Wert der Firmware. Und der Bedarf ist belegt: Der Wächter korrigierte noch während `SUSPEND_ON_GOING`. ShadowMountPlus liest dieselben Zustände bereits mit (`SceSystemStateMgrInfo`) | mittel | belegt |
| `[ ]` | **Automatischer Start** — wer onionHEN nutzt, legt die ELF nach `/data/OnionHEN/payloads/` und daneben eine leere `PS5_Cooling_Center.elf.auto_start`. Gehört in die LIESMICH. ⚠ Gilt nur für onionHEN; zusammen mit der Altinstanz-Übernahme können kurzzeitig zwei Regler laufen | klein | belegt |
| `[ ]` | **Sensorkanäle über 7 sondieren** — Abbruch bei `EINVAL` (`0x80020016`) statt fest acht anzunehmen. Billiges Experiment mit binärem Ausgang: antworten Kanäle 8–15, gibt es neue Messwerte und mehr Kandidaten für die Grafik/Rechen-Frage. ⚠ Einmalig beim Start hinter ein Diagnose-Bit, nie in die Sekundenschleife | klein | belegt, dass fremder Code bis 16 sondiert — **nicht**, dass es mehr als acht gibt |
| `[ ]` | **Zweiter Startweg über `homebrew.js`** — drei Textfelder plus das vorhandene Symbolbild, und die App erscheint in der Homebrew-Liste von `ps5-payload-websrv`. Ersetzt die Kachel nicht, ergänzt sie | klein | belegt |

### Reine Grafiklast ist jetzt vorhanden

`ps5-agc-gears` ist eine native Anwendung, die die GPU über AGC dauerhaft
rendert — belegt mit einem 60.000-Frame-Lauf auf FW 12.02 — und dabei die CPU
kaum belastet. Damit existiert erstmals das Werkzeug für die offene Frage, ob
ein Sensorkanal Grafik- von Rechenlast trennt: Gears gegen einen
Busy-Loop-Payload auf acht Kernen, Kanäle vergleichen. Der Rundgang selbst hat
die Frage **nicht** beantwortet; er hat nur das Messmittel geliefert.

⚑ **26.09.2026:** Den Busy-Loop auf acht Kernen gibt es nicht. Payloads dürfen
nur auf die fünf System-CPUs, und dort erzeugt selbst AVX2-Last keine messbare
Wärme (siehe „SoC-Kanaltest 26.09.2026"). Gears wird deshalb mit dem Spiel
verglichen statt mit einem Payload.

⚑ **27.09.2026:** Für die Kanalfrage nicht mehr nötig. Sie ist mit zwei
Spielen beantwortet, siehe „Kanaltest 27.09.2026".

### Was der Rundgang nicht abgedeckt hat

Drei Lücken, damit sie nicht als erledigt gelten:

1. **Kernel-Offsets und Firmware** — der Auswerter wurde von einem
   Sicherheitsfilter abgebrochen. Cluster komplett unbearbeitet.
2. **API-Mapper** — beide Prüfer fielen aus. Zwei Funde liegen **ungeprüft**
   auf Halde: der Kernel-Gerätebaum über `KERNEL_ADDRESS_BUS_DATA_DEVICES`
   (steht laut Auswerter im eigenen SDK bereit) und übernehmbare
   Sicherheitsgeländer für Kernel-Durchläufe.
3. **Vollständigkeitskritik** — am Abrechnungslimit gescheitert. Es gibt also
   keine unabhängige Prüfung, welches Material kein Auswerter angefasst hat.

---

## Neu in 1.19–1.21 — alles aus dem Kernelprotokoll

Das Systemprotokoll hat sich als die ergiebigste Quelle des Projekts erwiesen: Es liefert Auskünfte, die die zugehörigen Schnittstellen verweigern.

| | Was | Stand |
|---|---|---|
| `[x]` | **Controller-Akku** (1.19.x) — `Battery level : N` im Protokoll, **0–100 %**, an einem vollen (85) und einem leeren (5) Controller geeicht. `scePad` bleibt versperrt und wird nicht gebraucht. Ereignisgesteuert, daher mit Altersangabe | läuft |
| `[x]` | **Taktraten** (1.20.0) — `SceSystemStateMgr`-Tabelle mit **8 Kerntakten und dem Grafiktakt**. 900 MHz im Leerlauf, **2230 MHz** unter Last = der offizielle GPU-Höchstwert, was die Spaltenzuordnung unabhängig bestätigt | läuft |
| `[x]` | **Arbeitsspeicher** (1.21.x) — `VM Stats` und `FMEM` je Prozess. War in 1.9.5 als „Syscall liefert nichts" entfernt worden; im Protokoll stand es die ganze Zeit. ⚠ Liste bei jeder `VM Stats`-Zeile zurücksetzen, sonst steht die **älteste** Runde im Puffer da | läuft |
| `[x]` | **Energiezustand** (1.21.x) — `Power Mode Change` (`NAVIGATION_ACTIVE_1` ↔ `BIG_APP`) und Leerlaufdauer | läuft |
| `[ ]` | **Akkuwert dauerhaft speichern** — nach einem Neustart der App ist er weg, bis ein Controller wieder eingeschaltet wird. Mit Altersangabe wäre auch ein alter Wert ehrlich | klein |

**Merksatz aus dieser Reihe:** Bevor eine Auskunft als unmöglich gilt, prüfen, ob die Konsole sie **nebenbei ausplaudert**. Dreimal hintereinander lag die Antwort im eigenen Log.

---

## Neu in 1.16–1.18

| | Was | Stand |
|---|---|---|
| `[x]` | **Alle 8 Sensorkanäle aufgezeichnet** (`chanlog.c`, `/api/v1/channels`) — und damit vermessen: **acht eigenständige Sensoren**, kein Paar mit konstanter Differenz über 152 Messpunkte. Der gegenteilige Kommentar in `api.c` stammte aus Leerlaufwerten, wo alle acht innerhalb 2 °C liegen; unter Last spreizen sie sich um 10,5 °C | gemessen |
| `[x]` | **„Hauptchip" korrigiert** (1.17.0) — zeigte Kanal 0, der bis zu **17 °C unter** der CPU liegt und viel träger reagiert. Jetzt das Maximum der Kanäle 1–7; Kanal 0 bleibt als „Hauptchip, gedämpft" sichtbar. Die Regelung war nicht betroffen, sie nahm ohnehin `max(CPU, …)`. ⚑ **1.45.1:** Die Beschriftung „gedämpft" ist entfallen. Am 26./27.09. reagierte Kanal 0 so schnell wie alle anderen, siehe „Kanaltest 27.09.2026" | behoben |
| `[x]` | **Verschleißerkennung** (1.18.0, `thermalog.c`, `/api/v1/cooling-health`) — eine Zeile pro Woche, nur Messpunkte bei Last 25–75 % **und** Lüfter 20–50 %. Weil beides konstant gehalten wird, bedeutet ein Anstieg über Wochen wirklich nachlassende Kühlung. Urteil ab 4 verwertbaren Wochen | **Zählung noch nicht an der Konsole bestätigt** |
| `[x]` | **Kanal-Heuristik als Expertenhinweis** (1.32.x, `web/app.js`) — in den Rohsensoren werden Best-Effort-Hinweise für Kanal 1/2/5 und Lastdynamik angezeigt, klar als unbestätigt markiert; mit Schalter „Nur bestätigte Rohsensorwerte" ausblendbar. ⚑ **1.45.1:** Die Hinweise zu Kanal 1/2/5 sind widerlegt und entfernt. An ihrer Stelle steht der gemessene zu Kanal 7, die Lastdynamik bleibt | UI eingebaut, keine Regelungswirkung |
| `[-]` | ~~**Kanal 1/5 = VGFX/VCORE?**~~ — die aussichtsreichsten Kandidaten (die zwei unabhängigsten nach Kanal 0), aber ein Spiel belastet Grafik und Rechenwerk gleichzeitig, und die Abkühlung trennt sie auch nicht. **26.09.:** Unter einem fordernden Spiel wird Kanal 7 am wärmsten, Kanal 1 am kühlsten. **27.09.: widerlegt.** Beide sind Chip-Sensoren, der Kanal an der Grafik ist Kanal 7, siehe „Kanaltest 27.09.2026" | widerlegt |

---

## A — Ausgewählt, noch nicht eingebaut

| | Funktion | Aufwand | Sicher? |
|---|---|---|---|
| `[>]` | **Die sieben Sensoraufrufe schwach linken** — `sceKernelGetCpuTemperature`, `…SocSensorTemperature`, `…CurrentFanDuty`, `…CpuUsageAll`, `…CpuFrequency`, `…Cpumode`, `…SocPowerConsumption` sind **fest** gelinkt. Fehlt eine davon auf einer anderen Firmware, stirbt der Payload **vor der ersten Zeile**, ohne Meldung. Schwach gelinkt fehlte höchstens ein Messwert — und das Protokoll sagte welcher | klein | ja, Muster in `profile.c`/`netdisp.c` bewährt |
| `[>]` | **Installer-ELF ans klog anschließen** — `klog_printf` aus `<ps5/klog.h>`, lesbar per `nc <ip> 3232`. Der zweite Payload hat bis heute **keinen** Diagnosekanal; als er nicht lief, tappten wir im Dunkeln | klein | ja, in 1.10.2 für die App bewiesen |
| `[-]` | ~~**Schreibstatistik der SSD**~~ — **verworfen, gemessen 01.08.2026.** Die Firmware führt `SCE_REGMGR_ENT_KEY_SYSTEM_HDD_WRITE_STATS_*` **nicht**: 8 Summenzähler und 3 Geräteplätze, alle mit `rc=0` gelesen, alle leer. Drei Kontrollschlüssel derselben Schnittstelle antworten dagegen echt (`language`=4, `user_name`="JBuser2", `nickname`="PS5-615") — der Mechanismus ist also in Ordnung, die Werte existieren nicht. Gleiches Verhalten wie die in 1.9.5 entfernten Felder | — | widerlegt |
| `[x]` | **Konsolenname** — `SYSTEM_nickname` über `sceRegMgrGetStr`, in **1.15.3** eingebaut und in der Kopfzeile sichtbar. ⚠ Die **deklarierte** Größe übergeben (65), nicht die eigene Puffergröße — mit 32 wird der Aufruf abgelehnt und liefert stillschweigend nichts | klein | läuft |
| `[ ]` | **Region, Sprache, Zeitzone** — `language` (Wert 4 gelesen, Bedeutung unbelegt), `region`, `time_zone` aus derselben Tabelle. Mechanismus erprobt; es fehlt nur die Zuordnung der Zahlen zu Namen | klein | Schlüssel belegt, Kodierung nicht |

---

## B — Möglich, aber teuer oder heikel

| | Funktion | Aufwand | Sicher? |
|---|---|---|---|
| `[ ]` | **Takt drosseln statt nur kühlen** — `sceSystemServiceChangeCpuClock` / `ChangeGpuClock` / `ChangeMemoryClock` / `SetPowerSaveLevel`. Hitze an der Quelle senken statt wegblasen. Inhaltlich die größte mögliche Erweiterung, zugleich ein Eingriff in laufende Spiele. Zweimal bewusst nicht ausgewählt, bleibt als Option | groß | Symbole belegt, Wirkung ungetestet |
| `[ ]` | **Bilder pro Sekunde (FPS)** — nur durch Injektion in den Spielprozess und Abfangen von `sceGnmSubmitAndFlipCommandBuffersForWorkload` (so etaHENs `fps_elf`). **Stabilitätsrisiko für laufende Spiele** | sehr groß | belegt, aber aufwendig |
| `[ ]` | **Overlay über dem Spiel** — dieselbe Injektionstechnik. Ohne sie bleibt nur die System-Benachrichtigung, die wir schon nutzen | sehr groß | belegt (etaHEN) |
| `[ ]` | **Offline-Konto anlegen** — die vier Registry-Schlüssel sind seit dem regdump-Fund entschlüsselt (`USER_01_16_user_name` / `account_id` / `login_flag` / `NP_env`, Schrittweite 65536). Schreibt roh in die Registry, andere Risikoklasse | mittel | Schlüssel belegt |

---

## C — Blockiert

| Was | Woran |
|---|---|
| ~~**Profile pro Spiel**~~ | **✅ GELÖST in 1.14.0.** Die Sperre war eingebildet: `sceKernelGetAppInfo(pid, …)` liefert die Title-ID direkt mit — ein Kernelaufruf, kein Dienstaufruf. Gefunden in ShadowMountPlus, das damit auf derselben Konsole läuft. Bestätigt: `PPSA01325` / „ASTRO's PLAYROOM" / Version 01.200.000 |
| **`cpu_mode` deuten** | Kodierung undokumentiert. Die Zuordnung 0 „Normal" / 1 „Boost" / 2 „Spiel" stammte ungeprüft aus fremden Payloads; die Konsole zeigte „Boost" bei 800 MHz, und der Wert 4 wurde ebenfalls schon gesehen. **Beschriftung in 1.14.1 entfernt** — ein falsches Wort lädt zu Entscheidungen ein, ein fehlendes nicht. Der Rohwert bleibt in `/api/v1/status`, damit sich die Bedeutung später unter bekannten Bedingungen ermitteln lässt |
| **DualSense-Akkustand** | **ABGESCHLOSSEN OHNE ERFOLG (01.08.2026).** Alles vor dem Öffnen gelingt, das Öffnen selbst wird mit einem **konstanten** Code abgelehnt: `scePadInit()` → 0 · `scePadSetProcessPrivilege(1)` → 0 · `scePadOpen(0x18161531, 0, 0, NULL)` → **`0x809B0081`**, unverändert mit und ohne laufendes Spiel, unverändert mit und ohne Zugriffsrecht. Vier Hypothesen geprüft und **alle widerlegt**: fehlende Verdrahtung (war echt, behoben), Exklusivzugriff des Spiels, fehlgeschlagene Initialisierung, fehlendes Prozessrecht. **‼ Ursache gefunden (aus `StonedModder/Ghostpad`): Ein Payload kann den Controller grundsätzlich nicht öffnen.** Der Zugriff gehört einem Prozess. Ghostpads `hidDumper` klinkt sich per **`PT_ATTACH SceRemotePlay`** ein und ruft `scePadOpen` dann *in dessen Kontext* auf; für andere Prozesse heißt es dort ausdrücklich „`SceShellUI` is authid-protected, `SceShellCore` and others always return `0x80920008`" — dieselbe Fehlerklasse wie unser `0x809B0081`. Es fehlt also **kein Wissen mehr**: Die Funktion verlangt **Prozessinjektion per ptrace**, dieselbe Technik wie die geparkte FPS-Anzeige, und die gehört nicht in eine Lüftersteuerung. Der Byte-Offset des Akkustands ist selbst dort nicht dokumentiert und müsste zusätzlich gemessen werden. Aus etaHENs `pad.hpp` immerhin gesichert: `scePadReadState` nimmt **zwei** Argumente (unsere Deklaration hatte drei, korrigiert in 1.14.2), und `OrbisPadData` enthält `connected` sowie `ext[16]` — dort läge der Akkuwert, **falls** das Öffnen je gelingt. Historie: | Am 01.08.2026 systematisch eingegrenzt: Die Funktion war nie verdrahtet (dlsym nach Namen, `libScePad` gar nicht gelinkt) — behoben in 1.12.1 per schwacher Bindung. `scePadInit()` liefert 0, also Erfolg. Die Benutzerkennung war falsch (`0xFF` statt `0x18161531`) — behoben in 1.12.4, wodurch sich der Fehler von `0x809B0001` auf **`0x809B0081`** änderte, die Argumentprüfung also passiert ist. **Hier Schluss:** Das SDK hat keinen ScePad-Header, weder Fehlercodes noch zulässige `type`/`index`-Werte sind dokumentiert, `0x809B` kommt im Includebaum nicht vor. Unbelegte Vermutung: Eingabegeräte hängen am Fokus, den ein Payload nicht hat. Ungenutzt: `scePadOpenExt`, `scePadOpenExt2`, `scePadGetControllerInformation`. Die Lage des Statusbytes bleibt ohnehin unbestätigt |

---

## D — Geprüft und abschließend verworfen

| Was | Warum nicht |
|---|---|
| **Lüfterkennlinie aus der Registry** | `regdump`s `regmgr.h` listet **1187 benannte Schlüssel** — davon **kein einziger** zu Lüfter oder Temperatur. Nur `fake_main_thermal_alert` und `fake_m2_thermal_alert`, zwei Debug-Schlüssel zum *Vortäuschen* eines Alarms. Damit ist dieser Weg zu. Nicht nochmal suchen |
| **`sceSystemServiceGetAppIdOfRunningBigApp`** | **Tötet den Payload**, sobald wirklich ein Spiel läuft — an der Konsole nachgewiesen, in 1.10.1 entfernt. Harmlos nur, solange kein Titel läuft, weshalb es wochenlang überlebte. Gilt sinngemäß für alle LNC/IPMI-Aufrufe aus dem Abfrage-Thread |
| **USB-Erkennung über den Systemdienst** | `sceSystemServiceUsbStorageInit`/`IsExist` sind IPC-Aufrufe und blockierten den Abfrage-Thread. Der dokumentierte Rückfall — Mountpunkt plus Mindestgröße, nach Gerät entdoppelt — reicht allein und fragt niemanden |
| **Startursache, Gerätevariante, ICC-Zähler, RAM/VRAM, Thermoalarm** | Die Aufrufe **gelingen und liefern nichts** auf dieser Firmware. In 1.9.5 entfernt statt Nullen anzuzeigen |
| **Echte GPU-Auslastung** | Existiert auf der PS5 nicht. etaHENs „GPU-Last" ist die VRAM-Belegung, im eigenen Quellcode als Schätzung kommentiert. ⚠ Gilt **nur für die Auslastung** — der GPU-**Takt** ist lesbar, siehe nächste Zeile |
| ~~GPU-Takt nicht verfügbar~~ | **✅ WIDERLEGT in 1.20.0.** `SceSystemStateMgr` schreibt seine Energiezustands-Tabelle ins Kernelprotokoll: 8 Kerntakte **und** der Grafiktakt. Gemessen 900 MHz im Leerlauf, **2230 MHz** unter Spiellast — exakt der offizielle Höchstwert der PS5-GPU, was die Spaltenzuordnung unabhängig bestätigt. Nebenbei: `sceKernelGetCpuFrequency()` meldet nur **einen** Kern, daher die irreführenden 800 MHz |
| **GPU-Temperatur getrennt** | Gibt es nicht. Prozessor und Grafik teilen sich den SoC-Sensor. Die Hardware bestätigt es: `THERMDA`/`THERMDC` am APU liegen auf GND, die externe Thermodiode wird gar nicht benutzt |
| **In NVS/NOR schreiben** | `CpuClock`, `GfxClkDfllDeterminism`, `bapm table` liegen im Boot-Bereich des Serial-Flash. Ein fehlerhafter Schreibvorgang macht die Konsole **unbrauchbar**, ohne Weg zurück außer per Hardware-Programmiergerät |
| MemDBG · bfpilot · Kura · thinkii/PS5-Overlay | Kein Bezug zu Sensorik oder Lüftern; Kura nutzt dieselbe ICC-Schwelle, misst die Drehzahl aber nicht zurück |
| **Messwerte im 28-Byte-Block der Lüftersteuerung** | **Gemessen am 24.09.2026:** Über 881 Punkte bewegte sich nur die Schwelle (Byte 5). Alles andere sind feste Einstellungen. Nicht nochmal danach suchen, außer unter Volllast |
| **Vollbild im PS5-Browser** | Der Browser der Konsole bietet die Vollbild-Schnittstelle nicht an (24.09.2026). Der Knopf in der Oberfläche bleibt dort unsichtbar und hilft nur am PC |

---

## Eingebaut (Kurzfassung)

**Regelung:** Proportionalband mit Ratenbegrenzung, Drehzahl-Rückmeldung,
Notfallpfad · Betriebsart setzt seit 1.12.0 alle fünf Regelparameter ·
**an der Hardware belegt**: 69–70 °C bei 37–39 % Lüfter, während die Last
zwischen 13 % und 87 % schwankte.

**Betrieb:** Übernahme der eigenen Altinstanz statt Konsolenneustart (1.10.2) ·
Protokoll zusätzlich ins Kernel-Log, lesbar per klogsrv auf Port 3232 ·
Payload-Verwaltung mit Sicherheitsfilter · Startmenü-Kachel · 24-h-Verlauf ·
Energiebefehle · Warndialog oberhalb der Notfallgrenze.

**Anzeige:** Last je Kern · Netzwerk und Bildschirm (abfragegesteuert) ·
Speicherplatz · Firmware laut System · Benutzername ändern · Profilbild mit
DXT5-Umrechnung im Browser und Sicherung des alten Bildes.

**Sprache:** Seit 1.11.0 durchgehend laientauglich — kein SoC, Payload, ICC,
Totzone; jede Einstellung mit Beispiel und Wirkungsrichtung. Gilt auch für die
Protokolltexte im C-Code (1.11.2), die zuerst übersehen worden waren.

---

## Durchsuchte Quellen

**Lokal:** elf-arsenal · etaHEN · ps5debug-NG · Itemzflow · VoidShell · bfpilot ·
kura · ps5upload · ps5-payload-manager · offact · **klogsrv** · **regdump** ·
PS5-Payload-SDK samt aller 32 Zielbibliotheken · Kernel-Offset-Liste 3.00–12.70.

**Online:** GitHub-Organisation `ps5-payload-dev` (24 Projekte, vollständig
gesichtet) · `itsPLK` · `StonedModder` · `phantomptr` · `seregonwar` ·
deepwiki.com.

**Nicht abrufbar:** psdevwiki.com und playstationdev.wiki liefern 403
(Cloudflare, braucht einen echten Browser). Einzelne Seiten hat der User
gespeichert — daraus stammen die Hardware-Befunde zu APU und Spannungswandler.

**Rundgang 07.09.2026, achtzehn Cluster:** ps5-agc-gears · fan_target 0.1 +
ps5-fan-control 0.3 + kura · MM-PS5-API-MAPPER 0.8 · ps5debug-NG 1.3.0 ·
pacbrew-Quellen (klogsrv, websrv, shsrv, gdbsrv, elfldr, ftpsrv, offact) ·
fünf SDK-Fassungen im Vergleich · ShadowMountPlus 1.7alpha13 samt dessen
HTTP-API · onionHEN 0.0.12 und Autoloader · BFpilot 0.4.4 · evoX-CoreOS-WebUI ·
ps5-web-file-manager 1.7 · Native-App-Gerüst und LinkDev · ps5rs, SharpProspero
und ProsperoMgr · garlic-savemgr, ps5-app-dumper, CheatRunner, Trophäen ·
MemDBG, ampr_emu, opengnm · eigene Notizen und gespeicherte Seiten ·
Paketbau.

**Ausgereizt, nicht erneut durchsuchen:** Trophäen-Werkzeuge, Backport-Küche,
DLSS5oneclick, DualSense-Reparatur, RetroPapa, Spectrum Library,
exFAT-Bauwerkzeuge — kein Bezug zu Sensorik, Lüfter oder Oberfläche.

**Erledigt:** `gdbsrv` liegt jetzt vor (Wurzel des Materialordners und in
`ps5-payload-daemons/`) — er war der letzte Punkt unter „noch nicht geholt".

---

## Recherche 29./30.09.2026 — Backport, AMPR EMU, PlayGo (für 1.47.0)

Wunsch des Users: „Es soll bitte auch ein AMPR EMU (aktuellste), PlayGo, AMPR
EMU Asset Pack (Debug und Normal, aktuellste) und Backport eingebunden bzw.
integriert werden können." Dieselbe Lizenzsorgfalt wie beim Wechsel auf
GPL-3.0 (oben, MkPFS-Portierung) gilt auch hier.

| | Was | Stand |
|---|---|---|
| `[>]` | **AMPR EMU** — drakmor/ampr_emu, GPL-3.0-or-later, aktuell 0.4.2.1 (06.09.2026, vom Autor selbst noch als Testversion bezeichnet). Drei Baustufen in einer Version: ohne Paket, mit Paket, mit Paket und Debug — das deckt zugleich den Wunsch nach „Asset Pack Normal und Debug" ab. Datei liegt in `fakelib/libSceAmpr.sprx`; daneben entsteht `ampr_emu.index` (Kennung „AMPRIDX3") bei beschreibbaren Spielordnern von selbst, bei schreibgeschützten Abbildern muss sie vorher gebaut werden — Bauregeln aus `build_ampr_index.py` (GPL) | Recherche abgeschlossen |
| `[>]` | **PlayGo** — drakmor/pgo_stub, GPL-3.0. 0.5 ist eine Vorabversion (02.07.2026), 0.4 die letzte stabile. Datei liegt in `fakelib/libScePlayGo.sprx`, optional `playgo_stub.dat` im Spielordner | Recherche abgeschlossen |
| `[>]` | **Beide werden im Release mitgeliefert**, nicht zur Laufzeit von GitHub geladen (Entscheidung des Users, 30.09.2026) — GitHub-Release-Dateien lassen sich ohnehin nicht direkt aus dem Browser laden (keine CORS-Freigabe), nur die Konsole selbst könnte sie per HTTPS holen | Entscheidung getroffen |
| `[ ]` | **Backport braucht zwei Techniken zusammen, nicht nur eine.** (a) Die SDK-Version in `eboot.bin` *und* jeder `prx`/`sprx` des Spiels wird gesenkt und neu fake-signiert (`make_fself.py` aus dem SDK, GPL; portable Vorlage freefrank/ps5-image-forge, `backport.py`). (b) Ersatzbibliotheken werden **zur Laufzeit** eingeblendet, nicht ins Spiel geschrieben: ein dauerhaft laufendes Payload beobachtet `SceSysCore.elf` auf neue Spielstarts, findet die Sandbox des Titels (`/mnt/sandbox/<Titel>_NNN/`) und hängt per `mount_unionfs` den `fakelib`-Ordner *aus dem Spielordner selbst* über `common/lib` der laufenden Sandbox — beim Beenden wieder aus. Das zeigt BestPig/BackPork (github.com/BestPig/BackPork), am 30.09.2026 anhand vom User bereitgestellter Fakelib-Sätze und der eigenen Zeichenketten der ELF-Datei nachvollzogen, per README bestätigt | Methode verstanden |
| `[>]` | **Kein BackPork-Code oder -ELF im Projekt.** BackPorks Lizenztext ist wörtlich „provided for educational and research purposes" — keine Erlaubnis zur Weitergabe. Schritt (b) schreiben wir als **eigene, saubere GPL-3.0-Umsetzung** aus den oben beschriebenen Schritten neu — Sandbox finden, `fakelib` über `common/lib` unionfs-mounten, beim Beenden aushängen —, ohne eine Zeile von BackPork zu übernehmen, nur die Methode (Entscheidung des Users, 30.09.2026, nach Rückfrage ausdrücklich bestätigt) | **Entscheidung: wird gebaut** |
| `[-]` | **Eigene Live-Einbindung verworfen** (geschrieben und wieder entfernt, 30.09.2026): ein eigener Thread sollte alle 250 ms `/mnt/sandbox/` lesen und per `nmount()`/`mount_unionfs` selbst einhängen. Der User lieferte danach ShadowMountPlus 1.7 als Quelltext (`ShadowMountPlus-1.7.zip`), und der zeigt: **SMP kann das schon selbst** — `backport_fakelib=1` (Standard an) hängt `<scanpath>/backports/<TITEL-ID>/fakelib` schon vor dem Start des Prozesses in `common/lib` der Sandbox ein (`sm_fakelib_game_on_sandbox_ready`, `resolve_backport_path_for_title`, `mount_backport_overlay` in `src/sm_scan.c`/`sm_filesystem.c`). SMPs eigenes README warnt sogar ausdrücklich, dass ein zweiter solcher Beobachter (dort BackPork) damit kollidiert. Nebenbei bestätigt: SMPs echter `nmount`-Aufruf nutzt den Schlüssel `from` (nicht `target`, wie hier zunächst angenommen) plus `copymode=transparent`, `noatime`, `fnodup` — der eigene Aufruf wäre also ohnehin fehlgeschlagen | **verworfen zugunsten von SMPs eigener Funktion** |
| `[x]` | **Speicherort korrigiert** auf SMPs eigene Konvention: `/data/homebrew/backports/<TITEL-ID>/fakelib/` statt eines eigenen, von SMP nie gelesenen Ordners unter dem Datenverzeichnis dieser App. `/data/homebrew` ist SMPs erster Standard-Scanpfad (`SM_DEFAULT_SCAN_PATHS_INITIALIZER`, `include/sm_paths.h`); `resolve_backport_path_for_title` sucht zuerst am eigenen Scanpfad des Spiels, sonst der Reihe nach jeden konfigurierten Scanpfad durch — ein fester Ort reicht damit für jeden Titel, gleich wo dessen eigene Dateien liegen (Ordner, SMP-Abbild, installiertes Paket) | umgesetzt, `-Werror` fehlerfrei |
| `[>]` | **Einbindung über `backports/<TITLE_ID>`-Overlay**, wie ShadowMountPlus es schon kennt (`fakelib`, `fakelib2`, `backports/<TID>`) — funktioniert auch bei Abbildern, das Spiel selbst bleibt unverändert (Entscheidung des Users, 30.09.2026) | Entscheidung getroffen |
| `[ ]` | **Die Sony-Fakelibs selbst bleiben strikt Sache des Users** — Systembibliotheken wie `libSceAgc.sprx`, `libSceNpAuth.sprx` usw. dürfen nie ins Projekt oder nach GitHub. Vier vom User bereitgestellte Sätze (SDK/Firmware 4.xx–7.xx, je 3–7 Bibliotheken) liegen lokal bei ihm bereit. BackPorks eigene Empfehlung (`<TID>/fakelib/` im Spielordner selbst) gilt nur für BackPork — diese Konsole läuft mit ShadowMountPlus statt BackPork (die beiden dürfen laut SMPs README ohnehin nicht gleichzeitig laufen), darum gehören sie stattdessen in SMPs externen Ordner, siehe unten | liegt beim User bereit |
| `[x]` | **AMPR EMU und PlayGo, global — schon in 1.46.0 umgesetzt**, nicht erst 1.47.0: Ein Bereich oberhalb der Spieleliste legt eine Datei unter `/data/shadowmount/fakelib/` ab (SMPs `global_fakelib_path`, Standard an) — `libSceAmpr.sprx` bzw. `libScePlayGo.sprx`, unabhängig vom tatsächlichen Dateinamen (`?slot=ampr`/`playgo` legt den Zielnamen fest). SMP wendet die globale Datei automatisch auf jedes Spiel ohne eigenes Backport an (`global_fakelib_priority=game` lässt ein eigenes weiter gewinnen) | umgesetzt, `-Werror` fehlerfrei |
| `[x]` | **41 AMPR-EMU- und 2 PlayGo-Versionen fest eingebaut, direkt auswählbar** (Wunsch des Users, 30.09.2026: „diese möchte ich bitte im Tool dabei haben, damit diese AMPR EMU auswählbar sind"). Erst wurden alle 46 vom User gelieferten AMPR-Stände angeboten, auf Wunsch dann auf **0.2.6 bis 0.4.2.1** eingegrenzt (die älteren 0.2.0b–0.2.5.3 blieben draußen). Quelle: `tsuramatsu1/apr-emu-updater` (Sammlung 0.2.6–0.3.6.6, mit dessen eigenem `builds.json` für Bezeichnungen) plus die separat gelieferten 0.4.2.1-Baustufen; PlayGo 0.5 log/nolog aus `pgo_stub_0.5.zip`. Neues `tools/gen_fakelib_catalog.py` bettet `fakelib-assets/{ampr,playgo}/*` genau wie `gen_assets.py` die Web-Dateien als reine C-Byte-Arrays ein (`gen/fakelib_catalog.c`, rund 12 MB) — dieselbe, schon bei `gen_pkg_blob.py` (10-MB-Kachelpaket) bewährte Technik, keine neue. Ein Klick auf „Übernehmen" schreibt die eingebetteten Bytes direkt an die globale Stelle, ganz ohne Hochladen. Eigene Datei hochladen bleibt daneben möglich. ELF wächst dadurch von 2,9 auf rund 14,9 MB — `-Werror` fehlerfrei, 42 s Baudauer | umgesetzt |
| `[x]` | **AMPR-EMU-Asset-Pack — Hochladen umgesetzt, Bauen bewusst nicht.** Der User bat auch darum, Asset Packs „zu erstellen bzw. zu integrieren". Ein Asset Pack ersetzt die eigenen Spieldateien und wird deshalb aus dessen eigenem Datenbestand gebaut — `ampr_emu`s `docs/ASSET_PACKS.md` beschreibt das als Mitschnitt tatsächlicher Lesezugriffe eines Spieldurchlaufs, gepackt mit dem vom User gelieferten `ampr-pack-tools` (GUI + Python, gleicher Autor). Das kann und soll diese App nicht nachbauen — es braucht das laufende Spiel und Fachurteile, was ein Mitschnitt übersehen hat (andere Level, Sprachen, DLC). Umgesetzt ist die Zustellung: bei einem Spiel aus einem Ordner (`ps5tm_library_real_path`, neu, ohne Formatausschluss) landen fertig gebaute Dateien direkt im Spielordner selbst — `ampr_emu.index`, `ampr_assets.index[.crc\|.runtime]`, `ampr_assets-NNN.pak` — ohne die sonst geltende 4-MB-Grenze (8 GB, Pakete können groß werden). Bei Abbild oder installiertem Paket fehlt die beschreibbare Entsprechung, also nicht angeboten | umgesetzt (Zustellung), `-Werror` fehlerfrei |
| `[>]` | **Ziel 1.47.0**, nach 1.46.0 (Entscheidung des Users, 30.09.2026) | Termin gesetzt |
| `[-]` | **AMPR EMU/PlayGo „global" (Zeile 647/648) zurückgenommen** (01.10.2026): User meldete, dass die Funktion jedes Spiel am Starten hindert (CE-105773-3), während dieselbe Bibliothek direkt im Spielordner weiterhin funktioniert. Komplettes Nachlesen von SMP 1.7beta2 (`sm_fakelib.c`, `sm_scan.c`, `sm_config_mount.c`, `config.ini.example`, README) zeigt: `global_fakelib_path`/`global_fakelib_priority` existieren in SMP **nicht** — SMP kennt ausschließlich die pro-Titel-Overlay aus Zeile 644/645, nie eine "gilt für jedes Spiel ohne eigenes"-Regel. `/data/shadowmount/fakelib/` wird von SMP an keiner Stelle gelesen; live auf der Konsole bestätigt (Datei lag dort folgenlos, inkl. User-eigenem Build mit anderer Größe als die Katalog-Version). Das "global"-Konzept gehört laut diesem Quelltext zu BackPork (Zeile 641), nicht zu SMP — SMP tötet BackPork sogar aktiv beim eigenen Start (`stop_conflicting_backpork()`, `main.c`). **Nachtrag, selber Tag, nach Auswertung von SMPs `debug.log` (per `/api/v1/debug-log`, Port 10101, `POST {max_bytes}`, 4096..262144 erlaubt):** Die tatsächlich auf der Konsole laufende SMP-Fassung (Build 2026-09-21T21:19:45Z) hat ein globales Fakelib-Merkmal, das im hier vorliegenden Quelltext fehlt — Beweis im Log: `[FAKELIB] global libraries mounted for CUSA00775: /data/shadowmount/fakelib -> .../common/lib` gefolgt von `[MDBG] ... crashed before kstuff auto-pause` für jedes Spiel ohne eigenes Backport (Crash Bandicoot 4, Fishing North Atlantic, Tetris — alle betroffen). Ursache damit doch geklärt: der PS5-eigene Loader (`rtld`) lehnt die fremde `libSceAmpr.sprx` beim Laden ab ("mount flag / attribute error"), das Spiel stürzt sofort ab — exakt CE-105773-3. Frühere Zeile hier war also im Kern richtig (SMP wendet die globale Datei automatisch an), nur die Quelle (BackPork vs. SMP) war falsch zugeordnet. Entfernung war darum doppelt richtig: wirkungslos wäre falsch gewesen — sie war aktiv gefährlich. Komplett entfernt: `ps5tm_backport_global_*`, `ps5tm_backport_catalog()`/`_activate()` (backport.c), die vier zugehörigen Routen (api.c/http.c) und der komplette UI-Block (app.js, index.html, style.css). Die 41+2 eingebetteten Bibliotheken (`fakelib-assets/`, `tools/gen_fakelib_catalog.py`, `ps5tm_fakelib_catalog[]`) bleiben unverändert im Programm, falls ein künftiger **pro-Titel**-Katalog-Picker (statt global, z. B. direkt bei "Eigene Backport-Dateien" je Spiel) daraus bauen will | **zurückgenommen, Code entfernt** |

| `[x]` | **Backport-Upload machte Arkanoid komplett unstartbar — Rechte-Bug in `mkdir_p2()` behoben und live verifiziert** (01.10.2026). SMPs `debug.log` zeigte den eigentlichen, zweiten Fehler: `[IMG] backport overlay failed: /data/homebrew/backports/PPSA06328 -> /system_ex/app/PPSA06328 (Operation not permitted)`, danach dauerhaft `[SHELLCORE] launch mount unavailable: PPSA06328 status=5` — das Spiel startete gar nicht mehr, nicht nur "Datei wirkt nicht". Log-Detail nannte die Ursache direkt: unser Ordner `mode=040755 uid=0 gid=0`, der von SMP selbst erwartete/erzeugte Ordner `mode=040777 uid=4294967295 gid=4294967295`. `mkdir_p2()` (backport.c) rief `mkdir(path, 0755)` nur beim *Anlegen* auf — half nichts bei einem schon bestehenden Ordner aus einem früheren Upload, und SMPs eigener Reparaturversuch (`[BKP] permissions fixed`) behob es ebenfalls nicht dauerhaft. Fix: `mkdir(path, 0777)` **plus** `chmod(path, 0777)` auf allen drei Ebenen (Root, Titel-Ordner, fakelib), damit auch ein schon vorhandener Ordner aus einer älteren Sitzung korrigiert wird. Verifiziert per Testlauf: Build deployt, testweise `libSceAmpr.sprx-0.4.2.1-test-nopack` aus `fakelib-assets/` hochgeladen (nur um den Mount zu prüfen, nicht die richtige Version für Arkanoid), per `/api/v1/library/launch` gestartet — Log zeigt jetzt `[IMG] backport overlay mounted (ro): ... -> /system_ex/app/PPSA06328` statt des EPERM-Fehlers, Spiel startet (`[GAME] started`). Das Testspiel stürzte danach zwar noch ab (`crashed before kstuff auto-pause`), aber das ist die erwartbare Folge einer falschen/nicht passenden AMPR-EMU-Version für dieses eine Spiel, nicht der behobene Mount-Fehler — mit der richtigen, selbst gewählten Datei sollte es laufen wie beim manuellen Platzieren im Spielordner. Testdatei danach wieder über "Leeren" entfernt | **behoben, live verifiziert** |
| `[ ]` | **Nebenfund, nicht behoben:** Titel PPSA06328 taucht in SMPs `debug.log` jetzt als zwei Quellen für dieselbe Titel-ID auf — `/data/homebrew/Arkanoid Eternal Battle` (Ordner) und `/mnt/usb0/homebrew/Arkanoid Eternal Battle.ffpkg` (Abbild, "Duplikat ... ignoriert") — nicht mehr unter dem ursprünglichen `/mnt/usb0/homebrew/Arkanoid`. Stammt vermutlich aus den früheren ffpkg/UFS2-Konvertierungstests mit genau diesem Spiel, siehe [[konvertierung-ffpkg-ufs2]]. Deckt sich mit dem bereits bekannten, noch offenen Aufräum-Posten "Testartefakte auf der Konsole" | **offen, nicht angefasst** |
| `[-]` | **Manuelles "Ordner hinzufügen" für AMPR EMU/PlayGo (Zeile 644/645) komplett entfernt** (02.10.2026, Entscheidung des Users nach einem zweiten, unabhängigen Vorfall): Derselbe Rechte-Fix aus der Zeile direkt darüber reichte nicht — der EPERM-Mount-Fehler trat erneut auf, diesmal mit korrekten `0777`-Rechten auf beiden Seiten. Entscheidender Unterschied zum einen erfolgreichen Testlauf: Dort lag die Quelle zufällig auf einem **UFS**-Abbild (`/mnt/shadowmnt/...`, über ffpkg gemountet), beim erneuten Fehlschlag auf einem **exFAT**-Ordner direkt (`/mnt/usb0/homebrew/...`, der übliche Fall bei einem USB-Stick). SMPs unionfs-Overlay auf `/system_ex/app/<TID>` scheitert demnach grundsätzlich bei exFAT-Ordner-Quellen, unabhängig von Rechten oder Dateiinhalt — keine Sache, die sich in diesem Projekt beheben lässt. Der Fehler blockierte den Spielstart zweimal vollständig (`status=5`, nur durch manuelles Löschen der Dateien im Backport-Ordner behoben) und kostete den User zwei komplette Neuinstallationen von Arkanoid, während Debug-Build und config.ini fälschlich in Verdacht standen. Komplett entfernt: `ps5tm_backport_list/_prepare/_clear`, `lib_name_ok()`, `title_dir()`, `mkdir_p2()`, `BACKPORT_ROOT` (backport.c), die zwei Routen plus Upload-Pfad (api.c/http.c), der ganze UI-Block "Eigene Backport-/AMPR-/PlayGo-Dateien" (app.js/index.html). `title_id_ok()` bleibt, wird vom AMPR-EMU-Asset-Pack-Code weiter gebraucht. **Ausdrücklich bestehen bleibt** die reine Erkennung/Anzeige (`scan_fakelib()`, library.c; `gmTags()`, app.js) — die liest nur den Spielordner für die Kachel-Badges, schreibt nichts, ist von diesem Fehler nie betroffen. Einzig verbliebener, zuverlässiger Weg, eine Bibliothek zu ersetzen: direkt in den `fakelib`-Ordner des Spiels legen (FTP o. ä.) | **entfernt, `-Werror` fehlerfrei** |
| `[-]` | **AMPR-EMU-Asset-Pack-Hochladen (Zeile 649) ebenfalls entfernt** (02.10.2026, Entscheidung des Users, nicht wegen eines Fehlers — diese Funktion hatte funktioniert, per Live-Check bestätigt). Nach der Zeile direkt darüber war das die einzige verbliebene Funktion in `backport.c`; ohne sie wurde auch `title_id_ok()` ungenutzt, also wurde **die ganze Datei gelöscht** statt nur die Funktion. Komplett entfernt: `backport.c` (ganz, inkl. `title_id_ok()`, `assetpack_name_ok()`, `assetpack_dir()`, `ps5tm_backport_assetpack_list/_prepare/_clear`), `src/backport.c` aus dem Makefile (`SRCS +=`), die zwei Routen plus Upload-Pfad (api.c/http.c), `ps5tm_library_real_path()` (library.c — war nur für `assetpack_dir()` da, sonst nirgends aufgerufen), der ganze UI-Block "AMPR-EMU-Asset-Pack" (app.js/index.html/style.css: `.gm-backport`, `.gm-bp-*`, `.gm-ap-*`). **Übrig bleibt nur noch** die reine Erkennung/Anzeige (`scan_fakelib()`, library.c) — Backport-Küche damit auf ihren Kern zurückgefahren: anzeigen, was ein Spiel selbst mitbringt, nichts mehr einbauen | **entfernt, `-Werror` fehlerfrei** |
| `[-]` | **Eingebettete AMPR-EMU-/PlayGo-Bibliotheken (Zeile 640/648) samt Generator entfernt** (02.10.2026, Entscheidung des Users vor dem Öffentlichmachen des Repos): Seit die Auswahl, das globale Ablegen und die Uploads weg sind, las niemand mehr `ps5tm_fakelib_catalog[]` — der Linker (`--gc-sections`) warf die Daten schon beim Bauen weg. Nachweis: ELF vorher und nachher byte-genau gleich groß (2.901.896 Bytes), kein neues Deploy nötig. Entfernt: `fakelib-assets/` (41 AMPR-EMU- und 2 PlayGo-Versionen, 13 MB GPL-Binärdateien), `tools/gen_fakelib_catalog.py`, im Makefile `gen/fakelib_catalog.c` samt Regel und `FAKELIB_FILES`, in `ps5tm.h` `ps5tm_fakelib_entry_t` und die zwei `extern`-Deklarationen, in `THIRD_PARTY_NOTICES.md` die beiden Zeilen. Der Quellbaum enthält damit keine fremden Binärdateien mehr. Wiederherstellbar aus Commit `319b608` (`git show 319b608:fakelib-assets/...`) | **entfernt, `-Werror` fehlerfrei** |

Frühere Einschätzung überholt: Der Rundgang vom 07.09.2026 (oben) hatte die
„Backport-Küche" noch als „kein Bezug zu Sensorik, Lüfter oder Oberfläche"
eingestuft und nicht weiter verfolgt. Der ausdrückliche Wunsch des Users vom
29.09.2026 hebt das auf.

## ffpkg (UFS2) als drittes Konvertierungsziel, 30.09.2026

Frage des Users: „was ist mit weiteren Formaten bei der Konvertierung?" — auf
Rückfrage bestätigt: ffpkg (UFS2) neu bauen, und zwar zuerst nur für
kleine/mittlere Spiele, große folgen später (ausdrücklich gewählte Stufe,
30.09.2026).

| | Was | Stand |
|---|---|---|
| `[x]` | **UFS2Tool** (SvenGDK/UFS2Tool, BSD-2-Clause) als Referenz gewählt — reif, testbar (.NET 8, `dotnet build`), mit eigenem „PS5 Quick Create"-Modus (`PS5QuickCreateViewModel.cs`), der genau `makefs -S 4096 -t ffs -o version=2,minfree=0,softupdates=0,optimization=space` nutzt. Nur als Portiervorlage gelesen, nichts davon wird mitgeliefert (siehe THIRD_PARTY_NOTICES.md) | Referenz beschafft |
| `[x]` | **Python-Prototyp statt direkt in C**: `prospero-clang` kompiliert nur für die PS5 quer, erzeugt kein hier lokal lauffähiges Programm; ein lokaler LLVM-Build scheitert an fehlendem Linker (kein MSVC/MinGW). Python ist die einzige Sprache, die hier tatsächlich ausführbar ist — die Bitfeld-Logik wurde dort zuerst gebaut und geprüft, bevor eine Zeile C entstand | Prototyp validiert |
| `[x]` | **Byte-für-Byte gegen UFS2Tool geprüft**: Superblock-Felder (alle 19 verglichenen, inklusive der vorher nie verglichenen `qbmask`/`qfmask`) stimmen exakt überein, ebenso Root-Inode und Verzeichnisblock. UFS2Tools eigene Werkzeuge (`fsck_ufs`, `ls`, `extract`) lesen den Prototyp-Output fehlerfrei; extrahierte Dateien sind bytegleich mit der Quelle — geprüft an einer kleinen Testmenge und an einem ~9-MB-Baum mit mehreren Zylindergruppen und einer Datei tief im Einzel-Indirektionsbereich (6 MB) | validiert |
| `[x]` | **Zwei echte Fehler dabei gefunden und behoben**: (a) `di_blocks` muss immer in festen 512-Byte-Einheiten gezählt werden, unabhängig von der Sektorgröße des Dateisystems (hier 4096) — beide wurden anfangs verwechselt. (b) Die direkten/indirekten Blockzeiger im Inode beginnen bei Offset 0x70, nicht 0x68 — die beiden reservierten „Extended Attribute Block"-Felder davor sind 2×8, nicht 1×8 Byte. Ohne (b) galt das Wurzelverzeichnis als leer (0 Einträge), obwohl die Bytes selbst korrekt dastanden | beide behoben, erneut validiert |
| `[x]` | **Umfang dieser ersten Version**: direkte Blöcke (12) plus ein einfacher Indirektionsblock je Datei, macht rund 128 MiB je Datei bei 32-KiB-Blöcken; größere Dateien scheitern sauber mit `Ufs2TooLargeError`/einer klaren deutschen Fehlermeldung, nichts anderes wird angefasst. Doppelte/dreifache Indirektion bleibt für später (Entscheidung des Users, 30.09.2026: „Erst kleine/mittlere Spiele, große folgen später") | Grenze getestet (135-MB-Datei löst sauber aus) |
| `[x]` | **Nach C übertragen** (`src/conv_ufs2.c`/`.h`), am Stil von `conv_pfs.c` orientiert (`pwrite_all`/`pread_all`, deutsche Fehlermeldungen, Goto-Aufräumpfade), Verzeichnis-Scan wie `conv_exfat.c` (`readdir`+`qsort`, `strcasecmp`, dieselbe Ignorierliste für OS-Datenmüll). Verzeichnisreihenfolge ist dem Format egal — UFS2Tools eigener Schreiber sortiert selbst nicht, bestätigt am eigenen `ProcessDirectoryContents` (`EnumerateFileSystemInfos` ohne Sortierung) | umgesetzt, `-Werror` fehlerfrei |
| `[x]` | **Eingebunden als dritte Option** neben exFAT und ffpfsc: `gameconvert.c` (`ops_for`, `cv_thread`, Plan-/Start-Endpunkte), Web-UI (`CV_OPS`/`CV_EXT` in `app.js`, Erklärtext in `index.html`). `library.c`s `can_convert`-Freigabe brauchte keine Änderung — sie ist formatbasiert, „folder" war schon enthalten. Ergebnis wird vor dem Umbenennen immer gelesen und geprüft (`ufs2_verify_tree`: Superblock-Kennung, dann das ganze Verzeichnisbaum ab Wurzel) | umgesetzt, `-Werror` fehlerfrei |
| `[ ]` | **Einhängen an der Konsole nie bestätigt.** Am 30.09.2026 liefen zwei echte Konvertierungen auf der Konsole bis zur Größengrenze (Schreibpfad und saubere Fehlermeldung bestätigt); dass ShadowMountPlus ein fertiges `.ffpkg` dieser App einhängt, ist bisher nicht bestätigt, weder beim alten 32-KiB-Abbild noch beim jetzigen 64-KiB-Abbild. Mit dem Aufbau vom 05.10.2026 ist der Test fällig: ein kleines Spiel in ffpkg konvertieren und von ShadowMountPlus einhängen lassen | offen |
| `[x]` | **04.10.2026 — Dateien über 128 MiB (Doppel-Indirektion).** Anlass: „Asterix & Obelix: Heroes" scheiterte mit „Eine Datei ist zu groß für ein ffpkg-Abbild (210673664 Bytes, höchstens 134610944)". `conv_ufs2.c` legt jetzt zusätzlich den doppelten Indirektionsblock an (`write_indirect_block`, `write_double_indirect_block`, Vergabereihenfolge wie UFS2Tool: Block, seine Daten, dann der doppelte und je Eintrag ein einfacher Block mit Daten); dreifache Indirektion bleibt ungeschrieben. Die Größenplanung zählt die Indirektionsblöcke über `indirect_blocks_for()` (Formel gegen eine Nachbildung des Schreibers an Zehntausenden Größen geprüft), die Prüfung läuft in einem Durchgang über Direkt-, Einfach- und Doppelbereich (`verify_file`, `verify_pointer_block`), und die Anzeige meldet sich alle 32 Blöcke statt erst am Dateiende. Seit dem 64-KiB-Aufbau vom 05.10.2026: `UFS2_MAX_FILE_BYTES` = (12 + 8192 + 8192²) × 65536 = 4 398 584 168 448 Byte (bei den damaligen 32-KiB-Blöcken 549 890 424 832) | umgesetzt |
| `[x]` | **04.10.2026 — Vergleich mit dem aktuellen UFS2Tool (neu gebaut, Version 4.1.0) deckte vier Abweichungen im Abbild auf**, die bei kleinen Bäumen nie aufgefallen waren, weil dort nur Superblock, Wurzel und Verzeichnis verglichen wurden: (a) `fs_fsbtodb` und `fs_old_nspf` müssen in 512-Byte-Einheiten rechnen (heute 7 und 128 bei 64-KiB-Fragmenten, damals 3 und 8 bei 4 KiB), weil FreeBSDs `newfs` die Sektorgröße vor dem Layout auf DEV_BSIZE zurücksetzt (das offizielle `mkufs2.sh` von ShadowMountPlus ruft `newfs -S 4096` auf, exFAT Image Builder `-S 512`; beides ergibt dasselbe Abbild); wir schrieben 0 und 1. Die Konsole rechnet mit `fs_fsbtodb` Fragmentnummern in Plattenadressen um — mit 0 läge jeder Zugriff an der falschen Stelle. (b) der Wiederherstellungsblock (`struct fsrecovery`) in den letzten 20 Byte vor dem Superblock fehlte. (c) Cluster-Karte, Cluster-Summe und `cg_frsum` der Gruppenköpfe standen auf dem Stand eines leeren Abbilds (`compute_cg_maps`, `write_cg_maps` rechnen sie nach dem Befüllen aus der echten Fragment-Bitmap). (d) `cg_initediblk` deckte die benutzten Inodes nicht ab. **Abbilder aus Fassungen vor diesem Stand haben diese Fehler und sollten neu erzeugt werden** | behoben, validiert |
| `[x]` | **05.10.2026 — Umstellung auf den Aufbau von ShadowMountPlus und exFAT Image Builder.** Auftrag des Users: „schau dir an, wie dieses Programm ffpkg erstellt, und nutze die Infos" (exFAT Image Builder 4.0.2; gelesen wurde nur der mitgelieferte Quelltext, das Programm selbst wurde nicht gestartet, ebenso nicht die darin liegende UFS2Tool-Bibliothek, die laut Metadaten Version 4.0.0 ist). Befund: Es ruft UFS2Tool mit `newfs -O 2 -b 65536 -f 65536 -m 0 -S 512 -i 262144 -D <Ordner> <Abbild>` auf, und die README von ShadowMountPlus (1.7beta2) empfiehlt für ein `.ffpkg` dieselben 64-KiB-Blöcke und -Fragmente (`-i 262144` für normale Spiele, 131072 bei zehntausenden Dateien). Unser Schreiber nutzte bis dahin die 32-KiB-Blöcke und 4-KiB-Fragmente des „PS5 Quick Create"-Modus von UFS2Tool. Jetzt: `BLOCK_SIZE` = `FRAG_SIZE` = 65536, 8192 Zeiger je Indirektionsblock, Gruppen wie in UFS2Tools `newfs -D` (`geometry_for`: 256 oder 512 Inodes je Gruppe, ein Inode je Block), und die Abbildgröße plant der Schreiber selbst (`make_fs_size`: Blöcke des Baums, dazu freier Platz wie in `mkufs2.sh` — etwa 0,5 %, mindestens 64, höchstens 512 MiB — und die Verwaltung; die Größe wächst, bis `alloc_capacity` und die Inodes reichen, die letzte Gruppe hat immer mindestens einen Datenblock). Ergebnis: Abbild = Spieldaten (je Datei auf 64 KiB aufgerundet) + gut 1,2 % Verwaltung + freier Platz; der Gruppenkopf passt immer in seinen Block, die frühere Begrenzung der Gruppen entfällt. Ein Ordner aus fast nur leeren Dateien (mehr Inodes als Blöcke) wird mit klarer Meldung abgelehnt. Übernommen ist nur Wissen, kein Code von exFAT Image Builder (README und THIRD_PARTY_NOTICES nennen es) | umgesetzt |
| `[x]` | **05.10.2026 — Prüfungen am 64-KiB-Aufbau** (Werkzeuge im Scratchpad, nicht im Repo): (1) 72 Zufallsbäume (1 Byte bis 620 MB große Dateien, bis 151 Dateien in 12 Ordnern) gegen UFS2Tools `newfs -D` bei gleicher Größe: 71 byte-gleich bis auf Zeit, `fsid` und die Zufalls-Generationsnummern unbenutzter Inodes; bei Nr. 26 ist UFS2Tools eigenes Abbild defekt (`fs_size` kürzer als die Datei), dort gelten `fsck_ufs` und der eigene Prüfer. (2) Je ±1 Byte an den Grenzen 12, 12 + 8192 und 12 + 8192 + 8192 Blöcke, drei einfache Blöcke unter dem doppelten, eine 1,6-GB-Datei, 4000 Dateien in einem Ordner, 2000 / 11 000 / 20 000 winzige Dateien in einem Ordner: alle byte-gleich. (3) 24 Bäume mit der eigenen Größenplanung unter ASan/UBSan: `fsck_ufs` sauber, unabhängiger Prüfer (`ufs2_check2.py`: Zeigerbäume, Bitmaps, Cluster-Karte, `cg_frsum`, Wiederherstellungsblock) ohne Befund. (4) Verfälschungstest: 51 gezielt beschädigte Abbilder, alle so abgewiesen wie erwartet; Fuzz: 2200 zufällig beschädigte Abbilder ohne Absturz oder Hänger; Abbruch an elf Stellen (errno `ECANCELED`, kein Speicherleck); MemorySanitizer über vier Bäume (nie beschriebene Bytes gelangen nicht ins Abbild). (5) Größenplaner an 78 000 Größen von einem Block bis 16 TiB mit bis zu 33 Mio. Einträgen: Kopf passt, Platz und Inodes reichen, letzte Gruppe gültig, Abbild nie mehr als knapp 1,3 % über Daten und freiem Platz; abgelehnt wird nur, wo Inodes die Größe vervielfachen würden. Dabei fiel ein Planerfehler auf (viele Einträge bei wenig Daten: das Wachstum rechnete ein Inode je Block, die Inodes kommen aber gruppenweise; 64 Runden ohne Ergebnis) und wurde behoben. (6) Ende-zu-Ende `jobtest` (Kopieren und alle drei Konvertierungen, 98 Prüfungen) und die Web-Szenarien CV2, CK und A | geprüft |
| `[x]` | **UFS2Tool legt bei manchen Größen die letzte Gruppe zu kurz an** (Kopf und Inode-Tabelle liegen dann außerhalb von `fs_size`, die Datei ist länger als das Dateisystem); dort ist dessen Abbild als Vergleich unbrauchbar, unser Planer vermeidet es. Ältere Notiz zum 32-KiB-Aufbau (UFS2Tool-Abbild ab etwa 1 GiB beschädigt, „ROOT INODE IS NOT A DIRECTORY", weil der Gruppenkopf den Inode-Bereich überschrieb) gilt im 64-KiB-Aufbau nicht mehr: Der Kopf einer Gruppe mit 512 Blöcken ist winzig | geprüft |
| `[x]` | **05.10.2026 — Platzprüfung für ffpkg**: `ufs2_plan_size()` liefert die Abbildgröße aus den Ordnern und Dateigrößen allein, ohne Dateien zu lesen und mit denselben Ablehnungen wie der Schreiber; `gameconvert.c` prüft damit Platz und FAT32-Grenze, der Plan meldet `ffpkg_bytes`, die Oberfläche nennt die Zahl im Dialog. Das Abbild ist gut 1,2 % + etwa 0,5 % größer als die auf 64 KiB aufgerundeten Spieldaten (bei einem Spiel von 50 GB rund 0,9 GB mehr; das frühere „bis zu 9 %" gehörte zum 32-KiB-Aufbau) | umgesetzt |
| `[x]` | **05.10.2026 — Durchsicht von außen (zwei Prüf-Agenten) und was daraus wurde**: (1) Ein `readdir`-Fehler mitten in einem Ordner galt als Ordnerende (`conv_ufs2.c`, `conv_exfat.c`, `gamecopy.c` zweimal, `savebackup.c`): das Ergebnis lag ohne den Rest vor und bestand jede Prüfung, weil die nur kennt, was gelesen wurde. Jetzt `errno = 0` vor jedem `readdir` und eine Prüfung danach, Meldung „Der Ordner … ließ sich nicht vollständig lesen“. (2) ffpkg: die geschriebenen Einträge werden gegen die geplanten gezählt (ein Ordner, der sich zwischen Planen und Schreiben ändert, gab eine Prüfsumme als Grund), mehr als 32 765 Unterordner in einem Ordner werden abgelehnt (der Link-Zähler hat 16 Bit; vorher lief er still über). (3) Der Prüfer des fertigen Abbilds (`ufs2_verify_tree`) war an vier Stellen zu nachsichtig: verlorene, vertauschte oder doppelte Einträge (jetzt eine Summe über einen Hash jedes Eintrags aus Ordner, Inode, Typ und Name, im Schreiber und im Prüfer gebildet, dazu die Zahl der gelesenen Dateibytes), die Regeln des Kernels für Verzeichniseinträge (`ufs_dirbadentry`: reclen durch 4 teilbar, Platz für den Namen mit NUL, innerhalb des 512-Byte-Abschnitts, kein `/` im Namen; `.` und `..` zeigen auf Ordner und Eltern), der Superblock (alle 8 KiB mit `build_superblock()` aus der gelesenen Geometrie verglichen, ohne Zeiten, ID und die Zähler; 36 von 55 einzeln verfälschten Feldern fielen vorher nicht auf, darunter `fs_fsbtodb`) und Ganzzahl-Überläufe bei beschädigten Größenangaben (UBSan). Die neuen Prüfungen lehnen 19 gültige UFS2Tool-Abbilder nicht ab. (4) Oberfläche: der Plan nennt den Grund, wenn sich ein Ordner nicht als ffpkg bauen lässt (`ffpkg_error`), und die FAT32-Grenze rechnet für ffpkg mit der Größe des ffpkg-Abbilds (`fat32_too_big_ffpkg`). Offen bleibt: ein Ordner über 12 Blöcke Verzeichnisdaten (etwa 24 000 Einträge) wird abgelehnt, Verzeichnisse mit Indirektionsblock schreibt der Schreiber nicht | umgesetzt |

## Pakete: finden, aufteilen, installieren (04./05.10.2026)

Auftrag des Users (04.10.2026): Die Funktionen des PS5 PKG Managers (itsPLK, GPL-3.0) sollen **nicht eingebettet**, sondern in dieser App nachgebaut werden. Gelesen wurde dessen Quelltext (Version 1.4.1), übernommen wurde kein Code; die Quelle steht in README und THIRD_PARTY_NOTICES.

| | Was | Stand |
|---|---|---|
| `[x]` | **P1 — Finden, Anzeigen, Aufteilen** (`pkgparse.c`, `pkgscan.c`, `pkgsplit.c`; Reiter „Pakete“). Pakete lesen (PS4 CNT, PS5 FIH, geteilte PS5MPKG1), im Hintergrund suchen, ein großes Paket in Teile für FAT32 oder Discs aufteilen (zurückgelesen, Namen erst am Ende) | committet und gesendet; die Suche an der Konsole bestätigt, das Aufteilen dort noch nicht ausprobiert |
| `[x]` | **P2 — Installieren** (`pkginstall.c`, `pkgstream.c`, `helper/pkginst_helper.c`, `pkginst_ipc.h`; Knopf „Installieren …“). **Weg vom User gewählt: ein eigener Hilfsprozess.** Die App liefert das Paket per HTTP/1.1 mit `Range` auf `127.0.0.1:18851` (nur der eine gepinnte Name, bis 16 Verbindungen, 10 s für einen Anfragekopf, jede Datei ohne Links und nur, wenn sie noch so groß und so alt ist wie bei der Suche; ein geteiltes Paket als ein Strom). Eine eingebettete Hilfs-ELF (rund 80 KB, Prozessname `ps5cc-inst.elf`) wird über den Lader (9021) gesendet, verbindet sich auf `127.0.0.1:18853` zurück (Kennwort, das die App je Start zufällig ins Abbild schreibt) und ruft nach `sceNetCtlInit`, `sceUserServiceInitialize` und `sceNetInit` (wie beim PKG Manager) `sceAppInstUtilInitialize`, `…InstallByPackage` und `…GetInstallStatus` auf. **Fertig** heißt: Die Konsole meldet „abgeschlossen“ und alle Bytes sind gesendet, oder sie meldet „spielbar“ und zählt selbst, dass sie alles geholt hat; danach sucht die App bis zu 90 s das Ergebnis in `app.db`. Das Ende hat vier Gesichter: installiert / „von der Konsole als fertig gemeldet“ (gelb, nicht bestätigt) / „Paket geliefert, Ergebnis nicht bestätigt“ (gelb, ohne Hilfsprogramm) / fehlgeschlagen. Kurzzeitige Fehler (`0x80B2116F`, `0x80B2100D`, `0x80B2100E`) bis zu dreimal mit neuem Hilfsprogramm und neuem Namen; Stillstand: Hinweis nach 3 min, Fehler nach 20 min ohne Fortschritt. Nie `AppUnInstall`, nie etwas löschen; ein Abbruch lässt liegen, was die Konsole angelegt hat (→ P5). Verdrahtet mit Kopieren, Konvertieren, Verschieben, Sichern und Teilen (keines läuft zugleich, auch nicht, wenn zwei im selben Augenblick starten). **Durchsicht 05.10.** durch vier unabhängige Prüfer (Sicherheit, Nebenläufigkeit, Fehlerfälle, stille Fehler); ihre Berichte sind Daten, jeder Fund wurde selbst nachgestellt, die echten sind behoben, je mit Test: Ein Paketname wie `SetControllerFocus(0x7)` konnte über das Protokoll (es spiegelt in den Kernel-Puffer, aus dem die App auch liest) das erkannte Spiel verfälschen → das Protokoll macht aus Steuerzeichen keine Zeilen mehr, und die Leser (Spiel, Takt, Controller, Mikrofon-Taste) überspringen Zeilen der App selbst. Außerdem: Teile werden an dem Paket darin geprüft, nicht an ihrem Kopf; eine geänderte oder durch einen Link ersetzte Datei wird nicht geliefert; ein Spiel wird bei unlesbarer Liste der Konsole nicht blind installiert; „fertig“ nie allein nach den Zahlen des Servers; Teilen und Sichern können nicht mehr zugleich mit einer Installation beginnen; ein Server, der sich nicht beenden ließ, blockiert die nächste Installation nicht (Hinweis im Protokoll); die Uhr der Dauer ist monoton. **Erster Konsolentest 05.10.2026 (zwei Versuche: Moorhuhn 212 MB, LEGO Batman 31,8 GB), beide gescheitert, und was daraus wurde:** Die Konsole las wirklich vom Server auf `127.0.0.1` (die größte Unbekannte), nahm den Aufruf an und begann die Installation (PlayGo, BGFT-Task). Das Hilfsprogramm aber stieg wenige Millisekunden nach dem Aufruf aus der Schleife aus, rief `sceAppInstUtilTerminate` auf, und ein Hilfsfaden der Bibliothek sprang dabei in die Adresse 0 (SIGSEGV, in beiden Versuchen mit denselben Registerwerten: `poll(fds, 1, 50)` zuletzt, `rip` = 0; die Konsole schrieb je einen kleinen Speicherauszug nach `/devlog/system/sce_coredumps.0`); die App baute daraufhin ihren Server ab, die Konsole scheiterte beim Lesen (`0x8041013d`), und der abgebrochene Versuch behielt den reservierten Platz (bei LEGO Batman rund 34 GB auf der internen SSD, bis der Eintrag an der Konsole gelöscht wird). Lehren: (1) Die Installation gehört der Konsole und lief ohne das Hilfsprogramm weiter. (2) Die Bibliothek wird nie beendet (`_exit`, kein `pthread_join`, kein Weg durch `main`); das Protokoll des Hilfsprogramms nennt jetzt, warum es aussteigt, und welche Dateien es offen hat. (3) Stirbt oder hängt das Hilfsprogramm nach dem Aufruf, bleibt der Server an, und ein neues fragt mit der Inhalts-ID nach dem Fortschritt (bis zu sechsmal); ohne Hilfsprogramm liefert die App das Paket trotzdem aus und lässt die Liste der Konsole entscheiden (nie als „installiert“ gemeldet). Warum das erste Hilfsprogramm in beiden Versuchen sofort nach dem Aufruf aus der Schleife ausstieg, ist noch nicht geklärt. Geprüft im Prüfstand mit einer nachgebauten Konsole (Systembibliothek und Lader als Attrappen; das System läuft dabei in einem eigenen Prozess wie das echte, damit ein gestorbenes Hilfsprogramm es nicht mitnimmt): 506 Prüfungen der Installation, je ein Lauf unter ASan/UBSan und unter TSan, alle grün, darunter das Hilfsprogramm, das direkt nach dem Aufruf, mitten in der Installation und bei der dritten Frage stirbt, eines, das hängt, und der Fall ohne jedes neue Hilfsprogramm; 15 Mutationsproben (jede nimmt eine Sperre heraus, der Test muss scheitern), alle erkannt; 161 Prüfungen des Servers unter ASan/UBSan und TSan, 7 Mutationsproben (dabei fiel auf, dass die Zeitwerte des Tests zu dicht beieinander lagen und das Entfernen der Kopf-Frist unbemerkt blieb: Test geschärft); 70 des Teilens, 67 des Paket-Lesens, 352 des Sicherns, 16 des Protokolls (Steuerzeichen, mit Mutationsprobe); eine Probe, dass die Zeilen-Fälschung das erkannte Spiel nicht mehr täuscht; alle 46 Szenarien des Browser-Prüfstands (feindliche Texte, Abbrechen, Fehler, Fremdstart, das Ende ohne Hilfsprogramm; die wichtigsten auch mit der strengen Inhaltsrichtlinie). Beim Web-Test aufgefallen und behoben: Nach „Abbrechen“ (auch beim Aufteilen, ein Fehler aus P1) kamen die Karten-Knöpfe erst nach dem nächsten Neuladen zurück. | **Die erste Fassung scheiterte an der Konsole (siehe links); die jetzige (Hilfsprogramm ohne Terminate, Ersatz, Notbetrieb) ist an der Konsole noch nicht bis zum Ende erprobt.** Beim nächsten Versuch zu prüfen: ob das Hilfsprogramm jetzt in der Schleife bleibt (die Zeilen `fds …` und `serve: …` in `pkginst-helper.log` sagen es), ob `completed` und die Byte-Zahlen der Konsole zusammenpassen (sonst bleibt die Installation hängen und endet nach 20 min als Fehler), Status „none“ gleich nach dem Start (15 s Gnade) |
| `[ ]` | **P3 — Direkt vom PC** (WebSocket-Upload/-Stream des PKG Managers als Vorbild) | offen |
| `[ ]` | **P4 — Netzwerk/SMB** (libsmb2) | offen |
| `[ ]` | **P5 — Reste aufräumen** (abgebrochene Installationen; nur mit starken Sicherungen) | offen |
| `[ ]` | **P6 — PKG entpacken** | offen; zuerst Machbarkeit klären und berichten |

## Nach 1.48.0 (05.10.2026)

| | Was | Stand |
|---|---|---|
| `[x]` | **Klare Meldung, wenn sich ein Laufwerk abmeldet** (`src/ioerr.h`). Auslöser: vier Konvertierungen auf USB endeten mit „Schreibfehler: Device not configured“; schuld war das USB-Kabel (Kernel-Log: `umass0: USB_ERR_TIMEOUT`, `da1 … detached`, gleich wieder angemeldet). Die Meldung sagt jetzt, was passiert ist, was zu tun ist und dass bei Störungen danach ein Neustart der Konsole hilft. Gilt für Kopieren, Konvertieren, Spielstände und Paket-Teilen; die Fehlerfelder fassen 640 statt 256 Zeichen. | eingebaut, auf dem Host geprüft, an der Konsole noch nicht gesehen |
| `[x]` | **Power-Optionen:** Rahmen 2 px und heller; die Gruppe heißt „Power-Optionen“ (Wunsch des Users) | eingebaut |
| `[x]` | **Erste echte Installation (P2) an der Konsole:** LEGO Batman, 31,8 GB, 05.10.2026 22:46–23:02, ~37 MB/s, 0 Lesefehler, danach in der Bibliothek, aber im Notbetrieb: jeder Helfer verlor die Verbindung. **Ursache (06.10.):** `sceAppInstUtilGetInstallStatus` schreibt 712 Bytes, das vom PKG Manager übernommene `install_status_t` hat 600; die 112 Bytes überschrieben Stapelvariablen des Helfers (seine Verbindungsnummer wurde 0 = Socket des Laders). Abhilfe: eigener statischer 16-KiB-Puffer (Rest markiert, Protokoll „wrote up to byte“); dazu Verbindung im Helfer ab fd 100, in der App ab fd 200, Diagnose `pkg_install_ipc`, `pkg_install_blind` auch im Verlust-Pfad. **Zweiter Fehler:** Die Konsole zählt ohne Kopf/Signatur (LEGO: 34 064 826 368 B für eine Datei von 34 136 974 352 B) und endet bei „playable“; das gilt jetzt als fertig, wenn alles geliefert ist und `downloaded >= total` der Konsole. Prüfstand: fakesys `status_overrun`, `playable_short`, `steal_low_fd_at_status`, Befehl `steal_app_fd`; 543 Prüfungen ASan+TSan, 9 Mutationen erkannt. | an der Konsole bestätigt 06.10.2026: Moorhuhn „Paket installiert“, 0 Ersatzhelfer |
| `[x]` | **Spiele und Sicherungen löschen** (`gamedelete.c`, Wunsch des Users 06.10.2026: „für alle Spiele, saubere Deinstallation bzw. sauberes Löschen“, „vorher bestätigen“). Umfang vom User gewählt: installierte Spiele (Sonys `sceAppInstUtilAppUnInstall` + `…Pat` + `…Addcont`, im Helfer, neue Operation `PKGI_OP_UNINSTALL`, Protokoll Version 4), ShadowMountPlus-Spiele (SMP `POST /api/v1/games/uninstall`, dann Abbild/Ordner löschen; SMP allein bindet sonst wieder ein), Sicherungen der App (neuer Reiter). Spielstände bleiben. Bestätigung: Plan mit Einmal-Kennwort (10 min), Häkchen + zwei Klicks. Sicherungen: nie Verknüpfungen folgen, kein Laufwerkswechsel, nur unter /mnt/usb*, /mnt/ext*, /data, nie das Laufwerk selbst oder der App-Ordner. Prüfstand `scratchpad/lead/dltest` (43 Prüfungen), Mutationsproben, Web-Szenario DL/DL2. | eingebaut, auf dem Host geprüft, an der Konsole offen |
| `[-]` | ~~**„Cache leeren“ und „Datenbank neu erstellen“ als Power-Optionen.**~~ Sony bietet beides nur im abgesicherten Modus an (Menüpunkt 5); ein direkter Weg war nicht zu finden (psdevwiki-Seiten „Safe Mode“, „Filesystem“, „CP Box Non Volatile Storage“ ohne Angaben dazu). „Datenbank neu erstellen“ wirft laut Elf Arsenal PS4-Fake-PKG-Spiele aus `app.db` und löscht ihre Ordner auf der internen SSD. Der User verwarf die beiden Knöpfe am 05.10.2026. | verworfen |

## Nach 1.49.1 (07.10.2026)

| | Was | Stand |
|---|---|---|
| `[x]` | **Seitendateien komprimiert** (`src/assets.c`, `tools/gen_assets.py`): HTML, CSS, JS, JSON und SVG liegen gzip-gepackt in der ELF (Stufe 9, nur wenn es sich lohnt, < 90 %) und gehen mit `Content-Encoding: gzip` an Browser mit `Accept-Encoding: gzip`; alle anderen bekommen den Text entpackt (libdeflate). Die Seite (HTML + Skript + Stil) schrumpft von 611 auf 160 KB; die ELF wurde dabei kleiner (6,77 → 6,28 MB), bevor die Wörterbücher dazukamen | eingebaut, Test `assettest` (24 Prüfungen) |
| `[x]` | **Dateimanager: ordnen, auswählen, ansehen.** `GET /api/v1/files/view` (Bilder PNG/JPG/GIF/WebP/BMP/ICO bis 16 MB, Textanfang bis 256 KB, Standard und höchstens 1 MB; Binärdateien 415; strenge Kopfzeilen: `default-src 'none'; sandbox`, `nosniff`, nie SVG) und `GET /api/v1/files/size` (8 s Zeitgrenze, `partial`); auf der Seite Sortieren nach Name/Größe/Datum, „Alle auswählen“, Ordnergrößen, „Ansehen“ | eingebaut, `fmtest` 80 Prüfungen, Browser 64 |
| `[x]` | **Sechs Sprachen** (Deutsch, English, Italiano, Español, Français, Русский). **Weg:** Übersetzung zur Laufzeit im Browser (`web/i18n-boot.js`, `web/i18n.js`), Wörterbuch je Sprache `web/lang/<xx>.json` (Schlüssel = deutscher Quelltext, Muster mit `{0}`, an „ · “ zerlegt, MutationObserver), nur das gewählte Wörterbuch wird geladen; der Quelltext bleibt deutsch. Gründe: kein zweiter Satz Seiten zu pflegen, Server und Schnittstelle unverändert, Sprache wechselt ohne Neustart der App. Übersetzt von Opus-Agenten nach einer Anleitung mit Fachwörterbuch (Sonys Wortwahl der PS5-Menüs), geprüft mit eigenen Prüfern (Platzhalter, HTML, Länge). Texte: rund 2550 je Sprache; fehlende erscheinen deutsch, der Browsertest sammelt sie (`PS5I18N.misses()`). **Russisch hat Mehrzahlformen** (`{0} {0\|файл\|файла\|файлов}`, Laufzeit `PS5I18N.plural`), die anderen Sprachen schreiben Einzahl und Mehrzahl als eigene Muster. **Bleibt deutsch:** Meldungen, die die Konsole selbst auf dem Fernseher einblendet (`notify.c`), Entwicklerdokumente und Schnittstellenbeschreibung | eingebaut; Konsole: nicht getestet |
| `[x]` | **Handbuch, FAQ und README in allen sechs Sprachen.** Quellen `tools/i18n/docs/docs_src.<xx>.json` (Texte) + `docs_style.css`; `tools/i18n/docs_build.py` baut die Fassung ohne Bilder für die App (`web/handbuch[.xx].html`, `web/faq[.xx].html`, Sprachleiste, Logo als Datei der App) und die mit Bildschirmfotos für PDF/HTML zum Release | eingebaut |
| `[x]` | **Zieltemperatur bis 91 °C** (Wunsch des Users nach Vergleich mit SMPlusGUI). **Befund SMP:** ShadowMountPlus 1.7beta2 kennt `fan_target_temperature=system\|50..91` und schreibt beim Fokus eines Spiels **einmal** einen festen Wert in dieselbe Stelle des Lüfterbausteins wie diese App (`/dev/icc_fan`, Byte 5, die Schwelle); danach regelt die Konsole selbst, ohne Rückmeldung. **91 °C ist der Wert, den die Firmware bei jedem Zustandswechsel selbst einstellt** (gemessen 24.09.2026): ein Ziel von 91 ist „so leise wie ohne die App“. **Eingebaut:** Ziel 60–91 (vorher 78), Notfallgrenze bis 95 (folgt dem Ziel + 4), feste Schwelle im Modus „Beobachten“ bis 91 (vorher 80), Schnellwahl 62 / 77 / 91 (vorher 62 / 70 / 78), die Ruhelage der Schwelle liegt bei **Ziel + 10 °C, mindestens 80, höchstens 91** (`rest_threshold_c` in `fan.c`; mit der festen 80 hätte ein Ziel über 80 nie die Ruhe erreicht, die es verspricht) | eingebaut; Konsole: nicht getestet |
| `[x]` | **Regelkreis-Simulation** (Host-Build mit Streckenmodell der Firmware 12.00, `plantstubs.c`, Echtzeit, alter Stand 1.49.1 gegen neuen): Ziel 66: Schwelle und Drehzahl in allen 55 Messpunkten **identisch**; Ziel 78 bei Last über dem Ziel: **identisch**; Ziel 78 darunter: Mittel der Drehzahl 16 % statt 17,8 % (neue Ruhelage 88); Ziel 91: Schwelle erreicht 91 nach etwa 130 s, Notfall bei ≥ 95 °C; Ziel 85: Notfall bei ≥ 89 °C | belegt im Host, nicht an der Hardware |
| `[x]` | **Warnschwelle geht mit dem Ziel mit** (Wunsch des Users, 07.10.2026: „Automatisch“): wirksam ist max(eingestellt, Ziel + 2), die Hauptchip-Schwelle bleibt 5 °C darüber; SMP hat keine Warnung (es schreibt nur den festen Wert) | eingebaut |
| `[ ]` | **Sprachen an der Konsole:** Meldungen auf dem Fernseher deutsch; einzelne seltene Texte (Zustandswörter der Paket-Installation, „gesucht <Zeit>“) noch ohne Übersetzung | bekannt |
| `[x]` | **Payload-Profile** (Wunsch des Users nach dem Vorbild seines angepassten ps5-payload-manager, 07.10.2026): benannte Abläufe aus Payloads und Pausen, Startprofil mit Schutz vor Endlosschleifen (Markierung `payload-profile-run.txt`), Import von `.elf`-Dateien vom PC (derselbe Upload wie im Dateimanager), Profile sichern/laden als Datei. `src/payprofiles.c`, `/api/v1/payload-profiles*`; Idee von itsPLK (GPL-3.0), neu gebaut. Nicht übernommen: Autoloader-Anbindung, Sicherung/Wiederherstellung per USB, „Neuigkeiten“, Symbol der Kachel | eingebaut; Host-Test 18 Prüfungen, Browser 18; Konsole: nicht getestet |
| `[x]` | **fakelib2 und Backport-Ordner** werden erkannt wie bei ShadowMountPlus (fakelib2 ersetzt fakelib; `<Scanpfad>/backports/<TITEL-ID>/` geht vor); `mods.fakelib2` und `mods.libs_from` in `/api/v1/library`, im Abbild-Leser zuerst `/fakelib2`. Die globale Bibliothek (`/data/shadowmount/fakelib`) wird bewusst nicht je Spiel gezeigt | eingebaut; Test `lcache` (4 Prüfungen) |
| `[x]` | **Spielstände: Benutzer-Bild, Spielname bei der Titel-ID, größere Benutzerkachel**; Namen kommen aus der Bibliothek, sonst aus `param.json`/`param.sfo` in `appmeta` und bleiben in `title-names.json` gespeichert (`GET /api/v1/saves/avatar`) | eingebaut; Konsole: nicht getestet |
| `[x]` | **Spielstände: PS5/PS4 und Spiele/Apps getrennt, Löschen** (Wunsch des Users, 07.10.2026): `kind` je Titel (`applicationCategoryType` in `param.json`, `CATEGORY` in `param.sfo`, `NPXS…`), Gruppen in der Liste, `POST /api/v1/saves/delete` mit Sicherung des jetzigen Stands davor (Art „undo“) und Entfernen der Ordner; Host-Test 12 neue Prüfungen, Browser 7 | eingebaut; Konsole: nicht getestet |
| `[x]` | **Absturz gefunden und behoben:** 300-KB-Struktur der Profile lag auf dem Stack eines Threads (App starb 25 s nach dem Start bzw. beim ersten Abruf); jetzt auf dem Heap. Hosttests merken das nicht (großer Stack): große Strukturen nie auf den Stack | behoben, an der Konsole bestätigt |
| `[x]` | **Pakete direkt vom PC installieren** (Wunsch des Users, 07.10.2026; Vorbild: „Direct Install“ des PKG Managers von itsPLK, GPL-3.0, neu gebaut): `src/pkglive.c`. Der Browser schickt die `.pkg` in Stücken zu 1 MiB (`PUT /api/v1/packages/live/segment`), die Konsole installiert währenddessen; nichts wird gespeichert. Ring von 64 Stücken im Arbeitsspeicher (erstes Stück fest), nachgefragte Stücke zuerst, dahinter Vorlauf hinter jedem lesenden Strom; ein Stück, das gelesen wurde, wird zuerst ersetzt. Der Kopf wird durch denselben Ring gelesen (`ps5tm_pkg_parse_reader`), die Seite arbeitet im Wartemodus (`state?since&wait`, ohne Zeitgeber, damit ein Hintergrund-Reiter weiterläuft). Anschlüsse: `pkgscan.c` (ids `live-N`), `pkgstream.c` (Scheibe mit Pfad `live:`), `pkginstall.c` (kein Dateicheck, Bild über den Ring, Sitzung endet mit dem Auftrag). Sicherungen: Browser 40 s still → Abbruch mit Satz, Stück 2 min nicht da → Abbruch, ungenutzte Sitzung 3 min, offener Plan 30 min | eingebaut; Install-Test 80 Prüfungen (ASan, TSan 59), Host 18, Browser 15; Konsole: nicht getestet |
| `[x]` | **QR-Code mit der Adresse der Seite** (Wunsch des Users nach dem Blick in webhb 0.4.1, 08.10.2026; nur der QR-Code, nicht der Zugangscode): `src/qr.c` (Encoder aus webhb, GPL-3.0, slopmaster33; Byte-Modus, Stufe M, Version 1–6), `GET /api/v1/qr` (nur die eigene Adresse, kein Text), Karte „Am Handy öffnen“ auf der Systemseite (Bild als `data:`-Adresse, CSP erlaubt es). Prüfung: Lesegerät (OpenCV) liest die Bilder aller sechs Versionen an den Grenzen (106 Byte) und mit Umlauten zurück, ASan/UBSan, Browser 5 | eingebaut; Konsole: nicht getestet |
| `[x]` | **Neuer Lüfter in der Oberfläche** (Entwurf und Code des Users, 08.10.2026; Variante C + D nach Bildmustern): `web/ps5-fan.js` (Web Component, für den alten WebKit der Konsole abgesichert: kein `aspect-ratio`/`inset`/`rgb(r g b / a)`, `MediaQueryList.addListener`-Ersatz, Beschriftung von der Seite), `fanVisual()` in `app.js` (Farbe als `--fan-rgb`, färbt Kachel und Chip), kleiner Chip neben dem Verbindungsstatus, auf der Kühlungsseite ausgeblendet; Einstellung „Animation voll/reduziert/aus“; Selbstschutz „lite“/Stillstand bei langsamen Bildern. Die alten Bilder `fan-rotor.png`/`fan-frame.png` und der Ring sind weg. Bis 1.51.x drehte eine feste CSS-Animation, weil JS-Drehung an der Konsole ruckelte (v1.35.1): **ob die neue an der Konsole flüssig läuft, ist offen** | eingebaut; Browser 16 Prüfungen; Konsole: nicht getestet |
