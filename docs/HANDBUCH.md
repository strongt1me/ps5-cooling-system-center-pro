# Handbuch

[README](../README.de.md) · [Handbuch](HANDBUCH.md) · [API](API.md) · [Entwicklung](ENTWICKLUNG.md)

> **This document is in German.** The [README](../README.md) is in English, and the manual is also built into the app in seven languages (page "Manual").

Ausführliche Bedienung von PS5 Cooling & System Center - Pro, Stand v1.99.0. Die Kurzfassung
steht in der [README](../README.de.md); die Schnittstelle der App in [API.md](API.md). Dieses Handbuch ist das
Nachschlagewerk für Fortgeschrittene: Es nennt auch Pfade, Dateiformate, Grenzen und das Verhalten in Sonderfällen.

## Überblick: die Seiten der Oberfläche

Nach dem Start: Browser auf **`http://<PS5-IP>:8086`**. Der Port ist fest, weil die Kacheln diese Adresse benutzen.

- **Kühlung** — Klartext-Status, wärmster Punkt, Lüfter (Ring) und laufendes Spiel; darunter Zieltemperatur mit
  Schnellwahl, Betriebsart (Leise/Ausgewogen/Kühl), Nachtmodus, Lüfterkurve, Temperaturverlauf, Sensoren und Profile pro
  Spiel. Die Expertenansicht ergänzt Regelzustand, Lüfterdiagnose, Verläufe und erweiterte Parameter.
- **Spiele** — die Spiele des Startbildschirms (starten, kopieren, konvertieren, packen, löschen) und die Reiter
  **Spielzeit**, **Spielstände**, **Pakete**, **PS4 PKG von USB**, **KrakenMount**, **Disc**, **Sicherungen** und **Verlauf**.
- **Dateimanager** — die Ordner der Konsole, der Laufwerke und NTFS-Laufwerke.
- **Payloads** — laufende, gespeicherte und auf USB liegende Payloads, Profile (Abläufe) und „Payloads herunterladen“.
- **System** — ShadowMountPlus, Konsole, Firmware, Netzwerk, Bildschirm, QR-Code „Am Handy öffnen“, Speicherplatz (mit
  „Auswerfen“), Kühlleistung über die Zeit, Zusatzabfragen; der **Expertenmodus** (nur hier, ab Werk aus) blendet Diagnose
  und Rohwerte ein.
- **Profil** — Anzeigename, Profilbild, Offline-Konto, Fake-Anmeldung, Profile sichern und übertragen.
- **Protokoll** — Ereignisse der App (als `.log` exportierbar) und das Kernel-Log live.
- **Credits**, **Handbuch** und **FAQ** — Handbuch und FAQ sind in der gewählten Sprache eingebaut (ohne Bildschirmfotos)
  und öffnen am PC in einem neuen Tab.

**Power-Optionen.** In der Kopfzeile jeder Seite stehen vier Knöpfe: **Ruhemodus**, **Neu starten**, **Ausschalten** und
**Abgesicherter Modus**. Sie wirken beim ersten Klick, ohne Rückfrage; solange ein Befehl läuft, ist der Knopf gesperrt.
Nicht gespeicherte Spielstände gehen beim Ausschalten und Neustart verloren. Der abgesicherte Modus ist ein Menü beim
Hochfahren; „PS5 neu starten“ führt von dort zurück.

**Design.** Neben der Sprachwahl stehen vier Designs: **Standard** (dunkelblau, leuchtende Ränder), **Hell**, **Dunkel**
und **Optimal** (für Fernseher und PS5: kräftiger Kontrast, größere Schrift, ohne Transparenz, am schnellsten gezeichnet).
Jedes Gerät merkt sich seine Wahl im Browser.

**Protokolldatei.** Jedes Ereignis steht auch in `/data/PS5-Cooling-Center/app.log` (spätestens 2 s später, Warnungen
sofort; ab 1 MB wird die Datei zu `app.old.log`). Jeder Start beginnt mit `===== … gestartet =====`, ein reguläres Ende
schreibt `===== … beendet: <Grund> =====`; fehlt diese Zeile, sagt es der nächste Start.

**Rückfragen.** Beenden, Abbrechen, Starten und die Power-Optionen wirken sofort. **Löschen** fragt immer zuerst und nennt,
was weggeht (Name, Art, Ort, Größe) – im Browser wie in der Vollbild-App.

## Kühlung

### Betriebsarten

| Modus | Verhalten |
| --- | --- |
| `automatic` (Automatik an) | Komfortregelung: hält die Zieltemperatur, sanft und träge. |
| `observe` (Automatik aus, „Beobachten“) | Nur Beobachtung. Der Lüfter wird nur angefasst, wenn per Schnellwahl oder „Direkt setzen“ eine feste Schwelle gesetzt wurde (45–91 °C). Sie bleibt gespeichert (`fan_threshold_pinned`) und gilt auch nach einem Neustart der App; ohne sie bekommt die Konsole ihre eigene Einstellung (91 °C) zurück. |

### Komfortregelung

Ziel ist **nicht** die niedrigste Temperatur, sondern ein ruhiger, unauffälliger Lüfter (angelehnt an die
Temperaturregelung von Xbox 360 DashLaunch).

| Parameter | Standard | Bedeutung |
| --- | --- | --- |
| `target_temp_c` | 66 °C | zu haltende Temperatur (50–91, bis 72 empfohlen) |
| `profile` | comfort | comfort / balanced / cool (= Leise / Ausgewogen / Kühl); legt Band, Mittelwert, Schrittweite und Komfortgrenze fest |
| `control_band_c` | 14 / 10 / 8 K | Regelband je Profil |
| `deadband_c` | 1 °C | Ruhezone nur beim Runterregeln |
| `control_interval_s` | 1 s | Regelschritt und Messung jede Sekunde |
| `average_window_s` | 4 s | gleitender Mittelwert (3 s bei Kühl) |
| `max_step_pct` | 1 % | Änderung je Schritt (2 % bei Ausgewogen und Kühl) |
| `safety_temp_c` | 78 °C | Notfallgrenze (72–95; wirksam mindestens Ziel + 4) |
| `fan_cap_pct` | 100 | Obergrenze der Drehzahl im Normalbetrieb (30–100) |
| `warning_follows_target` | 1 | Warnschwelle folgt dem Ziel |

So arbeitet die Regelung:

- Sie nutzt **nie** den Rohwert, sondern den gleitenden Mittelwert; der **Trend** zählt mehr als der Einzelwert (fällt die
  Temperatur schon, wird nicht weiter hochgeregelt).
- Die Drehzahl folgt der **weichen Lüfterkurve** (unten) und steigt schon 10 °C unter dem Ziel sanft an. Runterregeln erst
  nach 20 s stabil niedriger Temperatur, bei deutlich zu kühler Konsole schneller.
- **Komfortgrenze**: Im Normalbetrieb dreht der Lüfter höchstens auf 65 % (Leise), 85 % (Ausgewogen) bzw. 100 % (Kühl),
  damit ein unerreichbares Ziel ihn nicht dauerhaft hochjagt. Ab `safety_temp_c` gelten Kurve, Trend und Komfortgrenze nicht.
- **Schnellwahl** auf der Karte „Zieltemperatur“: In der Automatik setzt sie das Ziel (Kühl 62, Ausgewogen 77, Leise 91 °C),
  im Beobachten eine feste Schwelle (58, 65, 74 °C) – sofort, ohne „Übernehmen“.
- **Ziele über 72 °C** sind erlaubt; die Konsole läuft dann wärmer und leiser. Die Notfallgrenze liegt immer mindestens bei
  Ziel + 4 °C (bei 91 °C also 95 °C); der eingestellte Wert bleibt gespeichert und gilt wieder, wenn das Ziel sinkt.
  **91 °C** ist der Wert, den die Firmware bei jedem Zustandswechsel selbst einstellt: Ein Ziel von 91 °C heißt „so leise wie
  ohne App“, erst darüber greift die Regelung ein. Die Firmware schützt sich unabhängig davon selbst.
- **Warnschwelle**: Mit `warning_follows_target` (ab Werk an) gilt in der Automatik max(`warning_cpu_c`, Ziel + 2 °C) –
  beim Spielprofil das Ziel des laufenden Spiels; die Hauptchip-Schwelle liegt 5 °C darüber. Aus = genau `warning_cpu_c`.
- **Profile pro Spiel**: Läuft ein Spiel, speichert „Aktuelle Einstellung merken“ Ziel und Betriebsart für diesen Titel; die
  App schaltet dann automatisch um. Voraussetzung ist die Spielerkennung unter *System → Zusatzabfragen*. Export und Import
  der Spielprofile stehen auf der Seite Profil.

### Schwelle statt Drehzahl

Der Lüfterbaustein (`/dev/icc_fan`) kennt **keine** Drehzahlvorgabe. Der einzige Hebel ist eine **Schwellentemperatur**:
Liegt sie über der Temperatur, bleibt der Lüfter auf seiner leisen Grundkurve; liegt sie darunter, dreht die Firmware
proportional zum Abstand auf (etwa 5 % je Grad). Die App rechnet deshalb zweistufig: Der Regler rechnet in Prozent, ein
Servo überträgt das auf die Schwelle, greift erst ab 7 Prozentpunkten Abweichung ein (im Notfall ab 4) und kalibriert sich
an der **gemessenen** Drehzahl.

- **Zurücklesen:** Alle 2 s liest die App die Schwelle zurück und setzt sie sofort neu, wenn die Firmware sie verändert hat
  (das tut sie bei jedem Spiel- oder App-Start). Lässt sie sich nicht lesen, wird sie alle `fan_reapply_sec` (15 s) gesendet.
- **Ruhelage:** Ohne Bedarf parkt die Schwelle bei Zielen bis 70 °C auf 80 °C, darüber auf Ziel + 10 °C (höchstens 91 °C);
  sonst könnte eine Konsole mit hohem Ziel nie so leise laufen, wie das Ziel verspricht. Über dem Ziel senkt der Servo die
  Schwelle um 1–3 °C alle 3 s (im Notfall alle 2 s).
- **Hohes Ziel (ab 85 °C):** Steigt die Temperatur schneller als 6 °C pro Minute, liest die App die Kurve bei der in etwa
  20 s erwarteten Temperatur (höchstens 6 °C voraus). Bei niedrigeren Zielen oder ruhiger Temperatur ändert sich nichts.
- **Mindestdrehzahl der Konsole:** Die Konsole dreht nie unter einen eigenen Mindestwert (gemessen rund 20 % im Leerlauf,
  34 % in Spielen). Die App lernt ihn im Betrieb und zeigt ihn in der Lüfterdiagnose; die Soll-Ist-Abweichung rechnet damit.
- **Ein Sensor fällt aus:** Antwortet nur einer der beiden Sensoren (Prozessor, Hauptchip) nicht, gilt sein letzter Wert
  bis zu 10 s weiter; danach regelt die App nach dem anderen allein (`sensor_single`). Fallen beide aus, hält sie den Lüfter
  nach 10 s auf mindestens 70 %.

### Nachtmodus

Zwischen zwei Uhrzeiten (zum Beispiel 23:00–07:00, auch über Mitternacht) gilt statt der Zieltemperatur das **Nachtziel**
(50–91 °C, meist wärmer und leiser). Eine Spielregel geht vor; gleiche Uhrzeiten bedeuten „nie“. Die Konsole kennt keine
Zeitzone: Die Seite schickt beim Speichern den Zeitversatz des Geräts mit und zieht ihn bei Sommer-/Winterzeit still nach.
Protokoll `night_mode_on`/`night_mode_off`; im Status `control.night_active` und `control.effective_target_c`.

### Lüfterkurve, Obergrenze, Regelaufzeichnung

- **Kurve** (`band_duty` in `src/fan.c`): Ab 10 °C unter dem Ziel steigt der Wunsch von der üblichen Mindestdrehzahl (23 %)
  auf **28 % (Leise), 33 % (Ausgewogen), 40 % (Kühl)** am Ziel und von dort bis 100 % über das 1,75-Fache des Bandes (Leise
  rund 24 K, Ausgewogen rund 17 K, Kühl 14 K über dem Ziel). Ein Wunsch unter der Mindestdrehzahl der Konsole ändert nichts.
  Schnelles Aufholen (halbe Lücke je Sekunde) gilt nur nahe der Notfallgrenze.
- **Karte „Lüfterkurve“** (Vollbild-App: Reiter „Lüfterkurve“ unter Kühlung) zeigt die Kurve mit der Temperatur von jetzt
  (`GET /api/v1/fan/curve`).
- **Obergrenze der Lüfterdrehzahl** (`fan_cap_pct`, 30–100 %): senkt nur die Grenze des Normalbetriebs; Notfallregelung und
  Lüfterwächter beachten sie nicht.
- **Regelaufzeichnung** (`src/fantrace.c`): die letzten 30 Minuten im Speicher, eine Zeile je Sekunde
  (`zeit_utc;cpu_c;soc_c;mittel_c;wunsch_pct;luefter_pct;schwelle_c;ziel_c;profil;spiel;sicherheit;automatik`).
  „Regelaufzeichnung herunterladen“ liefert sie als CSV, `POST /api/v1/fan/trace/save` legt sie als
  `regelaufzeichnung-<Datum>-<Zeit>.csv` im Datenordner ab. Wechsel von Profil oder Ziel stehen im Protokoll.

### Lüfter-Selbsttest und Lüfterdiagnose

In der Expertenansicht zeigt die Karte **Lüfterdiagnose (Soll / Ist / Clamp)** effektives Ziel, aktive Spielregel,
Soll-Ist-Abweichung, Anschlag und einen 60-Sekunden-Trend, dazu nummerierte Handlungsempfehlungen. **Lüfter jetzt testen**
setzt die Schwelle 8 °C unter die aktuelle Temperatur (nicht unter 30 °C) und prüft, ob die Drehzahl binnen 30 s um
mindestens 8 Prozentpunkte steigt (**reagierend**) oder nicht (**reagiert nicht**, mit möglichen Ursachen: Firmware
beachtet die Schwelle nicht, kstuff fehlt, Steuerung nicht verfügbar). Nur bei kühler Konsole (höchstens 72 °C, in der
Automatik unter dem Ziel) und lesbarer Drehzahl; der Lüfter wird bis zu 30 s hörbar lauter, der Knopf bricht ab.

### Lüfterwächter

`src/fanguard.c` ist ein eigener Faden, unabhängig von der Regelung, und liest die Temperaturen jede Sekunde selbst.

- **Totmann:** Meldet sich die Regelung 20 s nicht, gibt er den Lüfter an die Konsole zurück; ist es heiß, stellt er volle
  Kühlung.
- **Übertemperatur:** Bleibt die Temperatur 10 s über Notfallgrenze + 6 °C, obwohl der Lüfter nicht nahe am Maximum läuft,
  hält er volle Kühlung, bis es 8 °C kühler ist; meldet der Lüfter Höchstdrehzahl, greift er nach 60 s trotzdem ein.
- **Absturz:** Signalbehandlung auf eigenem Stapel; die Konsole bekommt den Lüfter zurück.
- Lässt er los, übernimmt die Regelung; im Beobachten ohne feste Schwelle bekommt die Konsole 91 °C zurück
  (`fan_released_late`). Solange er hält, zeigt der Status „Wächter“, und eine feste Schwelle von Hand wird mit
  `409 fanguard_holding` abgelehnt.

**Beenden der App.** Bei SIGTERM oder SIGINT gibt die App den Lüfter zuerst an die Konsole zurück (`fan_stop`) und endet
binnen weniger Sekunden (klemmt der Lüfterfaden, nach 5 s per Alarm). **SIGKILL** lässt sich nicht abfangen: Dann bleibt
die letzte Schwelle stehen, bis die Konsole beim nächsten Zustandswechsel selbst 91 °C setzt. Eine neue Ausführung beendet
eine ältere zuerst mit SIGTERM und erst nach etwa 6 s mit SIGKILL. Die App kann den Lüfter nur schneller machen, nie
langsamer als im Auslieferungszustand.

### Vergleich mit ShadowMountPlus (`fan_target_temperature`)

ShadowMountPlus kennt `fan_target_temperature=system|50..91` in seiner `config.ini`. Beide stellen dieselbe Größe ein, die
Schwelle des Lüfterbausteins (`/dev/icc_fan`), aber verschieden:

| | ShadowMountPlus | diese App |
| --- | --- | --- |
| Was | schreibt einen **festen Wert** als Schwelle | hält eine **Temperatur** und stellt die Schwelle laufend nach |
| Wann | einmal, wenn ein Spiel den Fokus bekommt; `system` = nichts | jede Sekunde; alle 2 s zurückgelesen |
| Rückmeldung | keine | Temperatur und Drehzahl, Mittelwert, Trend, Notfallgrenze |
| Pro Spiel | ein Wert für alle | Profile pro Spiel |

**Nicht beides zugleich:** Steht in ShadowMountPlus eine Zahl und läuft hier die Automatik, setzt die App ihre Schwelle
binnen etwa 2 s wieder durch (Protokoll: Überschreibung). Wer den Wert von ShadowMountPlus will, schaltet die Automatik aus
oder lässt ShadowMountPlus auf `system`.

### Die Ring-Anzeige

Die Lüfter-Kachel zeigt die **gemessene** Drehzahl als Ring (0 % leer, 100 % voll, Zahl in der Mitte). Die Farbe zeigt die
Lüfterlast, nicht die Temperatur: stufenlos von Blau (bis 25 %) über Grün (um 55 %) und Gelb (um 75 %) zu Rot (ab 95 %).
Ohne gültigen Wert ist der Ring grau und leer („--“).

### Voraussetzung kstuff

`/dev/icc_fan` ist nur mit geladenem **kstuff** erreichbar. Fehlt es, laufen Sensoren und Oberfläche weiter, die
Lüfterregelung meldet aber „nicht verfügbar“, mit genauem `errno` im Protokoll.

## Die Seite „Spiele“

**Woher die Liste kommt.** Aus der App-Datenbank der Konsole (`/system_data/priv/mms/app.db`): die sichtbaren Kacheln der
Spiele-Reihe des angemeldeten Benutzers, mit Spielzeit und „zuletzt gespielt“, wie die PS5 sie zählt. Suche nach Titel,
Titel-ID oder Content-ID; Filter Alle, PS5, PS4 und „Backport · AMPR · PlayGo“; Sortierung wie auf dem Startbildschirm,
nach Name, zuletzt gespielt, Spielzeit, Größe oder installiert. Die **Darstellung** ist **groß** (Vorgabe) oder **kompakt**
(kleines Bild, Knöpfe als Symbole; Maus, Steuerkreuz oder „i“ zeigen, was ein Symbol macht). Beide Ansichten haben dieselben
Knöpfe, Sperren und Hinweise; was für ein Spiel nicht geht, bleibt ausgegraut sichtbar, der Grund steht im Hinweis.

**Marken unter dem Titel:**

- **Format:** Dump-Ordner, exFAT, ffpkg, ffpfs, ffpfsc, PS4 PKG oder PS5 PKG. Installierte Spiele liegen als `app.pkg` unter
  `/user/app`; ob echt oder Fake-PKG, lässt sich nicht erkennen. Spiele von PS4Mount tragen **PS4Mount**, Spiele, die Kraken
  Mount eingehängt hat, **KrakenMount** (`fpkgmount` in `GET /api/v1/library`).
- **Firmware:** die Firmware, die das Spiel verlangt („FW 13.60“, bei PS4-Titeln „PS4-FW“). **Gelb** nur, wenn sie neuer ist
  als die der Konsole **und** auch das SDK, mit dem das Spiel gebaut ist (aus der `eboot.bin`, sonst `param.json`), neuer ist;
  passt das SDK, bleibt sie grau, denn dann startet das Spiel trotzdem. Mit Backport steht die Firmware, die der Backport trägt.
  Bei Spielen von Kraken Mount liest die App dafür dessen Zwischenspeicher (`/data/krakenmount/caches/<Titel-ID>`); die
  Bibliotheken, die Kraken Mount selbst beilegt (AMPR, PlayGo, DLC), zählen nicht als Backport. Die Vollbild-App zeigt in den
  Details zusätzlich „Gebaut mit SDK“.
- **Backport, AMPR EMU, PlayGo:** aus dem Spielordner eingehängter PS5-Spiele gelesen (Ersatzbibliotheken in `fakelib`,
  SDK-Version der `eboot.bin`). Abbilder lassen sich nur prüfen, solange sie eingehängt sind.
- **zstd gepackt / LZ4 gepackt:** Das Spiel liegt in AMPR-Packs (Ordner oder Abbild; bei Abbildern liest die App die
  Index-Dateien ohne Einhängen mit). Vollbild-App: farbige Plakette (grün zstd, blau LZ4).

**Infos & Metadaten** nennen den Ort, den ShadowMountPlus meldet (Dump-Ordner oder Abbild-Datei, nicht den Einhängepunkt
unter `/mnt/shadowmnt`), und „Gepackt“. **Pfad öffnen** springt im Dateimanager in den Ordner des Spiels bzw. der Abbild-Datei.

**Woher die Bibliotheken kommen.** Die Marken folgen der Suchreihenfolge von ShadowMountPlus: Ein Backport-Ordner
`<Suchpfad>/backports/<TITEL-ID>/` (auch `<TITEL-ID> Spielname`; der genau so benannte gewinnt, sonst der erste in
Byte-Reihenfolge) geht vor dem Spiel, und in beiden geht `fakelib2` vor `fakelib`; der erste vorhandene Ordner gilt allein,
auch ein leerer (`fakelib2` ersetzt, ergänzt nicht). Gesucht wird neben dem Spiel bzw. Abbild, in `/data/homebrew`,
`/data/etaHEN/games`, auf `ext0`/`ext1` und auf USB unter `etaHEN/games`; die Details nennen den Ordner. Die globale
Bibliothek `/data/shadowmount/fakelib` gilt für alle und wird nicht je Spiel angezeigt.

**Eigene Bibliotheken** legt man direkt in den `fakelib`-Ordner des Spiels (FTP oder Dateimanager); Hochladen über diese
Seite gibt es nicht. Bauen, Packen und Austauschen der mitgelieferten Fassungen geschieht im Fenster „AMPR & APR-EMU“ (unten).

**Covers & Metadaten.** Die App legt je Spiel unter `/data/PS5-Cooling-Center/covers_and_more/<Titel-ID>/` eine Kopie des
Titelbilds (`icon0.png`) und `meta.json` ab (Angaben, Größe des Spielordners, Backport/AMPR/PlayGo samt Merkmalen der
Dateien). So erscheint die Liste auch nach einem Neustart oder mit abgezogenem Stick sofort; eine vor weniger als sechs
Stunden gemessene Ordnergröße wird nicht neu gezählt, Geändertes (andere `eboot.bin`, neue Bibliothek, anderes Bild) wird
erkannt. Bilder über 6 MB werden nicht kopiert, alle zusammen bleiben unter 160 MB. Den Ordner darf man löschen; die App
legt ihn beim nächsten Lesen neu an.

**Doppelte Abbilder.** Liegt ein Spiel in mehr als einem Abbild, nimmt ShadowMountPlus bei jeder Suche mal das eine, mal
das andere. Die App sucht in der obersten Ebene jedes Laufwerks, eine Ebene darunter sowie in `homebrew` und `etaHEN/games`
nach `.exfat`, `.ffpkg`, `.ffpfs` und `.ffpfsc`, liest die Titel-ID (`param.json`/`param.sfo`, bei `.ffpfs` aus dem Namen)
und zeigt die Karte **Doppelte Abbilder** (eingeklappt). **Deaktivieren** benennt ein Abbild in `<Name>.off` um,
**Aktivieren** zurück; gelöscht wird nichts. Ein eingehängtes Abbild oder eines, dessen Spiel läuft, bleibt, wie es ist.

**Verlauf.** Der Reiter **Verlauf** zeigt alle Vorgänge der App (Kopieren, Konvertieren, Paket teilen, Installieren, Löschen,
Dateimanager, Spielstände, Disc-Abbilder, Dumps, AMPR-Index, APR-EMU, PlayGo, Packs, Abbild prüfen/entpacken/reparieren) mit
Ergebnis, Größe, Dauer und Zeit, nach Ergebnis filterbar. Die letzten 500 liegen in `/data/PS5-Cooling-Center/history.jsonl`;
**Verlauf leeren** fragt zuerst.

## Spiele starten und beenden

„Starten“ startet das Spiel direkt; läuft es schon im Hintergrund, holt die App es nach vorn. Im Browser der Konsole schließt
sich der Browser sofort beim Tippen, damit das Spiel vorn ist; Meldungen der Konsole erscheinen oben rechts.

- **Läuft schon ein anderes Spiel** (auch pausiert), schließt „Starten“ es und startet das gewählte in einem Zug („… wird
  geschlossen, danach startet …“). Das laufende Spiel speichert vorher nicht. „Beendet“ gilt erst, wenn das System den Bereich
  des alten Spiels (`/mnt/sandbox/<ID>_000`) entfernt hat und ShadowMountPlus eine Sekunde Zeit zum Aushängen hatte.
- **„Spiel beenden“** steht auf der Kühlungsseite an „Läuft gerade“ und auf der Karte des laufenden Spiels; es beendet sofort,
  ohne Rückfrage, wie „Close App“ in Elf Arsenal (der Spielprozess wird beendet, bis er weg ist). Ob ein Spiel läuft, fragt die
  App zusätzlich direkt den Kernel; so werden auch pausierte und hinter dem Browser gestartete Spiele erkannt.
- **Öffnet die Kachel nur eine Webseite** oder lehnt der Starter ab (etwa bei einem noch nicht eingehängten Abbild), startet die
  App nichts selbst, sondern zeigt oben rechts eine Meldung mit „Starten“, die den Link der Kachel öffnet.
- **Nie zweimal starten:** Einen laufenden Titel erneut zu starten, bringt die Konsole zum Absturz. Ein zweiter Klick startet
  deshalb nichts, solange der erste Start läuft und noch 30 s danach (je Titel).
- **„Device busy“:** Lehnt ShadowMountPlus einen Start kurz ab (Starter `0x80940033`, im Protokoll von ShadowMountPlus
  `launch mount unavailable … status=16`), fragt die App noch zweimal im Abstand von 3 s, bevor sie die Meldung mit „Starten“
  anbietet – im Browser, nach dem Ende der Vollbild-App, beim Spielwechsel und in der Dump-Warteschlange.

## Ein- und Aushängen über ShadowMountPlus

In der Spielkarte (Vollbild-App: Aktionen) hängen **Einhängen**, **Neu einhängen** und **Aushängen** ein Spiel über
ShadowMountPlus (ab 1.7beta10) ein oder aus – etwa wenn ein Spiel hängt oder nach Kopieren, Verschieben oder Löschen neu
erkannt werden soll. Nie, solange das Spiel läuft.

**Wichtig:** Ein eingehängtes Spiel, das nicht läuft, hält bei ShadowMountPlus den einzigen Platz für die Einhängung besetzt.
Bis es gestartet oder ausgehängt ist, startet kein anderes Spiel und keine Kachel (`launch mount unavailable: <Titel-ID>
status=16 (Device busy)`, an der Konsole ein Startfehler). Darum zeigt die Spieleseite einen gelben Hinweis mit „Aushängen“,
die Vollbild-App die Marke „EINGEHÄNGT“.

**Die App räumt selbst auf** (`src/smphold.c`): Jedes Spiel, das sie über ShadowMountPlus einhängt (Knöpfe der Karte,
Prüfung der Fakelibs, AMPR-Auftrag), merkt sie sich (auch in `/data/PS5-Cooling-Center/smp-mounted.txt`) und gibt es frei:

1. **sofort**, wenn sie selbst ein anderes Spiel startet (Browser, Vollbild-App, Dump-Warteschlange);
2. **sofort**, wenn die Konsole einen anderen Start ablehnt (das Kernel-Log nennt `launchApp(<ID>)`/`LaunchFlowError`): Die
   App hängt aus, schreibt `smp_hold_released` und meldet, dass der Start jetzt noch einmal versucht werden kann;
3. **sonst nach 30 s** ohne Start (nur solange kein Spiel läuft und kein Auftrag arbeitet; scheitert es, `smp_hold_failed` und
   ein neuer Versuch nach 10 s).

Was die App nicht selbst eingehängt hat (eigene Seite von ShadowMountPlus, dauerhafte Abbild-Einhängungen, Kraken Mount),
rührt sie nicht an; Hinweis und Marke gelten nur für `app_mounted` in der Spieleliste.

## Spiele kopieren und konvertieren

Auf der Karte eines Spiels, das als Dump-Ordner oder Abbild-Datei vorliegt: **Kopieren** legt es auf ein anderes Laufwerk,
**Konvertieren** macht daraus ein Abbild, **Verschieben** und das Entpacken eines Abbilds zurück zu einem Ordner erledigt
ShadowMountPlus selbst (damit es weiß, wo das Spiel danach liegt; zur Wahl stehen nur Ordner, in denen es sucht; bricht es
das Entpacken mit „Permission denied“ ab, entpackt die App selbst in denselben Zielordner). Der Streifen über der Bibliothek
zeigt diese Aufträge und auch Installationen, die ShadowMountPlus selbst ausführt.

**Ziel.** Drei Wahlmöglichkeiten: **A · Spielbar mit ShadowMountPlus** (Ordner `homebrew` des Laufwerks), **B · Spielbar
mit Kraken Mount** (Ordner `Kraken-Games`, dort sucht ShadowMountPlus nicht; bei PS4-Spielen gesperrt) und **C · Sicherung**
(`PS5-Sicherung/Spiele`, dort sucht niemand). Beim Format FPKG ist B vorgewählt und A gesperrt. Nichts wird überschrieben,
das Original bleibt unberührt, und bei Abbruch oder Fehler räumt die App ihre Dateien weg. Sichtbar wird ein Spiel erst, wenn
es vollständig ist: Bei einer Kopie kommt `sce_sys/param.json` zuletzt, ein Abbild trägt bis zum Schluss einen Zwischennamen.

### Formate

- **exFAT-Abbild** (`.exfat`) aus einem Dump-Ordner.
- **`.ffpkg`** (UFS2, unkomprimiert) im Aufbau, den ShadowMountPlus empfiehlt: 64-KiB-Blöcke und -Fragmente, kein
  reservierter Platz (`newfs -O 2 -b 65536 -f 65536 -m 0 -i 262144`). Jede Datei belegt volle 64-KiB-Blöcke; ein Spiel mit
  sehr vielen winzigen Dateien wird dadurch größer. Größe: Spieldaten (aufgerundet) plus gut 1,2 % Verwaltung und etwa 0,5 %
  frei, mindestens 64 MB, höchstens 512 MB Zuschlag; der Dialog nennt die genaue Größe vorab. Eine Datei darf über 4 TB groß
  sein; ein Ordner fasst bei 20-Zeichen-Namen etwa 24.000, bei 255-Zeichen-Namen etwa 1.500 Einträge (höchstens 32.765
  Unterordner).
- **`.ffpfsc`** (exFAT innen, komprimiert), der Aufbau, den MkPFS und ShadowMountPlus empfehlen. Auch ein exFAT-, ffpkg-
  oder ffpfs-Abbild lässt sich zu `.ffpfsc` machen. Ein Block wird nur komprimiert, wenn er mindestens 5 % spart; Kopf und
  Abschluss sind ausdrücklich als Nullen geschrieben, nicht als Loch in der Datei.
- **Debug-FPKG** (`.pkg`) für Kraken Mount, geschrieben von Dump Forge (unten).

Was nicht geht, meldet schon der Plan: Namen, die exFAT nicht darstellen oder nicht unterscheiden kann, zu lange Pfade, zu
große Dateien, überfüllte Ordner. Der Code für exFAT und ffpfsc ist aus [MkPFS](https://github.com/PSBrew/MkPFS) (GPL-3.0)
übertragen, der für ffpkg nach [UFS2Tool](https://github.com/SvenGDK/UFS2Tool) (BSD-2-Clause) neu geschrieben.

**PS4-Spielordner** (nur `sce_sys/param.sfo` und `eboot.bin`) lassen sich in alle drei Formate konvertieren; ShadowMountPlus
bindet PS4-Abbilder ab 1.7beta8 ein. Die App legt dabei eine `param.json` aus der `param.sfo` und bei Bedarf eine korrigierte
`npbind.dat` mit ins Abbild; der Ordner bleibt unverändert. Ziel nur USB oder M.2 (ein PS4-Titel aus dem Speicher der
Konsole bringt sie zum Absturz). Trophäen gibt es bei PS4-Spielen aus Abbildern nicht (`0x80551618`), und manche bleiben bei
„Bitte warten“ stehen: Beides verlangt eine echte Installation (Reiter „Pakete“).

**Ruckler-Warnung bei ffpfsc.** Ein komprimiertes Abbild muss beim Spielen ständig entpackt werden. Der Plan
(`GET /api/v1/library/convert/plan`) liefert `stream_risk` bei mehr als 40 GB Daten, großen Streaming-Containern (`.ucas`,
`.utoc`, `.pak`, `.forge`, `.streamdb`, `.resources`, `.cas`, `.rpf`, zusammen ab 4 GB) oder einem Ordner, dessen Name mit
„stream“ beginnt (ab 2 GB); dann steht beim Eintrag „ffpfsc“ ein Hinweis. Eine Schätzung an Namen und Größen, keine Prüfung
der Engine.

**Unsignierte Programmdateien.** Sind `eboot.bin`, `.prx`, `.sprx`, `.elf` oder `.self` einfache ELF-Dateien (`e_type`
`0xFE10`/`0xFE18`), startet die Konsole das Spiel nicht (`0x80020008`). Der Plan meldet sie (`unsigned_exec`). Beim
Konvertieren eines **PS5-Dump-Ordners** schreibt die App sie als **fake-signierte SELF** (FSELF, Aufbau wie `make_fself.py
--ptype fake`) in einen Hilfsordner `<Ziel>.ps5cc-sign-teil`, der über den Spielordner gelegt wird; jede Datei wird
zurückgelesen, ein `ampr_emu.index` an die neuen Größen angepasst, der Spielordner nur gelesen. Ausgenommen: PS4-Ordner,
Abbilder als Quelle (Byte für Byte übernommen), schon signierte Dateien. Beim Packen gilt dasselbe für „Als neuen Ordner …
daneben“ (LZ4); bei zstd ersetzt der Schalter **Programmdateien als FSELF signieren** (voreingestellt an) sie im Spielordner.

### Prüfung, Prüfsummen, Warteschlange

**Prüfung danach.** **Gründlich** (Vorgabe) liest das Ergebnis ganz zurück: Bei einer Kopie wird jede Datei gleich nach dem
Schreiben gelesen und ihre CRC-32 mit der des Originals verglichen; bei exFAT und ffpfsc die Prüfsumme der Nutzdaten, bei
ffpfsc zusätzlich Köpfe, Inodes, Blocktabelle, **jeder Block entpackt** und die leeren Bereiche; bei ffpkg Aufbau und Inhalt
jeder Datei, danach ein zweites Durchlesen. **Schnell** prüft bei ffpfsc den Aufbau und höchstens 2048 gleichmäßig verteilte
Blöcke, bei exFAT Größe und rund 400 Stücke, bei ffpkg alles in einem Durchgang; es entsteht keine `.sha256`, und ein
beschädigter Block außerhalb der Stichprobe bleibt unentdeckt.

**Prüfsummen-Datei.** Neben dem Ergebnis entsteht `<Name>.sha256` im Format von `sha256sum` (nicht bei „Schnell“, nicht bei
Abbildern mit Packs und nicht beim FPKG). Am PC im Ordner mit Ergebnis und `.sha256`:

```bash
sha256sum -c Spiel.ffpfsc.sha256      # Linux, WSL; auf dem Mac: shasum -a 256 -c …
```

ShadowMountPlus stört die Datei nicht; es durchsucht nur Ordner und Abbild-Dateien.

**Prüfung vor dem Start.** Das Fenster fragt bei jeder Wahl `POST /api/v1/library/convert/check` und zeigt die Befunde (Rot
sperrt Start und Warteschlange, Gelb warnt). Gelb steht dort auch ein beschädigtes Bild des Startbildschirms (`icon0.png`,
`pic0`–`pic2.png`: Kopf, jeder Abschnitt samt CRC-32 und `IEND` geprüft); es wird unverändert übernommen. Der **Name** der
Ausgabe kommt aus einer Vorlage mit `{titel}`, `{id}`, `{version}` und `{quelle}` (leer = Name des Ordners).

**Zur Warteschlange** legt den Auftrag in eine Warteschlange (höchstens 24 Einträge, im Speicher), die nacheinander abläuft;
scheitert ein Start an einem anderen Auftrag, wartet der Eintrag (alle zehn Sekunden ein neuer Versuch). Vor dem Schreiben
nimmt der Auftrag einen Fingerabdruck der Quelle; hat sich eine Datei bis zum Umbenennen geändert, wird das Ergebnis nicht
übernommen. Der Status nennt die Schritte (Vermessen, Schreiben, Prüfen, Umbenennen) mit ihrer Dauer.

### Original danach

Beim Konvertieren, Kopieren und Entpacken (auch über die Warteschlange) bestimmt „Original danach“, was mit dem Original
geschieht: **Behalten** (Vorgabe), **Löschen** oder **Verschieben** – damit derselbe Titel nicht zweimal auf der Konsole
liegt (ShadowMountPlus kommt damit nicht zurecht).

- **Löschen** fragt vorher (Name, Größe, Pfad); Spielstände bleiben. Gelöscht wird nur unter dem Einhängepunkt eines
  Laufwerks oder in `/data`, nie in der Ablage der App, nie bei laufendem Spiel. Wurde das Ergebnis nur „Schnell“ geprüft,
  bleibt das Original liegen (dann „Gründlich“ wählen).
- **Verschieben** macht zuerst eine geprüfte Kopie (jede Datei zurückgelesen, `.sha256` daneben) in einen Ordner deiner Wahl
  (interne SSD, M.2 oder USB; vorgeschlagen `PS5-Sicherung/Spiele`) und löscht das Original erst danach. Liegt das Ziel auf
  demselben Laufwerk (nicht bei der internen SSD), wird nur umbenannt. Abgelehnt werden Ordner, in denen ShadowMountPlus
  oder Kraken Mount sucht (`homebrew`, `etaHEN/games`, `Kraken-Games`, bei USB und M.2 die Wurzel bis zwei Ebenen tief),
  belegte Namen, zu wenig Platz und Wege über Verknüpfungen.
- Gelöscht oder verschoben wird erst, **nachdem der Vorgang gut zu Ende ging**. In dieser Zeit startet kein anderer Vorgang.
  Startet die App mittendrin neu, ist die Wahl verloren; der Verlauf und eine Meldung nennen, was offen blieb.
- Spielt Kraken Mount das Original (ohne ShadowMountPlus, siehe Kraken Mount), läuft „Löschen“ über Kraken Mount, und
  „Verschieben“ ist gesperrt.

Die Vollbild-App bietet dieselbe Auswahl; Schnittstelle in [API.md](API.md).

### Packen beim Konvertieren (AMPR-Packs)

Bei **exFAT** und **ffpkg** eines PS5-Spielordners gibt es die Gruppe **Packen (AMPR-Packs)**: **Nicht packen** (Vorgabe),
**LZ4-Packs** (Stufe 9 empfohlen, 3 schnell, 12 am kleinsten) oder **zstd-Packs** (Stufe 5 empfohlen, 1, 9, 19). Die
Laufzeit im Spiel ist immer **AMPR EMU NG**. Das Abbild schreibt dann **Dump Forge** und packt **im Strom**: Die Dateien
kommen beim Schreiben in die Packs, ohne Zwischenordner; das Original bleibt unverändert. Dump Forge liest die Quelle zweimal
(Vermessen, dann Schreiben) und legt NG selbst als `fakelib/libSceAmpr.sprx` ins Spiel; gepackt wird alles außer Programmen,
`sce_sys`, `sce_module`, `fakelib` und leeren Dateien. Den Index schreibt Dump Forge selbst, eine `.sha256` entsteht nicht.
Der Name der Ausgabe darf höchstens 63 Bytes haben. **Ausgegraut** bei PS4-Spielen, bei schon gepackten Spielen (erst
entpacken) und wenn sich das nicht prüfen lässt; für **ffpfsc** gibt es die Wahl nicht. Am Ende steht „<Größe> statt
<Größe des Spiels> (n %)“. Ob ein gepacktes Spiel läuft, hängt von AMPR EMU NG ab: zuerst an einem kleinen Spiel probieren.

### Debug-FPKG mit Dump Forge

Das Format **FPKG (Debug-.pkg)** schreibt **Dump Forge** (quer3q, GPL-3.0), ein eigenes PS5-Programm, das in der App steckt
(Fassung 0.6.0, zstd-gepackt in der ELF und damit auch in beiden Kacheln). Die App entpackt es für jeden Auftrag im Speicher,
prüft Größe und SHA-256, startet es über den Payload-Lader, zeigt seine Schritte und beendet es danach.

- **Stufen:** **Schnell** (Kraken, empfohlen), **Ausgewogen** und **Kleinste** (rund 2,6 % kleiner, aber etwa 6- bzw.
  9-mal so lange) und **Unkomprimiert** (falls die Konsole ein komprimiertes nicht startet).
- **Ziel:** B (`Kraken-Games`, vorgewählt) oder C. Name höchstens 63 Bytes (ohne „.pkg“). **Gründlich** liest jedes Byte des
  Pakets noch einmal, **Schnell** prüft Aufbau, kleine Dateien und eine Stichprobe.
- **Spiele mit AMPR-Packs** packt Dump Forge beim Umwandeln selbst aus (Kraken Mount nimmt kein Paket mit Packs an); das
  Paket enthält dann das ungepackte Spiel und wird entsprechend größer. Gesperrt nur, wenn sich das nicht prüfen lässt oder
  das Spiel zstd- und LZ4-Packs zugleich hat.
- Hängt das Original mit derselben Titel-ID bei ShadowMountPlus, vorher aushängen.
- **Neuere Fassung:** „Neue Fassung von GitHub laden“ (nur auf Knopfdruck, HTTPS, Prüfsumme aus `SHA256SUMS`) oder
  `ps5-dump-forge-<Version>-ps5.elf` von Hand nach `/data/PS5-Cooling-Center/dumpforge/` legen (1–40 MiB, vollständige ELF).
  Die höchste Version gewinnt; eine Datei ohne Version im Namen gilt als älter als die eingebaute.
- Dump Forge legt keine eigene Kachel an (die App hält `/user/app/PDFG00001` mit einer leeren Datei besetzt).

### Wenn sich das Laufwerk abmeldet

Ein USB-Laufwerk an einem schlechten Kabel, wackligen Anschluss oder mit zu wenig Strom kann mitten im Schreiben vom Bus fallen;
jeder Zugriff scheitert dann mit „Device not configured“. Die Meldung rät dann: anderes, kurzes Kabel direkt an der Konsole,
bei Störungen die Konsole neu starten. Typisch: Die Anzeige bleibt stehen, nach etwa einer Minute scheitert der Auftrag, im
Kernel-Log steht `umass0: USB_ERR_TIMEOUT` und das Laufwerk meldet sich ab und wieder an.

**Reste.** Nach einem Absturz oder Stromausfall kann eine `….ps5cc-konv-teil`-Datei liegen bleiben. Ein neuer Auftrag mit
demselben Ziel ersetzt sie; außerdem räumt die App vor der ersten Konvertierung nach jedem Start solche Reste auf, die älter
als eine Stunde sind (in `homebrew`, `Kraken-Games`, `PS5-Sicherung/Spiele` und im alten Ordner `FPKG-Games` jedes
Laufwerks). Anderes löscht man im Dateimanager.

**Außerdem:** Während einer Kopie oder Konvertierung hält die App den automatischen Ruhemodus zurück (ein selbst gewählter
Ruhemodus bricht den Auftrag ab, er räumt auf). Liegen Quelle und Ziel auf verschiedenen Laufwerken, wird gleichzeitig
gelesen und geschrieben (Protokoll `game_copy_io`, `game_convert_io`), auf einem Laufwerk nacheinander; gelesen wird in
4-MiB-Stücken.

## AMPR-Index, APR-EMU und PlayGo

Jede Spielkarte hat unter **Konvertieren** den Knopf **LZ4-/zstd-Asset-Packs …** (bei einem Abbild **AMPR & APR-EMU …**).
Das Fenster bündelt den AMPR-Index, „APR-EMU im Spiel“, „PlayGo im Spiel“, die Spiel-Plugins, das Packen (nur bei
Dump-Ordnern), die Optimierung per Mitschnitt und bei Abbildern Prüfen, Entpacken und Reparieren.

### AMPR-Index

Der **AMPR-Index** (`ampr_emu.index`) ist die Liste aller Dateien des Spiels, die APR-EMU zum Finden braucht. Das Fenster
zeigt, ob es einen gibt und wie viele Dateien er kennt.

- **Bauen / Neu bauen**: bei einem Dump-Ordner direkt, bei einem Abbild hängt ShadowMountPlus es kurz zum Schreiben ein. Ein
  alter Index bleibt als `ampr_emu.index.bak`. Ein laufendes Spiel oder ein anderer Vorgang sperrt das Bauen, ebenso ein
  gepacktes Spiel.
- Beim Konvertieren eines Dump-Ordners kommt der Index mit **AMPR-Index vorher bauen** ins Abbild (bei Spielen mit APR-EMU
  angehakt); bei `.ffpfsc` ist das der einzige Weg.
- Aufbau nach `build_ampr_index.py` aus [ampr_emu](https://github.com/drakmor/ampr_emu) (drakmor, GPL-3.0-or-later), Byte für Byte
  verglichen: Kennung `AMPRIDX3`, Pfade klein geschrieben, sortiert, mit FNV-1a-Tabelle.

### APR-EMU im Spiel

Die Zeile zeigt, welche `libSceAmpr.sprx` ein Spiel lädt, und lässt eine Version einbauen, das Original zurückholen oder die
Datei entfernen. Die Versionen liegen in `/data/PS5-Cooling-Center/AMPR_EMU` (Namen wie bei drakmor,
`libSceAmpr.sprx-0.4.2.1-test-pack`). Mitgeliefert sind sieben Fassungen; sie stehen in der Auswahl, auch wenn sie noch nicht
im Ordner liegen:

- **AMPR EMU NG @quer3q** (liest LZ4- und zstd-Packs, Marke „NG“): NG 0.5.1, NG 0.5.1 debug, NG 0.5.0, NG 0.5.0 debug;
- **AMPR EMU @Drakmor**: 0.4.2.1 (ohne Paketunterstützung, `test-nopack`);
- **LZ4 Asset Pack Version**: `0.4.2.1 test-pack` und `test-debug-pack`.

„Datei hinzufügen“ legt weitere Fassungen in den Ordner (Dateien einer NG-Veröffentlichung, `libSceAmpr.sprx` und
`libSceAmpr-debug.sprx`, benennt es passend um). **Auf neue AMPR-EMU-NG-Fassung prüfen und laden** fragt GitHub nach der
neuesten **stabilen** Veröffentlichung (nie eine Beta), lädt beide Dateien, prüft sie gegen `SHA256SUMS` und die Kennung,
stellt sie oben in die Liste und lässt die älteste NG-Fassung entfallen (von der App abgelegte Dateien werden gelöscht,
eigene bleiben). Geladen wird nur auf Knopfdruck.

**Automatische Fakelib-Aktualisierung von ShadowMountPlus.** Ist sie an (`update_emulators=1`, ab 1.7beta12
`update_enabled=1` unter `[emulators]`), ersetzt ShadowMountPlus die Datei beim Spielstart aus `/data/shadowmount/emus`.
Darum **fragt die App beim Einbauen von NG**: **Ja** setzt nur diese Zeile in `/data/shadowmount/config.ini` auf 0 (einmal
gesichert als `config.ini.ps5cc-bak`) und baut ein, **Abbrechen** lässt alles. Dieselbe Einstellung steht als Schalter
**Fakelib automatisch aktualisieren** in der Karte **ShadowMountPlus** auf der Seite System (Vollbild-App: System →
ShadowMountPlus). Die App versteht das alte und das neue Format der `config.ini` (ab 1.7beta12 Abschnitte, `[config]
version = 2`) und lässt Kommentare und alles andere unverändert; welches gilt, erkennt sie an der Datei (**Automatisch
erkennen**), festlegen lässt es sich dort mit **Ab 1.7beta12** oder **Vor 1.7beta12**.

**Wohin die Datei kommt** (Suchreihenfolge von ShadowMountPlus, siehe „Die Seite Spiele“):

- **Spielordner, `.exfat`, `.ffpkg`:** `fakelib/libSceAmpr.sprx` im Spiel (ein Abbild wird kurz zum Schreiben eingehängt).
  Ein `fakelib2` daneben würde gewinnen; davor warnt das Fenster. Bei `.exfat`/`.ffpkg` steht unter „Wohin kommt die Datei?“
  **Im Abbild** (Vorgabe) oder **Im Backport-Ordner** neben dem Abbild (`<Ordner des Abbilds>/backports/<Titel-ID>/fakelib`;
  lässt das Abbild unberührt). Ist das Abbild voll, nimmt die App von selbst den Backport-Ordner.
- **`.ffpfsc`:** nicht beschreibbar, also immer ein Backport-Ordner; die übrigen Dateien der `fakelib` des Spiels werden
  hineinkopiert, weil der Ordner sie verdeckt. Ein vorhandener Backport-Ordner gilt für jedes Format.
- Die Datei des Spiels wird einmal als `libSceAmpr.sprx.orig` gesichert und nie überschrieben (ein früherer APR-EMU oder
  PlayGo-Stub gilt nicht als Original). Ein leer werdender Backport-Ordner wird entfernt.
- Ein Spiel in LZ4-Packs nimmt nur Versionen mit „pack“ oder NG, ein zstd-Spiel nur NG.

### PlayGo im Spiel und Spiel-Plugins

**PlayGo im Spiel** arbeitet genauso für `libScePlayGo.sprx`, den PlayGo-Stub von drakmor
([pgo_stub](https://github.com/drakmor/pgo_stub), GPL-3.0). Er meldet einem Spiel, dessen PlayGo-Inhalte fehlen, alles als
vollständig installiert, und hilft nur bei solchen Spielen. Mitgeliefert: `libScePlayGo.sprx-0.6-nolog` und `-0.6-log`
(schreibt `/app0/playlgo.log`), Ordner `/data/PS5-Cooling-Center/PlayGo`. Ort, Sicherung (`.orig`), Warnungen und Entfernen wie
bei APR-EMU; ein Stub in `/data/shadowmount/emus` gewinnt auch hier.

**Spiel-Plugins** hält die Ersatzmodule von drakmor im `fakelib` des Spiels aktuell: den PlayGo-Stub (pgo_stub 0.6) und die
DLC-Module von dlc_emu 0.3 (`libSceAppContent.sprx`, `libSceGameUpdate.sprx`, `libSceNpEntitlementAccess.sprx`, Fassung
„log“, schreibt `/app0/dlc_emu.log`), die DLC aus der `dlc_emu.ini` des Spiels als gekauft zeigen. Eine bekannte ältere
Fassung wird ersetzt (die alte bleibt als `.old`), eine unbekannte bleibt. Der PlayGo-Stub kommt nie in ein Spiel ohne Stub,
die DLC-Module nur, wenn eine `dlc_emu.ini` im Spielordner liegt. Fehlt einer Bibliothek das Ausführungsrecht, setzt
„Aktualisieren“ es. Bei `fakelib2` oder einem Backport-Ordner ändert die App nichts.

### Abbild prüfen, entpacken, reparieren

Für `.exfat`, `.ffpkg` und `.ffpfsc`; die App liest das Abbild selbst, ohne es einzuhängen.

- **Abbild prüfen:** Bei `.ffpfsc` zuerst der ganze Container (Kopf, Blocktabelle, jeder Block entpackt und gegen seine
  Prüfsumme), dann ob die Dateien dort liegen, wo das Dateisystem es sagt; bei `.exfat` und `.ffpkg` wird jede Datei ganz
  gelesen. Das Fenster nennt betroffene Dateien mit der Stelle.
- **In einen Ordner entpacken:** nach `homebrew/<Name des Abbilds>` auf einem Laufwerk deiner Wahl (FAT32 nimmt keine Datei
  über 4 GB). Geschrieben wird nach `….ps5cc-teil`, jede Datei per CRC-32 zurückgeprüft, erst dann umbenannt. Das Abbild
  bleibt (außer „Original danach“ sagt etwas anderes).
- **Reparieren:** nur, wo es sicher geht: Stimmt die Größe einer `.ffpfsc` nicht mit dem Kopf überein, die Daten sind aber
  vollständig und der Überhang leer, setzt die App die Größe richtig und prüft erneut. Gesperrt, solange ShadowMountPlus das
  Abbild eingehängt hat.

### Passt der Index in AMPR EMU NG?

AMPR EMU NG baut aus dem Pack-Index Tabellen, die in seinen Indexspeicher passen müssen; passt es nicht, liefert NG **keine**
gepackte Datei aus, und das Spiel startet nicht. Bei einem Spiel mit Packs zeigt das Fenster deshalb **Passt der Index in
AMPR EMU NG?** („passt“, „knapp“ = innerhalb von 2 MiB, „zu groß“) mit den geschätzten Tabellen in MiB (Vollbild-App:
„INDEX-TABELLEN (AMPR EMU NG)“; `index_check` in `GET /api/v1/library/ampr`). Gerechnet wird nach dem Modell von NG 0.4.1
bis 0.5.x: Die Ansicht aller Dateien der `ampr_emu.index` und die Paket-Tabellen müssen zusammen in 80 MiB passen (`src/indexlimit.c`).
Das ist eine Schätzung aus dem Quelltext von NG; verbindlich ist die Meldung von NG beim Start. Hilft nichts, das Spiel mit
weniger Dateien packen.

## LZ4- und zstd-Asset-Packs

Ein **Asset-Pack** besteht aus wenigen großen Dateien (`ampr_assets-NNN.pak` mit `ampr_assets.index` und
`ampr_assets.index.crc` bei LZ4, `ampr_zstd-NNN.pak` mit `ampr_zstd.index` bei zstd). Die App packt die Spieldaten hinein;
die APR-EMU liest sie beim Spielen und entpackt jeden Block erst, wenn das Spiel ihn braucht. Das spart Platz und lädt oft
schneller, aber **nicht jedes Spiel verträgt das**. Ein Spiel liegt in LZ4- oder in zstd-Packs, nie in beiden.

### Was nötig ist

- Das Spiel liegt als **Ordner** vor (ein Abbild vorher mit „In einen Ordner entpacken“).
- Es **lädt libSceAmpr selbst** (die App sucht in `eboot.bin` und `sce_module`): Bei „✗ lädt libSceAmpr nicht selbst“ ist
  Packen gesperrt; solche Spiele lassen sich nur als `.ffpfsc` komprimieren.
- ShadowMountPlus bindet die `fakelib` des Spiels ein (fakelib-Modus „Emulatoren“ oder „Voll“, nicht „Deaktiviert“); sonst
  warnt die Zeile „LZ4-Asset-Packs“, ebenso wenn `eboot.bin` nicht ausführbar ist.
- Im Spielordner liegen keine Verknüpfungen. Platz: Beim Packen im Spielordner etwa die Hälfte der Spieldaten zusätzlich, bis
  die Originale entfernt sind. Das Fenster schätzt die Dauer; nach jedem Packen über 20 s rechnet es mit dem Tempo dieser Konsole.

### Schritt für Schritt (LZ4)

1. **APR-EMU einbauen:** bei „APR-EMU im Spiel“ `0.4.2.1 test-pack` (nur Versionen mit „pack“ lesen Pakete) oder NG.
2. **Ungepackt testen:** Menü, ein Level, einen Spielstand laden. Läuft es so nicht, läuft es gepackt nie.
3. **Mit LZ4 packen …:** Stufe 9 (3 = LZ4 Fast, 12 = am kleinsten) und **Automatisch** oder ein Profil. **Im Spielordner
   packen** spart am meisten: Index neu bauen, packen, jede Datei Byte für Byte mit dem Original vergleichen, erst dann die
   Originale entfernen; bei einem Fehler bleibt alles wie vorher. **Als neuen Ordner `<Name>-lz4` daneben** lässt das Original
   unberührt, legt aber zwei Ordner mit derselben Titel-ID nebeneinander – das Original vorher aus dem Suchpfad nehmen.
4. **Pakete prüfen:** liest jeden Block, entpackt ihn und prüft gegen die CRC-Datei. Dabei richtet es: fehlende oder ersetzte
   Pack-APR-EMU, Dateien, die die PS5 auf der Platte sucht, aber nur im Paket liegen, und die Rechte im Spielordner (0777);
   alles unter **Repariert**.
5. **Starten und länger spielen**, auch Laden und Speichern.
6. **Läuft es nicht:** **LZ4 entpacken …** schreibt alle Dateien zurück, vergleicht und entfernt die Pakete.

### zstd

**Mit zstd packen …** schreibt das Format von quer3q (ampr-emu-ng), gelesen nur von **AMPR EMU NG**; die App legt NG beim
Packen selbst in `fakelib` (ist die Fakelib-Aktualisierung von ShadowMountPlus noch an, warnt das Ergebnis). Gepackt wird das
ganze Spiel außer Systemdateien (Programme, `.ini`-Dateien wie `dlc_emu.ini`, `sce_sys`, `sce_module`, `fakelib*`, leere
Dateien, die Dateien, die die PS5 auf der Platte sucht); kein Profil. Stufen 1, 5 (Vorgabe), 9, 19; jedes Stück von 256 KiB
ist ein eigener zstd-Frame mit Prüfsumme. zstd packt **immer im Spielordner**; die Originale gehen erst nach dem Vergleich
Byte für Byte. **zstd entpacken …** bringt alles zurück. Unsignierte Programmdateien ersetzt der Schalter „Programmdateien
als FSELF signieren“.

### Profile

- **Getestete Profile** (von danthespal, an der Konsole getestet): **ASTRO BOT**, **EA Sports UFC 6**, **Insomniac-Engine**
  (Marvel's Wolverine) und **Unity-Engine** (God of War Sons of Sparta). Passt eines über Titel-ID, Name oder Aufbau, zeigt die
  Zeile „✓ getestetes Profil“, und es ist vorgewählt; ein eigenes Profil für die Titel-ID geht vor.
- **Eigene Profile** (TOML wie drakmors `ampr_pack.py`) liegen in `/data/PS5-Cooling-Center/LZ4_Profile` oder `LZ4_Profile`
  auf einem USB-Laufwerk. Die letzte passende Regel gewinnt.
- **Immer lose** bleiben leere Dateien, die PlayGo-Marker (`cache_ps5/pgc_*_dummy_file`, `cache_ps5/playgo.pgm`), die
  Startdateien, die Ghost of Yōtei vor dem schwarzen Bildschirm braucht, und die `*.rmdtoc` von Remedy-Spielen (Alan Wake 2):
  Die PS5 sucht sie auf der Platte, wo die APR-EMU nicht hilft. Alles, was die App in einen Spielordner schreibt, bekommt 0777.
- **Automatisch** lässt außerdem lose: Programme und Bibliotheken, `sce_sys`, `sce_module`, `fakelib`, Spielstände, schon
  komprimierte Medien, Konfigurations- und Datendateien (`.json`, `.xml`, `.ini`, `.dat`, `.bin`, `.utoc`), Archive, eigene
  `.pak` des Spiels, die Ordner `trophy2`, `uds`, `_DUPLEX_`, `decrypted`, Film-Ordner, Wwise-Soundbanks, Dateien direkt im
  Spielordner und große, kaum packbare Dateien.

**Optimierung per Mitschnitt** – zuverlässiger als „Automatisch“:

1. **Mitschnitt starten:** Die App legt `0.4.2.1 test-debug-pack` in einen Ordner `fakelib2` (mit den übrigen Bibliotheken
   aus `fakelib`). Voraussetzung: Das Spiel lädt libSceAmpr, der Index ist gebaut, es ist nicht gepackt.
2. **Spielen** und normal beenden; das Spiel schreibt `ampr_commands.bin` (die Debug-Version ist langsamer).
3. **Sitzung einsammeln** nach jeder Sitzung (der Mitschnitt wird bei jedem Start geleert) nach
   `/data/PS5-Cooling-Center/traces/<Titel-ID>/<n>/`. Mehrere Sitzungen ergeben ein besseres Profil.
4. **Profil erzeugen:** `<Titel-ID>-mitschnitt.toml` in `LZ4_Profile`, mit Blockgröße, Anordnung und „heiß“ je gelesener
   Datei; nie gelesene Dateien bleiben lose.

Danach **Mitschnitt beenden** (entfernt `fakelib2`).

### Wenn das Spiel nicht startet oder abstürzt

- **Erst klären, wer schuld ist:** entpacken und mit derselben APR-EMU-Version ungepackt starten. Stürzt es schon so ab,
  hilft nur eine andere APR-EMU (ältere Fassungen nimmt „Datei hinzufügen“) oder keine.
- **„mount flag / attribute error“** im Protokoll von ShadowMountPlus: Der Bibliothek fehlt das Ausführungsrecht. Die App legt
  sie mit 0755 an; selbst kopierte per FTP `SITE CHMOD 755`.
- **SIGBUS oder CE-108255-1 beim Start:** `eboot.bin` oder Module ohne Ausführungsrecht (typisch nach einer Kopie aus einem
  `.exfat`). **Pakete prüfen** setzt die Rechte auf 0777.
- **Absturz nach wenigen Sekunden, obwohl es ungepackt lief:** Das Spiel liest Dateien per **mmap** oder schreibend; diese
  müssen lose bleiben (eigenes Profil mit `loose`-Regel am Ende). Welche es nicht öffnen konnte, schreibt die Debug-Version als
  `apr.pack.open.fail` in `ampr_emu.log`.

**Grenzen:** Die APR-EMU lehnt Sätze mit mehr als 2 000 000 Dateien, 16 000 000 Blöcken oder 1 024 Paketdateien ab. Index,
`.runtime` und Pakete gehören zusammen (gleiche Build-ID): nie einzelne Teile tauschen und nichts ändern, während das Spiel
läuft. Solange ein Spiel gepackt ist, sind AMPR-Index und die Wahl der APR-EMU eingeschränkt. Vorbilder:
[drakmor/ampr_emu](https://github.com/drakmor/ampr_emu) (GPL-3.0-or-later), PS5-LZ4-Compression und
[danthespal/MkPFS.CSharp](https://github.com/danthespal/MkPFS.CSharp); von LZ4 Compression (Husam Osman, GPL-3.0) stammen
die Liste der losen Dateien, die Rechte 0777, die Reparatur beim Prüfen und die Auswahl der getesteten Profile (nachgebaut).

## PS4-Spiele von USB (PS4Mount)

Der Reiter **PS4 PKG von USB** spielt PS4-Spiele (Fake-PKG) direkt vom USB-Laufwerk, ohne sie zu installieren. Er beruht auf
PS4Mount 0.8.0 von gasb07 (GPL-3.0), dessen Quelltext in die App übernommen ist (`src/ps4m_*.c`).

1. **Vorbereiten:** Spiel, Update und Zusatzinhalte liegen als `.pkg` auf einem **exFAT**-Laufwerk, in beliebigen Ordnern und
   mit beliebigen Namen; zusammen gehört, was dieselbe Titel-ID hat. Die Pakete bleiben, wo sie liegen. Die Suche lässt sich in
   den Einstellungen auf bestimmte Ordner beschränken.
2. **Einschalten:** Ab Werk ist PS4Mount aus. „Einschalten“ startet die Suche, „Jetzt suchen“ löst sie sofort aus; die Wahl
   (an oder aus) bleibt über Neustarts gespeichert (`config.txt` in den Daten von PS4Mount).
3. **Warten:** Jedes Spiel wird gemeldet, sobald es spielbereit ist, und erscheint mit Symbol auf dem Startbildschirm. Bei
   mehreren Updates gilt das neueste („Updates“ wählt ein anderes); liegt dasselbe Spiel auf zwei Laufwerken, behält es das
   zuerst angemeldete.

- Auf die Konsole kommen nur kleine Verwaltungsdateien. Löscht man das Spiel an der Konsole oder mit „Kachel entfernen“,
  verschwindet nur die Kachel; „Kachel wieder anlegen“ holt sie zurück. Updates (`patch.pkg`) werden eingebunden,
  Zusatzinhalte, wenn der Schalter in den Einstellungen an ist.
- **Gefunden, nicht eingebunden:** Darunter stehen PKGs, die die Suche liegen lässt, mit Grund: schon installiert (erst das
  installierte Spiel löschen), noch nicht fertig kopiert, kein PS4-Paket (PS5-PKG installiert der Reiter „Pakete“), kein
  Fake-PKG, Zusatzinhalt ohne Spiel, zweite Kopie, beschädigtes Spielpaket (fehlerhafte Kennungen in der `param.sfo`; so ein
  Paket kann die Konsole beschädigen und wird nie angemeldet) oder **auf der M.2-Erweiterung** (`/mnt/ext1` wird durchsucht,
  aber nie angemeldet: Spiele von dort starteten an der Konsole nicht).
- **Keine zusammengeführten PKGs:** Spiel-PKGs mit eingebauten Patches oder Zusatzinhalten laufen womöglich nicht; besser das
  ursprüngliche Spiel-PKG und Update und Zusatzinhalte als eigene Dateien.
- **Laufwerk unter anderem Namen** (`usb1` statt `usb0` nach einem Neustart): Die Spiele bleiben angemeldet.
- **Neue Spiele schneller anmelden** (Einstellungen, ab Werk aus): Die Anmeldung der Konsole (`AppInstallAll`) geht sonst jeden
  Ordner in `/user/app` durch (etwa 0,75 s je PS4-Spiel). Mit dem Schalter sieht sie für höchstens zwei Minuten nur die neuen
  Spiele; in dieser Zeit lehnt die App Kachel-Aktionen mit „beschäftigt“ ab. Nur ohne laufendes Spiel.
- Die App muss laufen, damit die Spiele nach einem Neustart der Konsole sichtbar bleiben. Läuft PS4Mount schon als eigenes
  Payload (`ps4mount.elf`; erkannt an Port 8770, der Sperre auf Port 9032 oder dem Prozessnamen), zeigt der Reiter „wartet“ und
  übernimmt erst, wenn es beendet ist; die Seite Payloads startet keine zweite `ps4mount.elf`.
- **Firmware:** Ab 6.00 meldet die Konsole die Spiele auf dem üblichen Weg an, darunter einzeln; fehlt dort die nötige
  Funktion, werden zum Schutz der Datenbank keine Spiele hinzugefügt. Geprüft an einer PS5 Pro mit 12.00.

**PKGs zurückverschieben.** Ältere PS4Mount-Fassungen verschoben die PKGs nach `ps4/CUSAxxxxx` (Herkunft in `origin.txt`).
„PKGs zurückverschieben“ (je Spiel, „Ausgewählte“ oder „Alle“) legt sie mit ihren alten Namen zurück; das Spiel bleibt in der
Bibliothek. Ein laufendes Spiel wird vorher geschlossen; vorhandene Dateien werden nie ersetzt („ (1)“ am Namen), nichts wird
gelöscht.

**Hinweise:** Vor dem Ruhemodus PS4Mount-Spiele schließen – der Ruhemodus schaltet das Laufwerk ab, ein offenes Spiel hängt
danach. **Ausschalten** hängt alle Spiele aus (lehnt ab, solange eines läuft); Einschalten hängt sie wieder ein. Zwei oder mehr
Laufwerke (auch Partitionen) können die Oberfläche der Konsole träge machen. Je Laufwerk bis zu 768 Spiele und 64 Suchordner;
ein laufendes Spiel kann höchstens etwa 30 Pakete nutzen (Grenze der Konsole).

## Kraken Mount (Reiter „KrakenMount“)

Der Reiter **KrakenMount** spielt (F)PKG-Spiele, Spielordner (roh, zstd, LZ4) und Abbilder (`.exfat`, `.ffpkg`, `.ffpfs`,
`.ffpfsc`) direkt von der Platte oder vom USB-Laufwerk, ohne sie zu installieren. Dafür bringt die App **Kraken Mount** von
quer3q mit (GPL-3.0, Version 0.4.2; ein eigenes Programm), startet es und zeigt seine Spiele. Die Vollbild-App hat dieselbe Seite
unter Spiele → KrakenMount.

**Was die App an Kraken Mount ändert.** Im Original wartet Kraken Mount, bis ShadowMountPlus beendet ist. Damit beide
nebeneinander laufen, ändert die App in ihrer Fassung (eingebaut oder von GitHub geladen) den Namen des Prozesses, auf den es
wartet; das Warten auf kstuff bleibt. Außerdem schaltet sie die Selbst-Aktualisierung ab (neue Fassungen kommen über „Neue
Fassung von GitHub laden“) und verlegt seine Ersatz-Schnittstelle für ShadowMountPlus von Port 10101 auf 18097, weil die App
selbst über 10101 mit ShadowMountPlus spricht. Antwortet trotzdem Kraken Mount auf 10101 (eine selbst gestartete Fassung),
schickt die App ihm keine Aufträge für ShadowMountPlus (`smp_port_kraken`); eine selbst gesendete ELF wartet weiter auf
ShadowMountPlus.

- **Vorbereiten:** Spiele kommen in den Ordner **`Kraken-Games`** (direkt oder in einem Unterordner): intern
  `/data/Kraken-Games` (legt die App an), auf USB oder M.2 `Kraken-Games` im Hauptordner (selbst anlegen, oder Kopieren und
  Konvertieren mit „B · Spielbar mit Kraken Mount“ legen ihn an). Dort sucht ShadowMountPlus nicht. Backports kommen nach
  `<Suchordner>/backports/<TITEL-ID>*`. Ein älterer Ordner `FPKG-Games` wird beim nächsten Einschalten in `Kraken-Games`
  umbenannt, sofern die Suchordner noch die von der App angelegten sind.
- **Einschalten:** Ab Werk aus. „Einschalten“ sendet das Programm an den ELF-Lader; danach startet die App es bei jedem eigenen
  Start (nach etwa 5 s). Die Wahl steht in `kraken/zustand.txt` im Datenordner. Ausschalten wird abgelehnt, solange ein Spiel
  läuft oder Kraken Mount installiert. Läuft nach einem Update noch die ältere Fassung, sagt es die Seite (aus- und einschalten).
- **Liste:** Name, Titel-ID, Version, Größe, Laufwerk, Art (FPKG, Ordner, Abbild) und Zustand. **Neu aufbauen** baut den
  Zwischenspeicher eines Spiels neu (`/data/krakenmount/caches`). **Spiel löschen** fragt und löscht Datei bzw. Ordner, Kachel
  und Zwischenspeicher; Spielstände bleiben. **PlayGo-Stub ausschalten/einschalten** (nur bei Spielen, die PlayGo laden) hilft,
  wenn ein Spiel mit dem Stub auf schwarzem Bildschirm bleibt (`playgo_off`).
- **Einstellungen** (Suchordner, Fakelib, Zusatzmodule, Auswerfen je USB-Laufwerk, Installieren auf die Konsole) bleiben auf
  der eigenen Seite von Kraken Mount (Port 8097, Link im Reiter). Die Liste im Reiter zeigt nur seine eigenen Spiele, nie die
  auf der Konsole installierten und nie die Kacheln der App.
- **Fehlt die Quelle 5 Minuten** (abgezogenes Laufwerk), nimmt Kraken Mount das Spiel vom Startbildschirm; es kommt mit der
  Quelle zurück. Ein Spiel, das noch kopiert wird, wird übersprungen, bis sich seine Dateien 10 s nicht mehr ändern.
- **Abbilder und Auswerfen:** Kraken Mount hängt Abbilder schreibgeschützt über `/dev/lvdN` ein. Hält es ein Abbild von einem
  USB-Laufwerk, wirft die App dieses Laufwerk nicht aus (`usb_eject_held`): Die Einhängung bleibt auch nach dem Ausschalten von
  Kraken Mount stehen. Dann das Laufwerk zuerst auf der Seite von Kraken Mount aushängen.
- **Vorhängeschloss an der Kachel:** Trägt die `param.json` „upgradable“ als `applicationDrmType`, meldet Kraken Mount sie
  unverändert an (ShadowMountPlus schreibt „standard“). Starten lässt sich das Spiel trotzdem.
- **Derselbe Titel bei zwei Einhängern** (`src/mounters.c`): Liegt dieselbe Titel-ID bei Kraken Mount und ShadowMountPlus
  (oder PS4Mount), kommen sie sich in die Quere (`Device busy`, verschiedene Kacheln). Der Reiter warnt gelb; das Spiel soll
  nur bei einem liegen. Wird so ein Spiel beendet, hängt die App es bei ShadowMountPlus aus.
- Für große Spiele empfiehlt der Autor zstd-Packs statt einer `.pkg`. Nach einem Neustart der Konsole muss Kraken Mount laufen,
  damit die Spiele sichtbar bleiben.

### Ohne ShadowMountPlus: homebrew und etaHEN/games

Läuft kein ShadowMountPlus (zum Beispiel nach einem Neustart nicht geladen), ergänzt die App die Suchordner von Kraken Mount
**eine Minute später** um die Ordner von ShadowMountPlus: `homebrew` und `etaHEN/games` intern und auf jedem Laufwerk sowie den
Hauptordner jedes Laufwerks (Liste in `src/krakenmig.h`). Kraken Mount meldet die Spiele dort genauso an wie ShadowMountPlus:
Kacheln von ShadowMountPlus gelten weiter, auch die von Homebrew-Programmen in `/data/homebrew`, und eine `.pkg` im Hauptordner
eines Laufwerks spielt es ohne Installation.

- **Startet ShadowMountPlus**, nimmt die App die Ordner beim nächsten Blick (alle 5 s) wieder heraus; Kraken Mount gibt seine
  Einhängungen und Abbilder dieser Spiele selbst frei, ShadowMountPlus übernimmt die Kacheln (an der Konsole in etwa einer
  Sekunde). Eine `.pkg` aus dem Hauptordner behält ihre Kachel, startet aber erst wieder mit Kraken Mount.
- Kraken Mount muss nicht neu starten (die App schickt die Liste mit `POST /api/settings`). Jeder Start von Kraken Mount durch
  die App beginnt **ohne** diese Ordner, damit ein ShadowMountPlus aus dem Autostart Zeit hat. Eine von Hand geänderte
  Ordnerliste fasst die App nie an. Als laufend gilt ShadowMountPlus auch, wenn es in der letzten Minute auf seinem Port
  geantwortet hat.
- Solange Kraken Mount die Ordner hat, nimmt die Spieleliste Quelle und Format eines solchen Spiels von Kraken Mount (es lässt
  sich kopieren und konvertieren); „Original danach löschen“ und das Löschen laufen über Kraken Mount, Verschieben des Originals
  und „Deaktivieren“ eines von Kraken Mount eingehängten Abbilds werden abgelehnt.
- **Lämpchen** im Reiter (Browser und Vollbild, `smp_folders` in `GET /api/v1/kraken`): **grün pulsierend** = Kraken Mount
  hat die Ordner, **gelb blinkend** = Übernahme gleich oder Rückgabe läuft, **blau** = ShadowMountPlus hat sie, **grau** = von
  Hand geänderte Liste; bei der Grundeinstellung von Kraken Mount (leere Liste) sagt es die Zeile „Suchordner“. Protokoll
  `kraken_smp_folders`.

**Löschen in der Spieleliste.** Ein Spiel, das Kraken Mount angemeldet hat, löscht die App über Kraken Mount selbst
(aushängen, Daten samt Prüfsummen-Datei löschen, abmelden, Zwischenspeicher leeren); das Fenster nennt „Spiel von Kraken
Mount“ und die Datei. Ist Kraken Mount aus, beschäftigt (antwortet nicht in 5 s) oder führt es das Spiel gerade nicht, lehnt der
Plan mit Grund ab. Sehr lange Pfade werden nicht gekürzt, sondern auf den Reiter KrakenMount verwiesen.

## Disc im Laufwerk

Den Reiter **Disc** gibt es nur, wenn die Konsole ein Laufwerk hat (`has_drive` in `GET /api/v1/disc`; die App sucht nur das
Gerät, ohne dem Laufwerk einen Befehl zu schicken). Er beruht auf ps-disc-detect (GPL-3.0), neu geschrieben in `src/disc.c`.

- **Erkennung** (Schalter, ab Werk aus): fragt das Laufwerk alle anderthalb Sekunden. Profil wie bei der Konsole (PS2 DVD,
  PS3 DVD/BD, PS4 BD, PS5 BD; PS1/PS2-CDs kann das Laufwerk nicht lesen) und Titelkennung aus `SYSTEM.CNF`,
  `PS3_GAME/PARAM.SFO`, `bd/param.sfo` bzw. `bd/param.json`; gelesen wird die eingehängte Disc unter `/mnt/disc`.
- **Abbild (ISO):** nur von PS2-DVD und PS3-Disc. `/dev/cd0` wird in 128-KiB-Stücken in eine `.iso.part` gelesen, dabei
  CRC-32, MD5 und SHA-1; danach umbenannt. Ziel `/data/PS5-Cooling-Center/disc/` (Platz: Größe der Disc plus 512 MiB), daneben
  `<Name>.hash.json`. Ein vorhandenes Abbild gleicher Größe wird nicht neu gelesen. **Neue Disc automatisch sichern** ist ein
  zweiter Schalter (ab Werk aus).
- **Redump:** Eine `/data/redump.db` (mit `tools/build_redump_db.py` aus ps-disc-detect gebaut, bis 64 MB) liefert den Namen
  und prüft das fertige Abbild gegen Größe und Prüfsummen. CD-Abbilder und PS3-IRD werden nicht unterstützt.

**Disc-Spiele dumpen.** Die Liste zeigt die PS5- und PS4-Spiele, die von Disc installiert sind. Zum Dumpen muss das Spiel
laufen und die Disc im Laufwerk liegen: **„Dumpen starten“** startet das Spiel und dumpt es nach der Ladezeit von selbst (die
App muss nicht wieder geöffnet werden; die Konsole meldet „Dump gestartet“, „Dump fertig“ oder „Dump gescheitert“). Läuft
schon ein anderes Spiel, fehlt ein Ziel oder gibt es den Dump schon, startet der Knopf („Starten“) nur das Spiel.

- **Dump-Einstellungen:** Ziel (Laufwerk und Ordner, bis 3 Ebenen tief), Spielname im Ordnernamen, DLC mitdumpen, nach dem
  Schreiben prüfen, bei PS4 Spiel und Update getrennt/zusammen/einzeln. **Spielbar machen**: „Programmdateien entschlüsseln“
  und „Als FSELF signieren“ (beide nötig, damit der Dump startet und ShadowMountPlus ihn einbindet; das Entschlüsseln ist laut
  Oberfläche noch nicht an der Konsole erprobt). Unter „Für
  Fortgeschrittene“: **SDK herunterpatchen (Backport)** für ältere Firmware (kann den Dump beschädigen).
- **Dump-Warteschlange:** dumpt mehrere Spiele nacheinander (jedes wird gestartet, ein laufendes beendet; Ladezeit 5–600 s);
  fehlt eine Disc, wartet sie und erinnert alle zwei Minuten. Vorhandene Dumps: überspringen oder ersetzen. Sie läuft auf der
  Konsole weiter, auch bei geschlossener Seite.
- **Dumps aus dem Disc-Bereich** listet nur die hier entstandenen Dumps (USB, M.2, `homebrew` intern).

Nur für Sicherungen eigener Spiele. Schnittstelle: `GET`/`POST /api/v1/disc` ([API.md](API.md)).

## Spiele und Sicherungen löschen

Jede Spielkarte hat **Löschen**; der Reiter **Sicherungen** listet die Kopien und Konvertierungen der App (Ordner
`PS5-Sicherung/Spiele` intern und auf jedem Laufwerk, mit „unfertig“ und „mit Prüfsumme“), jede ebenfalls mit Löschen.

- **Installierte Spiele** (auch Fake-PKG): über die Deinstallation der Konsole wie im Menü „Löschen“, dazu Updates und
  Zusatzinhalte; die App wartet, bis das Spiel aus Bibliothek und SSD verschwunden ist.
- **Spiele von ShadowMountPlus:** Zuerst meldet ShadowMountPlus das Spiel ab (sonst würde es die Datei wieder einbinden), dann
  löscht die App Abbild bzw. Ordner und eine `.sha256` daneben.
- **Spiele von Kraken Mount:** über Kraken Mount (siehe dort).
- **PS4-Spiele von PS4Mount:** PS4Mount hängt aus, die Konsole deinstalliert; die PKG-Dateien auf dem USB-Laufwerk bleiben.
- **Sicherungen:** Datei bzw. Ordner und die Prüfsummen-Datei.

**Die Spielstände bleiben immer erhalten.** Ein Klick holt nur den Plan; das Fenster nennt Name, Art, Ort und Größe, erst
„Löschen“ darin löscht („Abbrechen“ hat den Fokus). Die Konsole löscht nur mit dem Kennwort zum Plan (einmal, zehn Minuten,
nur für genau dieses Ziel). Der Fortschritt zählt gelöschte Bytes (große Dateien werden in 512-MB-Schritten von hinten gekürzt)
bzw. bei installierten Spielen, wie weit die Konsole die Ordner geleert hat.

**Nicht gelöscht wird:** ein laufendes (auch pausiertes) Spiel; irgendetwas, solange ein anderer Vorgang der App läuft; ein
über eine Verknüpfung eingebundenes Spiel, das ShadowMountPlus nicht führt; alles außerhalb der Laufwerke und `/data`, ein
Laufwerk selbst und der Ordner der App. Verknüpfungen werden nie verfolgt. Protokoll: `game_delete_start`,
`game_delete_done`, `game_delete_failed`, bei installierten Spielen `pkg_uninstall_called`.

## Spielzeit

Der Reiter **Spielzeit** zeigt, wann gespielt wurde, wie lange und wie warm die Konsole dabei wurde.

- **Woher:** Die App sieht jede Sekunde nach, welches Spiel läuft und ob es vorn ist (Spielerkennung unter *System →
  Zusatzabfragen* muss an sein, sonst steht ein Hinweis da). Gezählt wird nur, solange das Spiel vorn ist. Das ist nicht die
  Spielzeit auf den Karten der Bibliothek: Die zählt die Konsole selbst, von Anfang an.
- **Sitzung:** vom Start bis zum Beenden, auch mit Pausen. Sie endet, wenn das Spiel 45 s nicht mehr da war, ein anderes
  startet oder die Konsole schlief; als Ende gilt der letzte Augenblick, in dem es vorn war. Sitzungen mit weniger als 30 s
  vorn werden nicht gespeichert.
- **Anzeige:** das laufende Spiel, Summen für *Heute*, *7* und *30 Tage* und *Insgesamt*, die letzten 14 Tage als Balken,
  *Meistgespielt* (mit Anteil und höchster Temperatur) und die letzten Sitzungen mit CPU- und SoC-Höchstwert und mittlerer
  Lüfterdrehzahl. Marken: **Notfallmodus**/**Warnung** (die Regelung kam in diesen Zustand) und **Ende geschätzt** (App oder
  Konsole endete während des Spiels; als Ende gilt der letzte Zwischenstand, alle 30 s).
- **Tage und Uhr:** Die Zuordnung zu Tagen folgt der Ortszeit des Browsers (über Mitternacht anteilig auf beide Tage), „jetzt“
  der Uhr der Konsole; weicht sie mehr als zehn Minuten vom Gerät ab, steht ein Hinweis da.
- **Dateien:** beendete Sitzungen in `/data/PS5-Cooling-Center/sessions.csv` (die letzten 3000), die laufende in
  `session-open.csv`. Wird die App während eines Spiels ersetzt, führt die neue die Sitzung fort, wenn das keine drei Minuten
  dauert; sonst schließt der nächste Start sie am letzten bekannten Zeitpunkt ab.
- **Als Tabelle speichern** legt `ps5-spielzeit.csv` ab (Semikolon, Ortszeit, für Excel und Calc). **Verlauf löschen** fragt
  zuerst; ein laufendes Spiel zählt weiter.

## Spielstände sichern und zurückspielen

Der Reiter **Spielstände** kopiert die Spielstände der Konsole auf einen USB-Stick, eine Platte oder in den Speicher der
Konsole, spielt einzelne Titel zurück und überträgt Spielstände als ZIP auf eine andere Konsole. Spiele und Trophäen gehören
nicht dazu. **Apps** (YouTube und andere) stehen nicht in der Liste und werden nie gesichert, zurückgespielt, gelöscht oder
eingespielt (die Schnittstelle antwortet 403). Als App gilt, was die Konsole so beschreibt: bei PS5 `applicationCategoryType`
ungleich 0, bei PS4 eine `CATEGORY`, die nicht mit „g“ beginnt, und die System-Anwendungen `NPXS…`.

**Was kopiert wird.** Die Dateien der Spielstände so, wie sie sind (verschlüsselt, unverändert): PS4 (`savedata`), PS5
(`savedata_prospero` mit der Sicherungsdatei der Konsole) und die Begleitdateien (Bild, Beschreibung). Dazu die
Spielstand-Datenbank der gewählten Benutzer (`savedata.db`, `game_setting.dat`) – nur aufbewahrt, nie zurückgespielt. Die Liste
steht je Benutzer (bis 16) getrennt nach PS5- und PS4-Spielen, mit Name, Größe und Datum.

**Sichern.** Titel wählen (alle vorgewählt), Ziel wählen (vorgewählt ein Stick oder eine Platte, sonst die Konsole) und
„Spielstände sichern“. Die Sicherung liegt in `PS5-Sicherung/Spielstaende/<Datum_Uhrzeit>` (UTC im Namen, Ortszeit in der
Anzeige), mit denselben strengen Rechten wie auf der Konsole. Auf dem Ziel müssen 64 MB mehr frei bleiben, als sie braucht.

- **Geprüft wird jede Datei:** nach dem Schreiben zurückgelesen; die SHA-256 kommt in `manifest.json` und
  `pruefsummen.sha256` (am PC: `sha256sum -c pruefsummen.sha256`). Ein Fehler bricht ab und räumt auf; solange geschrieben
  wird, trägt der Ordner die Marke `.ps5cc-unfertig`. Eine Sicherung zählt nur, wenn sich beim Sichern keine Quelldatei geändert
  hat und kein Spiel gestartet wurde.
- **Läuft ein Spiel** (auch pausiert), wird weder gesichert noch zurückgespielt; „Prüfen“ geht trotzdem. Es läuft immer nur ein
  Vorgang, auch nicht neben Kopieren, Konvertieren oder Verschieben.
- **Prüfen** liest jede Datei einer Sicherung neu und vergleicht sie mit der Liste.
- Die Seite zeigt die 40 neuesten Sicherungen; unfertige werden gezählt, aber nicht gezeigt. Die App löscht keine Sicherung:
  Das geht am PC, per FTP oder im Dateimanager.

**Zurückspielen** (unter „Titel anzeigen“, beim ersten Klick):

1. Die Sicherung wird ganz geprüft; ist eine Datei des Titels beschädigt, wird nichts geändert.
2. Der jetzige Stand wird als eigene Sicherung abgelegt (`…_vor-Zurueckspielen_<Titel-ID>`), die sich ebenso zurückspielen lässt.
3. Platzprüfung auf der Konsole (Größe plus 64 MB); Reste eines abgebrochenen Zurückspielens (`.ps5cc-neu`) werden entfernt.
4. **Alle** neuen Dateien werden neben die alten geschrieben (`.ps5cc-neu`) und geprüft – bis hier ist nichts ersetzt.
5. Unmittelbar davor sieht die App noch einmal nach, ob ein Spiel gestartet wurde.
6. Die neuen Dateien werden in einem Zug umbenannt (Modus und Besitzer der alten); ab hier verschwindet „Abbrechen“.
7. Zum Schluss wird alles gelesen und mit der Sicherung verglichen.

Dateien, die nur auf der Konsole liegen, bleiben unberührt; die Datenbank wird nie zurückgespielt. **Nur zur selben Konsole:**
Spielstände sind an ihre Konsole gebunden; eine fremde Sicherung trägt die Marke *andere Konsole* und lässt sich nur prüfen.

**Löschen** je Titel: Das Fenster nennt den Titel und das Laufwerk, auf das vorher gesichert wird („Stand vor dem Löschen“;
ohne Laufwerk wird ohne Sicherung gelöscht, ein Hinweis sagt es). Dann werden die Spielstand-Ordner dieses Titels für diesen
Benutzer entfernt; `savedata.db` bleibt unberührt. Nur ohne laufendes Spiel.

### Auf eine andere Konsole übertragen (ZIP)

**Als ZIP** (bei jedem Titel) liest alle Spielstände des Titels aus, eine ZIP-Datei je Spielstand
(`<Titel-ID>_<Spielstand>_<Zeit>.zip` in `/data/PS5-Cooling-Center/savexport`). Die Konsole hängt dafür eine **Kopie** des
Abbilds ein und liest es mit eigenen Mitteln im Klartext; die App entschlüsselt nichts selbst, das Original wird nie eingehängt.
Die ZIP-Datei (gespeichert, nicht komprimiert) enthält die Dateien, das echte `sce_sys/param.sfo` und bei PS5 `chives.json`
(Größe und Aufbau des Abbilds, Besitzer und Rechte jeder Datei). Die Karte **Auf eine andere Konsole übertragen** listet die
Dateien (die neuesten 60) zum Herunterladen und Löschen.

- Die ZIP-Datei ist **nicht verschlüsselt**. `ACCOUNT_ID` steht darin als Null; die übrigen Besitzerangaben (`PARAMS`) bleiben,
  weil das Einspielen sie braucht – nur weitergeben, wem man das zutraut.
- Nur ohne laufendes Spiel; frei sein müssen etwa die doppelte Größe des Spielstands plus 64 MB; höchstens 5000 Dateien und
  Ordner, keine Datei ab 4 GiB, höchstens 16 Spielstände je Titel.

**Spielstand einspielen** (auf der anderen Konsole): ZIP-Datei(en) wählen (von dieser App oder von Garlic SaveMgr, bei PS5 mit
`chives.json`). Sie gehen nach `/data/PS5-Cooling-Center/savexport/import`; die App zeigt Titel, Dateien und Größe, **ohne
etwas zu ändern**. Dann den Benutzer wählen:

1. Vorhandene Spielstände dieses Titels werden als „Stand vor dem Einspielen“ auf ein Laufwerk gesichert (ohne Laufwerk kein
   Einspielen).
2. Die Konsole legt mit ihrem eigenen Formatierer ein **neues Abbild** an (Größe und Aufbau aus `chives.json`); die App
   schreibt die Dateien hinein und trägt im `param.sfo` Konto (`ACCOUNT_ID`) und Benutzer-Kennung dieser Konsole ein.
3. Das Abbild wird erneut eingehängt und **jede Datei zurückgelesen** und verglichen.
4. Erst dann ändert sich etwas: Das Abbild kommt nach
   `/user/home/<Benutzer>/savedata_prospero/<Titel>/sdimg_<Spielstand>` (ein vorhandenes bleibt bis zum Ende als
   `.ps5cc-alt`), das Bild in `savedata_prospero_meta`, und der Eintrag in der `savedata.db` wird angelegt, nachdem die
   Datenbank nach `/data/PS5-Cooling-Center/savexport/db-backup` kopiert wurde (je Benutzer bleiben die 10 neuesten Kopien).
   Scheitert etwas, geht alles auf den Stand davor zurück.

- **Mehrere ZIP-Dateien** müssen zum selben Titel gehören und verschiedene Spielstände enthalten; der Stand davor wird einmal
  für das ganze Spiel gesichert.
- **Unterbrochenes Einspielen:** Ab der ersten Umbenennung führt die App `savexport/import.journal`. Nach Stromausfall oder
  Absturz führt der nächste Start das Einspielen zu Ende oder nimmt es zurück.
- **Neueres Abbildformat:** Ein PS5-Spielstand mit `fs_ext` ungleich 0 wird als klassisches Abbild aus seinen Datenblöcken
  angelegt; ob das Spiel ihn annimmt, zeigt erst ein Start. Nicht unterstützt: andere neuere Abbildformate und ZIP-Dateien
  ohne `chives.json` (PS5) oder mit mehreren Spielständen; gepackte Einträge über 64 MB lehnt die App vorher ab.
- **PS4-Spielstände** (`CUSA…`, Ordner `savedata`): Der versiegelte Schlüssel liegt in `<Spielstand>.bin` neben dem Abbild
  (ohne ihn kein Auslesen); die ZIP-Datei hat kein `chives.json`. Beim Einspielen berechnet die App die Größe selbst, und die
  Konsole legt das Abbild mit einem **neuen, für sie versiegelten Schlüssel** an.
- **`PARAMS`** (Benutzer-Kennung und ein 32-Byte-Wert, gleich in allen Spielständen eines Benutzers) muss stimmen. Die App
  übernimmt ihn aus einem vorhandenen Spielstand desselben Benutzers an dieser Konsole (über eine eingehängte **Kopie**;
  bevorzugt gleicher Titel). Gibt es keinen, wird trotzdem eingespielt, und der Auftrag warnt, dass das Spiel ihn vielleicht
  nicht annimmt.
- **Vollbild-App:** Im Menü eines Spiels (Quadrat-Taste) schreibt „ZIP auslesen auf …“ die Dateien nach
  `PS5-Sicherung/Spielstand-ZIP` des gewählten Laufwerks; an der anderen Konsole listet „ZIP-DATEIEN“ diesen Ordner, die X-Taste
  prüft eine Datei, danach „Für <Benutzer> einspielen“ (bzw. „… alle N Spielstände einspielen“). Dateinamen nur aus
  Buchstaben, Ziffern, `_`, `-` und `.`.

Während ein eingehängter Spielstand gelesen oder geschrieben wird, regelt der Lüfter weiter.

## Pakete finden und aufteilen

Der Reiter **Pakete** zeigt die Spiel-Pakete (`.pkg`) auf USB-Laufwerken, Platten, einer Disc und im Konsolenspeicher, teilt
große auf und [installiert](#pakete-installieren) sie (nach dem Vorbild des PS5 PKG Managers von itsPLK, siehe
[Credits](../README.de.md#credits-und-danksagung)).

- **Wo gesucht wird:** auf jedem Laufwerk unter `/mnt` im Hauptordner (nur die Dateien direkt darin) und im Ordner `pkg`/`PKG`
  bis drei Ebenen tief; im Konsolenspeicher nur in `/data/pkg`. Verknüpfungen werden nicht verfolgt, Dateien unter 4 KB zählen
  nicht, höchstens 512 Pakete je Suche.
- **Was gelesen wird:** nur einige Kilobyte des Kopfes: Titel-ID, Name, Version, Content-ID, Art (Spiel, Update,
  Zusatzinhalt), PS4/PS5, Bild und bei PS5 das **SDK-Ziel** („SDK 5.xx“, nur wenn eindeutig lesbar). Ein Rückportier-Patch
  (Kopf `LIH`) und Pakete mit Patch-Inhaltsmarke gelten als Update. Es wird nichts geschrieben; Kaputtes wird übersprungen
  (`failed`). Unveränderte Pakete (Pfad, Größe, Zeit) werden bei „Neu suchen“ nicht erneut gelesen.
- **Geteilte Pakete** erscheinen als ein Eintrag („geteilt, vollständig“ oder „Teile: 2 von 3“); Teile dürfen auf anderen
  Laufwerken oder Discs liegen.
- **Aufteilen …** (ab 128 MB): Ziel und Teilgröße wählen – **FAT32-Stick** (4095 MB), **Blu-ray 25 GB** (23 GB), **Blu-ray
  50 GB** (46 GB) oder eigene Größe (ab 64 MB, höchstens 256 Teile). Die Teile heißen `<Name>.pkg.part1`, `.part2` … im Ordner
  `pkg` des Ziels: je ein 4096 Byte großer Kopf (Paket, Teilnummer, Lage, Titel, Content-ID, Version, im ersten Teil das Bild)
  und dahinter ein Stück des Pakets, wie es ist – dasselbe Format liest der PS5 PKG Manager.
- **Absicherung:** Das Paket wird nur gelesen. Jeder Teil entsteht als `….ps5cc-teil`, wird zurückgelesen (SHA-256 des
  Stücks) und bekommt seinen Namen erst, wenn **alle** Teile gut sind. Fehler, gezogener Stecker oder „Abbrechen“ entfernen das
  Geschriebene; vorhandene Teile gleichen Namens werden nicht überschrieben. Ein Vorgang zugleich, Ruhemodus währenddessen
  verhindert; die SHA-256 des Pakets steht in der Meldung am Ende.

## Pakete installieren

**„Installieren“** an einer Paketkarte startet sofort die Installation der Konsole selbst (Systembibliothek AppInstUtil, wie
im Menü); die App stellt das Paket nur bereit und zeigt den Fortschritt. Der Knopf fehlt bei unvollständig geteilten Paketen und
solange auf der Seite ein Vorgang läuft. Lehnt die Konsole ab, klappt unter der Karte ein **Plan** auf: Name, Art, Version,
PS4/PS5, Größe (bei geteilten Paketen samt Zahl der Teile), freier Platz und eine schon installierte Version; Rot verhindert,
Gelb warnt, „Jetzt installieren“ geht nur ohne Rot.

**Was verhindert:**

- *schon da:* ein Spiel, das die Konsole kennt (die App überschreibt nichts; zum Neuinstallieren erst an der Konsole löschen);
  ein Update ohne Spiel oder mit gleicher oder neuerer installierter Version; ein vorhandener Zusatzinhalt. Lässt sich die Liste
  der Konsole nicht lesen, wird ein **Spiel** aus Vorsicht nicht installiert (Update und Zusatzinhalt mit Warnung schon).
- *es läuft etwas:* das Spiel selbst (Zusatzinhalte gehen trotzdem) oder ein anderer Vorgang der App, der die Laufwerke braucht.
- *das Paket stimmt nicht:* seit der Suche verändert, ein Link, unbekannte Plattform oder Kennung; bei geteilten Paketen
  falsch beschriftete, fehlende, doppelte, komprimierte oder nicht lückenlos passende Teile (geprüft wird das Paket in den
  Teilen, nicht der Kopf).
- *Warnungen:* wenig oder unbekannter Platz (weniger als Paketgröße plus 1 GB), unbekannte Versionen.

**Wie das Paket zur Konsole kommt.** Die App öffnet für diese eine Installation einen Server nur auf der Konsole selbst
(`127.0.0.1:18851`), der genau diese Datei unter einem einmaligen Namen liefert (Bereichsanfragen, bis 16 zugleich; geteilte
Pakete als eine Datei, ohne Zusammensetzen). **Die Konsole muss mit einem Netzwerk verbunden sein** (LAN oder WLAN, kein
Internet nötig); sonst meldet sie `0x80B21121`.

**Das Hilfsprogramm.** Den Aufruf in die Systembibliothek macht ein kleines eingebautes Programm (`ps5cc-inst.elf`, über den
Payload-Lader, Port 9021, verbunden über `127.0.0.1:18853` mit einem Kennwort je Start). Hängt oder stirbt es, bleibt die App
unberührt; die Installation gehört nach dem Aufruf der Konsole. Antwortet das Hilfsprogramm 40 s nicht, übernimmt ein neues die
Fortschrittsfragen (bis zu sechsmal). Ist keines mehr zu bekommen, liefert die App das Paket trotzdem fertig aus. Seine Zeilen
stehen in `/data/PS5-Cooling-Center/pkginst-helper.log`.

**Fortschritt und Ende.** Die Karte „Paket wird installiert“ steht oben (auch nach Neuladen): Phase, Prozent, Bytes, Zustand
der Konsole (läuft, spielbar, abgeschlossen …), Versuch, Dauer, Restzeit. Danach prüft die App bis zu 90 s die Datenbank der
Konsole. Vier Ausgänge: **„Paket installiert“** (grün, in der Liste gefunden), **„Von der Konsole als fertig gemeldet“** (gelb,
nicht in der Liste), **„Paket geliefert, Ergebnis nicht bestätigt“** (gelb, kein Hilfsprogramm mehr) und **„Installieren
fehlgeschlagen“**. Nach einem Neustart der App steht „Installieren unterbrochen“; Näheres in der Download-Liste der Konsole.

- **Fehler:** Vorübergehende Codes (`0x80B2116F`, `0x80B2100D`, `0x80B2100E`) versucht die App bis zu dreimal neu (nach 2 und
  5 s). Andere stehen im Klartext mit Code (zum Beispiel `0x80A30002`: zu wenig Speicher). Drei Minuten ohne Fortschritt geben
  einen Hinweis, zwanzig Minuten beenden mit Fehler; ebenso eine veränderte Paketdatei oder ein abgezogenes Laufwerk.
- **Abbrechen** beendet Hilfsprogramm und Server. Was die Konsole schon angelegt hat, kann liegen bleiben; für ein großes Spiel
  reserviert sie gleich den ganzen Platz – freigeben in der Download-Liste der Konsole mit „Löschen“.
- **Was die App nie tut:** löschen, deinstallieren, überschreiben, ein Spiel starten oder das Paket verändern.
- **Protokoll:** `pkg_install_start`, `pkg_install_called`, `pkg_install_status`, `pkg_install_retry`,
  `pkg_install_helper_lost`/`_restarted`, `pkg_stream_*`, am Ende `pkg_install_done`, `pkg_install_unverified` oder
  `pkg_install_failed` mit `pkg_install_detail`.

### Pakete in der Warteschlange installieren

- **„In die Warteschlange“** an jeder Paketkarte; **„Alle … Pakete dieses Titels in die Warteschlange“** legt Spiel, Updates
  (ältere zuerst) und Zusatzinhalte in der richtigen Reihenfolge hinein. Die Karte **Warteschlange** zeigt je Paket den Zustand.
- Nichts läuft von allein: **„Starten“** setzt sie in Gang. Geprüft wird, wenn ein Paket dran ist (dieselben Regeln); was die
  Konsole ablehnt, wird **übersprungen**, mit Grund.
- **Ein Fehler hält an:** „Weiter“ macht beim nächsten Paket weiter, „Wiederholen“ versucht das fehlgeschlagene noch einmal.
  „Nach diesem Paket anhalten“, „Abbrechen“, „Entfernen“, „Erledigte entfernen“, „Alle entfernen“.
- Höchstens 64 Pakete, nur im Arbeitsspeicher (nach einem Neustart der App leer).

### Pakete direkt vom PC installieren

Die Karte **„Vom PC installieren“** installiert eine `.pkg` vom Rechner, an dem der Browser läuft, ohne sie vorher auf die
Konsole zu kopieren: Datei wählen oder auf die Karte ziehen, Plan lesen, „Jetzt installieren“.

- Die Seite schickt die Datei in 1-MB-Stücken, *während* die Konsole installiert; die App hält einen Ring von 64 Stücken im
  Speicher (das erste bleibt immer) und liefert, was die Konsole gerade anfordert. Es wird nichts auf der Konsole gespeichert.
- Geteilte Pakete gehen auf diesem Weg nicht. Die Seite muss offen bleiben (auch im Hintergrund-Tab); meldet sich der Browser
  40 s nicht oder fehlt ein gebrauchtes Stück zwei Minuten lang, bricht die Konsole mit einem Satz ab.
- Ein Vorgang zugleich; eine verwaiste Übertragung endet nach drei Minuten, ein offener Plan nach einer halben Stunde. Die
  Konsole braucht wie bei jeder Installation eine Netzwerkverbindung.

## Dateien: der Dateimanager

Die Seite **Dateimanager** zeigt oben die Orte (Interne SSD `/data`, jedes angeschlossene Laufwerk, NTFS-Laufwerke, „Ganzes
System“), darunter den Pfad zum Anklicken und die Liste (Ordner zuerst, mit Größe und Datum). USB-Laufwerke heißen wie auf der
Konsole: `usb0`, `usb1` …

- **Ansehen und Herunterladen gehen überall.** „Ansehen“ zeigt Bilder (PNG, JPG, GIF, WebP, BMP, ICO) und den Anfang von
  Textdateien (bis 256 KB) mit strenger Sicherheitskopfzeile; Gerätedateien werden nicht ausgeliefert. **Bearbeiten** öffnet
  Textdateien bis 64 KB im Editor (`POST /api/v1/files/write`).
- **Ändern geht überall**, auch in `/user` und im Ordner der App: **Neuer Ordner**, **Hochladen** (mehrere Dateien, am PC auch
  per Ziehen), **Ordner hochladen** (vorhandene Dateien werden übersprungen, nie überschrieben), **Umbenennen**, **Kopieren**,
  **Ausschneiden**, **Löschen**. Schreibgeschützte Bereiche der Konsole (`/system` u. a.) lehnen selbst ab.
- **Gesperrt** („Nur ansehen“) zum Schutz vor Unfällen: `/` selbst, `/mnt`, `/dev`, `/proc`; Verknüpfungen (nie verfolgt, liegt
  eine auf dem Weg, ist der ganze Pfad gesperrt); jeder Ordner direkt unter `/` und `/mnt` (auch ein leeres `/mnt/usb2`) und
  Ordner, unter denen etwas eingehängt ist; alles auf Gerätedateisystemen. Kopieren und Löschen gehen nie auf ein anderes
  Laufwerk über.

> **Vorsicht:** Unter `/system_data` und `/user` liegen Datenbanken und Einstellungen der Konsole. Wer dort das Falsche löscht
> oder verschiebt, riskiert eine Konsole, die nicht mehr startet.

**Ordnen und auswählen.** Sortieren nach Name, Größe oder Datum (der Browser merkt es sich); „Alle auswählen“ (ohne
Verknüpfungen und Sonderdateien); „Größe berechnen“ im Menü „⋯“ eines Ordners bzw. „Ordnergrößen“ für alle (höchstens etwa 8 s
je Ordner, sonst „mindestens“).

**Suchen.** Das Feld **Suchen** filtert den offenen Ordner sofort nach Namensteilen (ohne Groß-/Kleinschreibung, auch bei
Umlauten); mit `*` und `?` muss der ganze Name passen (`*.pkg`, `PPSA?????`). **Auch Unterordner** sucht im Ordner und allen
darunter (`/api/v1/files/search`): höchstens 2.000 Treffer und 1 Million geprüfte Einträge, ohne Verknüpfungen, ohne andere
Laufwerke, nicht in `/`. **Ordner öffnen** springt zum Treffer. **Spieltitel:** Ordner und Abbilder, deren Name mit einer
Titel-ID beginnt, zeigen den Titel des Spiels (aus `param.json`/`param.sfo` oder den Spielen der Konsole; Abbilder werden nicht
eingehängt); die Suche findet sie auch über den Titel.

**Kopieren und Verschieben.** Einträge ankreuzen (oder Menü „⋯“) und **Kopieren nach** / **Verschieben nach** wählen, im
Zielfenster den Ordner wählen und **Hierhin kopieren/verschieben**; oder über die Ablage (Kopieren/Ausschneiden, Zielordner,
**Hier einfügen**). Gibt es einen Namen schon, fragt ein Fenster: **Ersetzen** (Datei erst daneben geschrieben, dann
darübergelegt; Ordner zusammengeführt), **Beide behalten** (`Name (2)`), **Überspringen** oder Abbrechen. Die Anzeige nennt
Eintrag x von y, von/nach, Prozent, aktuelle Datei, Rate und Restzeit. **Abbrechen** entfernt den angefangenen Eintrag im Ziel.
Verschieben auf demselben Laufwerk ist ein Umbenennen, sonst Kopie und danach Löschen; der Platz wird vorher geprüft.

**Löschen fragt zuerst** und nennt Namen, Art, Größe und bei Ordnern Zahl und Umfang der Dateien (Plan
`POST /api/v1/files/delete/plan`, Kennwort einmal gültig, zehn Minuten, genau für diese Liste). Während Kopieren, Verschieben
oder Löschen läuft, startet kein zweiter solcher Auftrag, und der Dateimanager startet nicht neben anderen Vorgängen der App.

**USB-Laufwerk sicher auswerfen.** Neben jedem USB-Laufwerk (`/mnt/usb0`–`usb7`, `/mnt/ext0`) steht **Auswerfen**; im Browser
wechselt die Seite dafür auf System → Speicherplatz und wirft dort aus (Regeln siehe [System](#system)). Vollbild-App:
Quadrat-Taste auf dem Laufwerk unter „Orte“ oder △ → Mehr → **Sicher auswerfen**.

**Vollbild-App.** Dieselben Routen und Regeln; dazu ein **Auswahlmodus** (X setzt den Haken und geht weiter, O beendet), **Alle
Größen messen** (△ → Mehr), Bearbeiten von Textdateien zeilenweise mit der Bildschirmtastatur und die Suche über △ → **Suchen …**.
Hochladen und Herunterladen bleiben im Browser.

### NTFS-Laufwerke

NTFS hängt die Konsole nicht ein; der Dateimanager liest und beschreibt solche Laufwerke mit dem Treiber aus
[PS5-NTFS-BROWSER](https://github.com/abcde6543/PS5-NTFS-BROWSER) (abcde6543, v1.4.0, GPL-3.0-or-later; mit eigenen
Härtungen, `src/third_party/ntfsb/ORIGIN.md`), im Browser wie in der Vollbild-App. Solange der Dateimanager offen ist, sucht die
App höchstens alle 3 s nach Platten (`/dev/daN`), von denen die Konsole nichts eingehängt hat. Jede NTFS-Partition (ohne
Tabelle, MBR samt logischen, GPT) wird ein Ort `ntfs0` … `ntfs7` mit Pfaden `/ntfsN/...`.

- **Geht:** Liste, Ansehen, Herunterladen, Größe, Hochladen (auch Ordner), Neuer Ordner, Kopieren und Verschieben in beide
  Richtungen (auch NTFS zu NTFS) und Löschen. Einträge mit dem Windows-Merkmal „System“ sind schreibgeschützt.
- **Geht nicht:** Umbenennen, Bearbeiten und Ersetzen (das Konfliktfenster bietet nur „Beide behalten“ und „Überspringen“).
  Unter Windows verbotene Zeichen und Namen (`\ : * ? " < > |`, `CON`, `NUL` …) schreibt die App als `_`. Spiele lassen sich von
  NTFS weder starten noch einhängen.
- **Nur lesbar**, wenn das Laufwerk nicht sauber getrennt wurde (Dirty-Flag), das Windows-Journal offen ist, chkdsk nicht
  fertig ist, die Systemplatte eines Windows-PCs im Ruhezustand ist, NTFS älter als 3.x ist oder ein Schreibfehler auftrat.
  Die Seite nennt den Grund und die Schritte am Windows-PC („Nur lesbar · Grund“).
- **Auswerfen** (`POST /api/v1/files/eject`, ohne ShadowMountPlus): schreibt Offenes aus, schließt das Gerät und meldet
  „ntfs0 kann jetzt abgezogen werden.“; abgelehnt, solange die App das Laufwerk benutzt.

## Payloads

Die Seite **Payloads** zeigt, was gerade läuft (mit „Beenden“), was in `/data/PS5-Cooling-Center/payloads` liegt, und die
`.elf`-Dateien auf USB-Sticks (Hauptordner und Ordner `payloads`). **Starten** schickt die Datei an den Payload-Lader (elfldr,
Port 9021) – wie vom PC, nur ohne PC. Von einem Stick lässt sich eine Datei in den internen Ordner kopieren; dort lässt sie
sich löschen (mit Rückfrage).

- Gestartet wird nur, was auf `.elf` oder `.bin` endet, eine gewöhnliche Datei ist und mit dem Kopf einer 64-Bit-x86-64-ELF
  beginnt; die Seite nennt nie einen Pfad, nur Einträge aus der Liste der Konsole. Ein zweiter Klick während eines Starts wird
  abgewiesen. Startet ein Payload die App neu, findet die Seite sie von allein wieder.
- Der Ordner wird nur angelegt, wenn es ihn nicht gibt; ein vorhandener bleibt unberührt.

### Payloads herunterladen

Der Bereich **Payloads herunterladen** zeigt zwei Listen zusammen: die des Payload Managers von itsPLK
(`https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json`, täglich mit der neuesten Fassung jedes Payloads) und die von
strongt1me (`https://raw.githubusercontent.com/strongt1me/ps5-payload-source/main/payloads.json`). Beide werden zusammen
geladen und in `/data/PS5-Cooling-Center/payload-store/catalog.json` bzw. `catalog-strongt1me.json` gespeichert; ist eine nicht
erreichbar, bleibt ihre gespeicherte Kopie. Bei gleicher Kennung oder Datei gilt die Liste von itsPLK.

- **Liste laden** geht als Einziges ins Internet (beim Öffnen höchstens alle drei Stunden erneuert). Suche und Kategorien
  filtern, „Geladen“ und „Mit Update“ zeigen, was schon da ist.
- **Herunterladen** schreibt die Datei zuerst unter einem versteckten Namen und benennt sie erst um, wenn ihre SHA-256 zur
  Liste passt; sonst wird sie verworfen. Ein Download zugleich, mit Fortschritt und „Abbrechen“.
- **Aktualisieren** zeigt „alt → neu“ und ersetzt die alte Datei. Liegt im Ordner schon eine fremde Datei gleichen Namens,
  fragt die App zuerst und nennt sie.
- **Löschen** entfernt nur Dateien, die die App selbst geladen hat (`payload-store/installed.json`); eine seither veränderte
  Datei bleibt liegen.
- Verbunden wird nur per HTTPS mit GitHub, mit eigener Namensauflösung und festen Wurzelzertifikaten. Die Payloads stammen
  von ihren Autoren; die App prüft nur, dass die Datei die aus der Liste ist.

### Profile: Abläufe aus mehreren Payloads

Ein **Profil** ist eine benannte Abfolge aus Payloads des internen Ordners und Pausen (zum Beispiel kstuff, 3 s Pause, dann
ShadowMountPlus). „Ausführen“ startet sie Schritt für Schritt, „Anhalten“ nach dem laufenden Schritt.

- **Startprofil:** Mit „Als Startprofil“ führt die App das Profil nach jedem eigenen Start aus (etwa 25 s danach). Höchstens
  eines. Wurde der letzte Ablauf nicht beendet (ein Payload hat App oder Konsole neu gestartet), läuft das Startprofil diesmal
  **nicht**, und das Protokoll sagt es.
- **Bearbeiten:** Name, Einträge, Pausen (1 ms bis 10 Minuten), Reihenfolge; höchstens 24 Profile mit je 64 Einträgen.
  „Vom PC importieren“ lädt `.elf`-Dateien in den Ordner (nie überschreibend); „Profile sichern“/„Profile laden“ als Datei.
- Gespeichert in `/data/PS5-Cooling-Center/payload-profiles.json`. Die Idee stammt vom ps5-payload-manager von itsPLK
  (GPL-3.0), neu gebaut ohne dessen Code.

## Profil

- **Anzeigename** des angemeldeten Benutzers (höchstens 16 Zeichen); gilt sofort, im Startmenü manchmal erst nach einem
  Benutzerwechsel.
- **Profilbild:** eines der **30 eingebauten Bilder** (`web/avatars/`, kein PC und keine Datei nötig) oder ein eigenes Bild vom
  Gerät (Zuschneiden oder Einpassen). Der Browser rechnet es in die vier Größen um, die die PS5 erwartet; vor der ersten
  Übernahme wird das bisherige Bild gesichert, **Vorheriges zurückholen** macht die Änderung rückgängig.
- **Eigene Bilder in der Vollbild-App:** JPG oder PNG bis 16 MB in `/data/PS5-Cooling-Center/Profilbilder` (legt die App an)
  oder auf einem USB-Stick im Hauptordner bzw. in `Profilbilder`. Sie stehen vorn mit der Marke „EIGENES“; die Vollbild-App
  rechnet sie auf der Konsole um. X übernimmt, Quadrat holt das vorherige Bild zurück, Dreieck ändert den Namen.
- **Offline-Konto aktivieren:** setzt für einen vorhandenen Benutzer Konto-ID, Typ „np“ und Kennzeichen in der
  Systemregistrierung (wie OffAct); der Name bleibt. Braucht einen aktiven Jailbreak.
- **Fake-Anmeldung schreiben / zurücksetzen:** sendet die ELF np-fake-signin (earthonion) an den ELF-Lader; sie schreibt
  `auth.dat`, `config.dat` und Registrierungseinträge für den aktiven Benutzer (`/system_data/priv/home/<uid>/`).
- **Profile sichern und übertragen:** **Web-Profil** (Einstellungen dieses Browsers wie Expertenmodus und eigene Alarme, Format
  `ps5tm-web-profile-v1`) und **Spielprofile** (Zieltemperaturen je Spiel) als Datei exportieren und auf einem anderen Gerät
  oder einer anderen Konsole importieren.

## System

- **ShadowMountPlus** (oben): Version, Schalter **Fakelib automatisch aktualisieren** und die Wahl des Formats der
  `config.ini` (Automatisch erkennen, Ab 1.7beta12, Vor 1.7beta12; siehe „APR-EMU im Spiel“).
- **Konsole:** Modell, Firmware, Laufzeit und **Seriennummer** – ab Werk gekürzt (`••••8023`); **Vollständig** zeigt die ganze
  Nummer, **Gekürzt** schaltet zurück (Einstellung `serial_full`, gilt für Browser und Vollbild-App). Ist sie ganz eingeschaltet,
  kann jedes Gerät im Heimnetz sie abrufen.
- **Netzwerk** und **Bildschirm**, **Am Handy öffnen:** ein QR-Code mit der Adresse dieser Seite (nur die Adresse der Konsole,
  keine Zugangsdaten); das Handy muss im selben Netz sein. Encoder aus webhb von slopmaster33 (GPL-3.0).
- **Speicherplatz** mit **Auswerfen** für USB-Laufwerke (unten), **Kühlleistung über die Zeit**.
- **Zusatzabfragen**, einzeln zuschaltbar: Spielerkennung (nötig für Spielzeit und Profile pro Spiel), Netzwerk und
  Bildschirm, Controller-Akku, Risikotelemetrie (Last aller 16 CPUs, Takt, Stromschienen über riskantere Pfade),
  Laufwerksdetails und Bilder pro Sekunde; dazu die Voreinstellungen Sicher, Monitoring und Voll.
- **Hintergründe der Vollbild-App** – nur, wenn die Seite aus der Vollbild-App heraus geöffnet wurde (Options → „Weboberfläche
  im Browser öffnen“); dort lädt man eigene Hintergrundbilder hoch.

**Expertenansicht** (Schalter „Expertenmodus“ auf dieser Seite, ab Werk aus): Region, Sprache und Zeitzone als Rohcodes
(`SYSTEM_language`, `DATE_time_zone` …, weil die Zuordnung je Firmware variiert), Firmware-Interna, Auslastung je Kern,
Arbeitsspeicher, **Live-Status und Warnungen** (Ampel: Grün ohne Warnung, Gelb ab Ziel + 5 °C oder fehlendem Adapter, Rot bei
Warnflag oder Notfallmodus; mit Ampelgrund, letzten Zustandswechseln und Zählern für 2/5/10 Minuten), Diagnostik-Masteransicht,
Monitoring-Alarme, **Assistent und Integrationen** (Warn-Countdown 30–900 s, Prospero Lights = DualSense-Licht als
Temperaturampel, best effort; „Nur bestätigte Rohsensorwerte“; Konfiguration exportieren/importieren; Live-Protokoll),
Power-Telemetrie, Controller-Diagnose, **Grafikdaten** (einen eigenen GPU-Sensor und eine GPU-Auslastung meldet die PS5
nicht; auf der Pro erscheint SoC-Kanal 7 als Grafiktemperatur, statt der Auslastung die Grafikleistung) und **Rohsensoren
(SoC-Kanäle)** mit der letzten gültigen Messung je Kanal (`/api/v1/channels`) und als unbestätigt markierten Heuristiken.

### USB-Laufwerk auswerfen

Unter „Speicherplatz“ hat jedes USB-Laufwerk (`/mnt/usb0`–`usb7`, `/mnt/ext0`) den Knopf **Auswerfen** (Vollbild-App: System
→ Speicher, X-Taste). Ausgeworfen wird über ShadowMountPlus (ab 1.7beta10), aber nur, wenn

- kein Auftrag der App läuft (Kopieren, Konvertieren, Verschieben, Löschen, Dateimanager, Spielstände, Pakete, AMPR-Index,
  Disc-Abbild),
- PS4Mount keine Spiele von diesem Laufwerk spielt und Kraken Mount kein Abbild davon hält,
- die App selbst nichts darauf offen hat; andere Programme prüft sie, wo die Konsole die Abfrage zulässt (auf der Pro mit
  Firmware 12.00 nicht).

**Warum so vorsichtig:** Die Konsole trennt das Laufwerk mit einem erzwungenen Unmount und fragt niemanden; was dann noch darauf
liest, wird mitgerissen. Darum beansprucht die App das Laufwerk zuerst (nichts Neues liest mehr davon, laufende Leser haben bis
zu 4 s), prüft offene Dateien (`src/driveholders.c`; abfragbar mit `GET /api/v1/storage/holders?mount=/mnt/usb0`), ruft
`sync()` auf und schreibt einen Zustandsbericht (`usb_eject_state`) sofort in `app.log`; erst dann fragt sie ShadowMountPlus.
Danach meldet die Konsole „… ist ausgeworfen und kann abgezogen werden“ oder den Grund der Ablehnung. Erst dann abziehen;
manche Laufwerke meldet die Konsole nach einigen Sekunden von selbst wieder an.

- Der Auswerfen-Knopf auf der Seite von ShadowMountPlus kennt die Aufträge dieser App nicht: während eines Auftrags nur über
  diese App auswerfen.
- **Gesperrt nach einem Absturz:** Kurz vor der Anfrage schreibt die App `usb_eject_offen.txt` in den Datenordner und löscht
  sie 30 s nach dem Ende. Findet sie die Datei beim Start, bleibt Auswerfen gesperrt, bis man es mit „Auswerfen wieder
  erlauben“ (System → Speicherplatz oder Seite Dateien) freigibt. Wer ganz sicher gehen will, schaltet die Konsole aus und zieht
  das Laufwerk erst dann ab.

### Firmware

Die App ist mit dem PS5-Payload-SDK **v0.50** gebaut; dessen Startcode kennt jede Firmware **von 1.00 bis 14.22**. Eine
Firmware, die er nicht kennt, kommt nicht bis `main()`: Das Programm startet dann nicht und schreibt nichts ins Protokoll.
Getestet ist die **Firmware 12.00** auf einer PS5 Pro; Nutzer melden auch 4.xx bis 10.xx und 13.60. Ob Lüftersteuerung und
Sensoren auf anderen Ständen gleich arbeiten, ist nicht geprüft. Was die Konsole meldet, steht unter System („Firmware“) und im
Protokoll (`firmware`).

## Protokoll

Der erste Reiter **Ereignisse der App** zeigt, was die App gemessen, geregelt und gemeldet hat (Zähler für Einträge, Hinweise,
Fehler; als `.log` exportierbar; dauerhaft in `app.log`, siehe Überblick).

### Kernel-Log live

Der zweite Reiter zeigt die Meldungen der Konsole selbst, wie sie entstehen: Start und Ende von Spielen, Abstürze,
Fehlercodes und die Zeilen dieser App – ohne den Umweg über `klogsrv` und `nc` am PC (Port 3232).

- **Woher:** aus dem Meldungspuffer des Kernels (`kern.msgbuf`, ein Ring von rund einem halben Megabyte), im Sekundentakt,
  nur solange der Reiter offen und die Seite vorn ist. `klogsrv` bleibt unberührt.
- **Uhrzeit:** Die Kernelzeilen tragen keine Zeit; die Spalte zeigt, wann die App die Zeile zuerst sah (Uhr der Konsole).
- **Filter:** Wörter, die alle vorkommen müssen (ohne Groß-/Kleinschreibung), `-` schließt aus (`sce -fmem`). Schnellwahl
  *Alles*, *Ohne Rauschen*, *Diese App* (`[ps5tm.elf]`), *Spiele*, *Fehler*. Fehler rot, Warnungen gelb, die App grün.
- **Pause**, **Leeren** (nur das Fenster), **Mitlaufen** (hochscrollen schaltet es aus, ans Ende scrollen wieder an).
- **Speichern** legt die gefilterten Zeilen auf der Konsole ab: `/data/PS5-Cooling-Center/klog-live-log/klog-<Datum>_<Uhrzeit>.log`.
  **Herunterladen** lädt sie als `ps5-kernel-log.log` auf das Gerät des Browsers. **Aufnahme** lässt die **Konsole selbst**
  mitschreiben (`aufnahme-<Datum>_<Uhrzeit>.log`, auch bei geschlossener Seite, alle 5 s auf den Datenträger gebracht, mit den
  letzten 200 Zeilen als Rückblick, ohne Filter). Sie endet mit „Aufnahme beenden“, bei 64 MB, unter 128 MB freiem Platz oder bei
  einem Schreibfehler; sie beginnt nicht unter 512 MB frei.
- **Auf der Konsole gespeichert** listet die Dateien des Ordners zum Herunterladen; **Ordner leeren** löscht nach Rückfrage
  nur Dateien der App (`klog-….log`, `aufnahme-….log`), nie eine laufende Aufnahme.
- **Grenzen:** Die App hält 3000 Zeilen, die Seite 5000, das Fenster zeigt 1500. Schreibt die Konsole schneller, als
  abgefragt wird, steht eine Zeile „Lücke“ da.
- **Zeiten** in den Dateien folgen der Zeitzone des Browsers, der gespeichert oder die Aufnahme gestartet hat (in der ersten
  Zeile genannt; ohne Angabe UTC).
- **Vorsicht beim Weitergeben:** Das Kernel-Log nennt Konsolenname, Benutzerkennung, Titel und Pfade.

## Die Vollbild-App

Die Vollbild-App (Kachel `FAKE86921`, gebaut auf dem Oberflächen-Baukasten ps5-homebrew-ui, `tvapp/`) kann fast alles, was
die Weboberfläche kann; beide sprechen mit derselben laufenden App über `127.0.0.1:8086`.

- **Tasten:** Touchpad-Taste öffnet die Seitenleiste; L1/R1 vorheriger/nächster Bereich, L2/R2 Unterreiter, Steuerkreuz
  auswählen und Werte ändern, X starten/bestätigen, Kreis zurück, Quadrat Aktionen, Dreieck Details oder „Mehr“,
  Options-Taste Menü („Weboberfläche im Browser öffnen“, „Neu laden“, „Sprache / Language“, „App beenden“, Power-Optionen).
  Die Leiste unten zeigt die gerade gültigen Tasten.
- **Bereiche:** Kühlung (Übersicht, Einstellungen mit Warnungen, Lichtleiste, Spielprofilen, Nachtmodus, Selbsttest,
  Hintergrund-Pause; Reiter Lüfterkurve mit Regelaufzeichnung), Spiele (Bibliothek mit Aktionen – Kopieren, Konvertieren,
  Verschieben, Entpacken, Abbild prüfen, Löschen, Extras, Aus-/Einhängen, Spiel beenden –, Spielzeit, Spielstände, Pakete,
  PS4 PKG von USB, KrakenMount, Disc, Sicherungen, Aufträge), Dateimanager, Payloads (eigene, geladene, Store, Profile mit
  Editor), System (Konsole, ShadowMountPlus, Speicher, Kühlleistung, Zusatzabfragen, Experte), Profil (Name, Profilbild,
  Konto, Danke) und Protokoll (App-Protokoll, Kernel-Log live, Gespeichert).
- **Sprache:** wie die Systemsprache der Konsole (sonst Englisch); Options → „Sprache / Language“ wählt eine der sieben
  Sprachen. Die Texte kommen aus denselben Wörterbüchern wie im Browser.
- **Nur im Browser:** Hochladen vom PC, Herunterladen, Profile als Datei sichern/laden, Links öffnen. Unter System zeigt die
  Vollbild-App einen QR-Code mit der Adresse der Weboberfläche.
- **Eingaben** über die Bildschirmtastatur (mit ä ö ü ß). **Hintergrundbilder** je Bereich unter System → Speicher →
  Hintergrundbilder; eigene Bilder lädt man in der Weboberfläche hoch, die man aus der Vollbild-App öffnet.

## Kacheln `FAKE86921` (Vollbild) und `FAKE86922` (Browser)

Mit den Kacheln muss die ELF nicht mehr gesendet werden. `FAKE86921.zip` und/oder `FAKE86922.zip` aus der Veröffentlichung
entpacken und die Ordner nach `/data/homebrew/` kopieren (FTP oder Dateimanager); ShadowMountPlus legt dafür Kacheln im
Medien-Bereich an. Beide können nebeneinander liegen. (Ältere Fassungen hießen `PPSA86921`/`PPSA86922`; diese Ordner und
Kacheln löschen – ShadowMountPlus hielt eine offene PPSA-Kachel für ein laufendes Spiel.)

- **`FAKE86921`, die Vollbild-App:** startet die App, falls sie nicht läuft, und zeigt sie im Vollbild mit Controller-Bedienung.
- **`FAKE86922`, die Browser-Kachel:** startet die App, falls nötig (die `eboot.bin` schickt die eingebettete ELF an den Lader
  auf `127.0.0.1:9021`), wartet, bis die Oberfläche antwortet, und öffnet sie über `POST /api/v1/open-ui` im Browser der Konsole.
- Die `eboot.bin` ist ein nativer, signierter PS5-Titel (gebaut mit `tools/make_starter.sh`); die App steckt darin. Nach einem
  Update den Ordner aus der neuen Veröffentlichung kopieren (die Vollbild-App muss dafür geschlossen sein, sonst lehnt FTP ihre
  `eboot.bin` ab).
- **Eine ältere laufende App wird ersetzt:** Beim Antippen vergleicht die Kachel ihre Version (Marke `PS5CC-VERSION:<Version>:`
  in der ELF) mit der laufenden (`app_version` in `GET /api/v1/system`). Ist die laufende älter, tritt die eingebaute an ihre
  Stelle (die Kachel wartet bis 30 s); eine gleiche oder neuere bleibt. Läuft gerade ein **Auftrag**, wird nichts ersetzt
  („Eine neuere App liegt bereit“; nach dem Auftrag noch einmal öffnen).
- **Alte Seite, neue App:** Kommt eine neue ELF, während die Weboberfläche offen ist, steht oben „Neue Version … läuft“ mit
  **Seite neu laden** (selbst lädt sie nicht neu, eine Eingabe ginge verloren). Ist die ELF in der Vollbild-Kachel älter als die
  laufende App, zeigt die Vollbild-App das gelb neben der Version und bittet einmal, die Kachel neu zu kopieren.
- **Nur die ELF, keine Kachel:** Liegt keiner der beiden Ordner in `/data/homebrew` und gibt es keine Kachel, legt die App etwa
  eine Minute nach dem Start einmal selbst eine einfache Medien-Kachel `PSCC69690` an, die nur die Weboberfläche öffnet
  (Notiz `tile-auto` im Datenordner; die App muss trotzdem nach jedem Neustart der Konsole gesendet werden).

## Tasten am Controller im Browser

Im Browser der Konsole wechseln **L1** und **R1** zur vorherigen und nächsten Seite, **L2** und **R2** rollen die Seite um
knapp eine Bildschirmhöhe. Der Browser meldet diese Tasten als F5 bis F8; die Seite fängt sie ab, ein Neuladen lösen sie nicht
aus. Die Gamepad-Schnittstelle des Browsers liefert auf der PS5 nichts.

## Bildschirmanzeige auf der Konsole

Beim Start meldet sich die App oben links (einzeilig, ohne Ton) mit Version, IP-Adresse und Port. Weitere Meldungen kommen bei
Warnungen und Fehlern sowie am Ende längerer Aufträge.

**Anzeige per Mikrofon-Taste** (Schalter auf der Kühlungsseite): Die Mikrofon-Taste **zweimal kurz hintereinander** zeigt
oben links Prozessortemperatur und Lüfterdrehzahl, auch ohne offene Oberfläche (höchstens alle 5 s). Das Mikrofon ist danach
wie vorher. Ein Payload darf den Controller nicht lesen; die App wertet die Kernelzeile `MicMuteKeyPressed` aus (ab Firmware
10.01), auf älterer Firmware `onPostUmmStatusChanged`, die auch bei Headset- oder Party-Wechseln kommen kann.

**Spiel im Hintergrund pausieren** (Schalter auf der Kühlungsseite, ab Werk an; Vollbild-App: Kühlung → Einstellungen): Geht
ein Spiel in den Hintergrund und ist der Startbildschirm vorn, pausiert die App es selbst. Das verhindert die Fehlermeldung
„Device busy“ beim Antippen einer Kachel der App (die Konsole scheiterte sonst beim Pausieren, und ShadowMountPlus gab in dieser
Zeit die Einhängung der Kachel frei). Nur für echte Spiele (PPSA/CUSA, nie eine Kachel der App oder eine Medien-App), nur wenn
der Startbildschirm selbst vorn ist, einmal je Aufenthalt im Hintergrund und nur, solange ShadowMountPlus antwortet; ein kleines
Hilfsprogramm (`src/helper/bgpause_helper.c`) ruft dafür die Konsole auf. Protokoll `bg_pause`, `bg_pause_skip`,
`bg_pause_failed`.

Ein dauerhaft eingeblendetes Overlay wie bei etaHEN ist von außen nicht möglich (dafür müsste die App den Renderer des Spiels
hooken); die Systembenachrichtigung erscheint zuverlässig über Spielen und Menüs.

## Zugriff und Sicherheit im Heimnetz

Die Weboberfläche ist im Heimnetz offen erreichbar – jedes Gerät im selben Netz kann lesen **und** ändern. Das ist bewusst so:
Die Konsole steht im eigenen LAN, und eine Kennwortabfrage brachte dort nur eine Hürde. Wer das anders braucht, setzt die
**Bind-Adresse** (`bind_address`, Kühlung → Expertenansicht → Erweiterte Einstellungen) auf `127.0.0.1`; dann antwortet der Server nur der Konsole selbst.

ShadowMountPlus nimmt ab 1.7beta8 ebenfalls Anfragen aus dem ganzen Heimnetz an (Port 10101, ohne Anmeldung); wer das nicht
möchte, setzt in dessen `config.ini` `api_bind_address=127.0.0.1` (die App spricht es ohnehin über `127.0.0.1` an).
**Kraken Mount** (Port 8097, nur eingeschaltet) und **Dump Forge** (Port 8095, nur während eines Auftrags) sind Programme anderer
Autoren; sie lauschen im ganzen Heimnetz und prüfen weder `Host` noch `Origin`. Wer das nicht will, lässt Kraken Mount aus.

Offen heißt nicht „für jede Webseite“: Eine fremde Seite im Browser eines Geräts im Heimnetz könnte dessen Verbindungen zur
Konsole mitbenutzen. Der Server weist deshalb ab, was erkennbar von einer fremden Webseite kommt:

- Der `Host` muss eine IP-Adresse, `localhost` oder ein Name mit einer Heimnetz-Endung sein, die es im öffentlichen DNS nicht
  gibt (`.local`, `.lan`, `.home`, `.home.arpa`, `.internal`, `.localdomain`, `.intranet`) – Schutz vor DNS-Rebinding. Namen
  ohne Punkt und `.fritz.box` gelten nicht; dann die IP-Adresse der Konsole nehmen (sonst Fehler 403 mit Hinweis).
- Fragt der Browser im Namen einer fremden Seite an (`Sec-Fetch-Site` `cross-site` oder `same-site`), wird jede API-Anfrage
  abgelehnt; bei allem außer Lesen muss ein mitgeschickter `Origin` dieselbe Adresse nennen. Werkzeuge ohne `Origin` (curl,
  Skripte, die Kacheln) funktionieren normal.
- Höchstens 24 gleichzeitige Verbindungen (ein Browser braucht etwa sechs); wer Kopfzeilen nicht in 15 s bzw. den Inhalt nicht
  in 20 s schickt, wird getrennt. Die Seite kommt mit einer Content-Security-Policy (kein Skript von außen, kein Einbetten).

## Weboberfläche nach einem Netzwerkwechsel

Wird das WLAN aus- und eingeschaltet oder das Kabel gezogen, beendet die Konsole mitunter den Zugang der Weboberfläche, obwohl
App und Lüftersteuerung weiterlaufen. Die App versucht deshalb nach zehn Sekunden Leerlauf eine Verbindung zur eigenen Adresse;
wird sie abgelehnt, öffnet sie einen neuen Zugang (Protokoll `http_listener_refused`).

## Sprache der Oberfläche

Oben rechts: **Auto (System), Deutsch, English, Italiano, Español, Français, Русский, Svenska**. **Auto** (ab Werk) übernimmt die
Sprache des Browsers – im Browser der PS5 die der Konsole –, sonst Englisch. Die eigene Wahl speichert der Browser
(`localStorage`, Schlüssel `lang`); Zahlen, Datum und Uhrzeit folgen ihr. Die Quelltexte sind deutsch; jede andere Sprache lädt
ein Wörterbuch (`/lang/<xx>.json`). Seltene Meldungen der Konsole können deutsch bleiben, ebenso die Texte, die die Konsole auf
dem Fernseher einblendet. Lücken bitte als Issue („Translation“) melden; zum Ergänzen siehe
[ENTWICKLUNG.md](ENTWICKLUNG.md#übersetzungen). Handbuch und FAQ gibt es in allen sieben Sprachen in der App.

## Einstellungen

Alles steht in `/data/PS5-Cooling-Center/config.json` und gilt nach jedem Start wieder; eine unbrauchbare Datei wird
beiseitegelegt und die Standardwerte gelten. Läuft die App nicht, greift nichts davon – dann regelt die PS5 den Lüfter mit ihrer
eigenen Kennlinie. Die ganze Konfiguration lässt sich unter System → Expertenansicht exportieren und importieren; das
Web-Profil und die Spielprofile auf der Seite Profil. Die Felder sind in [API.md](API.md#konfiguration) beschrieben.

## Credits

Die Seite **Credits** dankt den Entwicklern, deren Arbeit in der App steckt, und weiteren Entwicklern der Community: je eine
Karte mit Profilbild (in der App eingebaut), Name, Dank und Links. Die Links zu GitHub sind nur anklickbar, wenn der Browser
Internet hat (er lädt dafür einmal ein kleines Symbolbild von github.com); sonst stehen sie grau da. Die vollständige Liste
steht in der [README](../README.de.md#credits-und-danksagung).
