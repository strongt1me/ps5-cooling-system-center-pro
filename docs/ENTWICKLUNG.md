# Entwicklung

[README](../README.md) · [Handbuch](HANDBUCH.md) · [API](API.md) · [Entwicklung](ENTWICKLUNG.md)

Bauen, auf die Konsole senden, Fehlersuche und Aufbau des Quellcodes.

## Bauen

Voraussetzung: das [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk)
**ab v0.42**; gebaut und getestet ist mit **v0.43**
(`ps5-payload-sdk.zip`, SHA-256
`a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb`, so auch
von GitHub als Prüfsumme der Datei angegeben).
`tools/build-windows.sh` erwartet eine Kopie im Projektordner unter
`PS5_PAYLOAD_SDK/` (Git ignoriert diesen Ordner); ein anderer Ort lässt sich mit
der Umgebungsvariablen `PS5_PAYLOAD_SDK_DIR` nennen.

### Warum nicht älter als v0.42

Jedes Payload beginnt im Startcode des SDK (`crt1.o`). Dessen `__kernel_init()`
liest die Firmware der Konsole und vergleicht sie, Fall für Fall, mit den
Versionen, für die es Kernel-Offsets hat. Eine Firmware ohne Fall endet vor
`main()`, ohne eine einzige Zeile im Protokoll. v0.41 kennt Firmware bis 13.40,
v0.42 brachte 13.60 dazu, v0.43 nur Kleinigkeiten (`kernel_iommu_copyin()`,
ein optionales `payload_args->payloadout`); beim Wechsel von v0.41 auf v0.43
blieb alles Übrige im SDK gleich, bis auf die Kopfdatei `ps5/kernel.h`, die nur
neue Deklarationen bekam.

Darum prüft der Bau selbst (`tools/check_sdk_firmware.py`, in `Makefile` und
`installer/Makefile`):

- **vor dem Bauen** das `crt1.o` des SDK: Hat es keinen Fall für 13.00, 13.20,
  13.40 und 13.60, bricht `make` ab;
- **nach dem Binden** die fertige ELF, in die der Startcode eingelinkt ist:
  Fehlt dort ein Fall, wird sie gelöscht.

`make FW_NEED=` (leer) schaltet die Prüfung aus, etwa um bewusst mit einem
älteren SDK zu bauen; `make FW_NEED="13.00 13.20"` prüft nur diese. Ein
Wechsel des SDK baut alles neu: `gen/sdk.stamp` hält eine Prüfsumme des
Startcodes fest, weil die Dateidaten eines nachträglich eingesetzten SDK älter
sein können als die gebauten Dateien und `make` sonst nichts zu tun sähe.

### Windows ohne WSL (Git Bash + LLVM 21)

Seit 24.09.2026 geprüft: Dieser Weg baut dasselbe ELF wie der WSL-Weg,
bis auf die zufällige Build-ID bytegleich. Einmalig einrichten:

```powershell
winget install --id ezwinports.make --exact
winget install --id LLVM.LLVM --version 21.1.8 --exact
```

Danach in Git Bash im Projektordner:

```bash
tools/build-windows.sh
```

⚠ **Nicht LLVM 18**, auch wenn ältere SDK-Anleitungen das nennen: Damit
entsteht ein ELF mit OS/ABI „System V" und anderem Dynamik-Layout. Das Skript
nimmt die SDK-Kopie `PS5_PAYLOAD_SDK/` im Projekt und umgeht die Leerzeichen im
Pfad über kurze DOS-Namen. Die Web-Dateien unter `web/` müssen LF-Zeilenenden
haben, sonst weicht der eingebettete Inhalt vom Release ab.

### Schnellstart (Windows / PowerShell)

Wenn `make` mit `PS5_PAYLOAD_SDK is undefined` abbricht, fehlt die
Umgebungsvariable. In PowerShell für die aktuelle Sitzung setzen:

```powershell
$env:PS5_PAYLOAD_SDK="C:/Pfad/zu/PS5_PAYLOAD_SDK"
make
```

Optional für Deploy den Zielhost setzen:

```powershell
$env:PS5_HOST="192.168.1.50"
make deploy
```

Optional dauerhaft (neues Terminal erforderlich):

```powershell
setx PS5_PAYLOAD_SDK "C:/Pfad/zu/PS5_PAYLOAD_SDK"
```

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

Ergebnis: `PS5_Cooling_Center.elf` (~6,1 MB — Web-UI samt den 30 Profilbildern,
Konverter, Bibliotheken und die Dateien der Kachel (1,7 MB) eingebettet; das Kachel-Paket nicht: Es kostete 10,5 MB Speicher,
und die Konsole konnte dann keine einzige Anfrage mehr bedienen. Mit
`make TILE_PKG=pkg/out/x.pkg` lässt es sich trotzdem einbetten. Der Installer
trägt es mit sich).

`make release` legt zusätzlich einen Ordner `PS5 Cooling and System Center v<Version>/`
mit beiden ELF-Dateien, dem PKG und der LIESMICH an. Es löscht dabei nichts:
Liegt in dem Ordner etwas, das es nicht selbst anlegt, bricht es mit einer
Liste ab; ebenso bei mehr als einem Paket in `pkg/out/`. Nach Änderungen an
einem Header, an `src/*.inc` oder am Makefile selbst baut `make` neu.

Ein-Kommando-Pre-Release-Check (Build + Release + Asset-Prüfung):

```powershell
.\tools\pre-release-check.ps1
```

Optional mit GitHub-Upload/Update der Release-Assets:

```powershell
.\tools\pre-release-check.ps1 -CreateOrUpdateRelease
```

Erwartete Erfolgsausgaben bei einem vollständigen Lauf:

```text
Local release assets: OK
Successfully uploaded 4 assets to v<Version>
GitHub release assets: OK
Pre-release check completed successfully.
```

Wenn einer dieser Marker fehlt, ist der Lauf nicht vollständig erfolgreich
und sollte vor einem finalen Release erneut geprüft werden.

`-Repo` steht ohne Angabe auf `strongt1me/ps5-cooling-system-center-pro`.

Vorlagen für Release-Notes (Kurzfassung/Endnutzer + Langfassung/Entwickler)
liegen in `docs/RELEASE_NOTES_TEMPLATES.md`.

Nach dem Entpacken des SDK einmalig die Ausführungsrechte setzen — das ZIP
transportiert sie nicht:

```bash
chmod +x $PS5_PAYLOAD_SDK/bin/*
```

## Auf die Konsole übertragen

```bash
make deploy PS5_HOST=192.168.1.50
```

Alternativ die `.elf` mit einem beliebigen Payload-Sender an **Port 9021**
(elfldr) schicken.

## Fehlersuche (Windows + PS5)

### Fehler: `PS5_PAYLOAD_SDK is undefined`

- Ursache: Umgebungsvariable ist nicht gesetzt.
- Check:

```powershell
echo $env:PS5_PAYLOAD_SDK
```

- Fix (aktuelle Sitzung):

```powershell
$env:PS5_PAYLOAD_SDK="C:/Pfad/zu/PS5_PAYLOAD_SDK"
make
```

### Fehler: Build startet, aber Toolchain-Binaries fehlen

- Ursache: SDK entpackt, aber Dateirechte/Dateien unvollständig.
- Check: Existieren Dateien in `.../PS5_PAYLOAD_SDK/bin/`.
- Fix: SDK erneut sauber entpacken und Pfad kontrollieren.

### Fehler: Web-UI nicht erreichbar auf `http://<PS5-IP>:8086`

- Ursache: Payload nicht aktiv oder Bindung nicht im LAN.
- Check von Windows:

```powershell
Test-NetConnection -ComputerName <PS5-IP> -Port 8086
```

- Erwartung: `TcpTestSucceeded : True`.
- Falls `False`: Payload neu laden und in der App-Konfiguration
  `bind_address` auf `0.0.0.0` setzen.

### Fehler: Deploy klappt, aber keine Reaktion auf der Konsole

- Check: Richtige Ziel-IP in `PS5_HOST`.
- Check: Payload-Loader/elfldr auf der PS5 aktiv.
- Test:

```powershell
make deploy PS5_HOST=<PS5-IP>
```

### Fehler: API-Antworten schlagen sporadisch fehl

- Check:

```powershell
Invoke-RestMethod -Uri "http://<PS5-IP>:8086/api/v1/config" -Method Get -TimeoutSec 8
```

- Wenn Verbindungsfehler kommen: zuerst Port-Erreichbarkeit testen,
  dann Payload neu starten und erneut prüfen.

### Fehler: Release-LIESMICH ist nicht synchron

- Check:

```powershell
.\tools\check-release-liesmich-sync.ps1
```

- Auto-Fix:

```powershell
.\tools\check-release-liesmich-sync.ps1 -Fix
```

- Zweck: prüft den Abschnitt `KURZDIAGNOSE (PC-SEITE)` in allen
   Release-Ordnern gegen `docs/LIESMICH.txt`.

## Kurztest nach dem Bauen

1. Presets testen (`Kühl`, `Ausgewogen`, `Leise`):
   Erwartung: Direktwert wird gesetzt und in der Live-Anzeige aktualisiert.
2. Experte-Modus schalten:
   Erwartung: Zusätzliche Karten für Locale- und Firmware-Rohwerte erscheinen.
3. Konfiguration exportieren:
   Erwartung: `ps5tm-config.json` wird heruntergeladen.
4. Konfiguration ändern und importieren:
   Erwartung: Werte werden übernommen, Seite zeigt nach Reload die alten Daten.
5. Spielprofile nach Config-Import prüfen:
   Erwartung: Regeln bleiben enthalten (Export/Import umfasst `game_rules`).

## Host-Test

> [!WARNING]
> Diese Anleitung stammt aus einer frühen Fassung und **baut mit dem heutigen
> Quellstand nicht mehr** (geprüft am 02.10.2026): Es fehlen FreeBSD-Kopfdateien
> wie `sys/sysctl.h` und `sys/cpuset.h`, und `test/stubs.c` deckt die neueren
> Module nicht ab. Der Quellcode kennt dafür den Schalter `-DPS5TM_HOST_TEST`;
> eine passende, aktuelle Host-Testumgebung gehört noch nicht zum Repository.
> Die Anleitung bleibt hier als Ausgangspunkt stehen.

Server, API, Konfiguration und Kurvenmathematik lassen sich ohne Konsole auf
Linux prüfen — `test/stubs.c` simuliert Sensorik und Kernel:

```bash
gcc -g -O0 -Isrc -Isrc/third_party -Itest/include \
    -DPS5TM_DATA_DIR='"/tmp/ps5tm"' -o /tmp/ps5tm-host \
    src/*.c gen/assets.c src/third_party/cJSON.c test/stubs.c \
    src/third_party/libdeflate/lib/adler32.c \
    src/third_party/libdeflate/lib/crc32.c \
    src/third_party/libdeflate/lib/deflate_compress.c \
    src/third_party/libdeflate/lib/deflate_decompress.c \
    src/third_party/libdeflate/lib/zlib_compress.c \
    src/third_party/libdeflate/lib/zlib_decompress.c \
    src/third_party/libdeflate/lib/utils.c \
    src/third_party/libdeflate/lib/x86/cpu_features.c \
    -lpthread
mkdir -p /tmp/ps5tm && /tmp/ps5tm-host
```

Seit 1.46.0 braucht der Host-Build zusätzlich libdeflate (Kompression fürs
Konvertieren, `conv_pfs.c`) — dieselben Quellen wie `DEFLATE_SRCS` im
Makefile, sonst fehlen beim Binden die `libdeflate_*`-Symbole.

Danach `http://127.0.0.1:8086`. Der ICC-ioctl ist auf dem Host naturgemäß nicht
testbar und meldet sauber „nicht verfügbar".

## Kachel-Dateien und Kachel-Paket bauen

Seit 1.47.0 trägt die ELF die Dateien der Kachel selbst: `tile/sce_sys/` (`param.json`,
`icon0.png`, `pic0.png`, `pic1.png`). `tools/gen_tile_files.py` prüft sie (JSON, PNG-Kennung,
Title-ID gleich `PS5TM_TITLE_ID` in `src/tile.c`, höchstens 4 MB), legt gleiche Dateien nur
einmal ab und schreibt `gen/tile_files.c`; `src/tile.c` macht daraus beim Start die Kachel
(siehe das [Handbuch](HANDBUCH.md#homescreen-kachel)). Das Paket unten bleibt der Rückfall und
steckt im Installer.

Gebaut wird sie mit `pkg/build.ps1` (Bilder skalieren → `LibProsperoPkg.dll`
bereitstellen → `dotnet run`). Voraussetzung ist das **.NET-10-SDK**.

## Übersetzungen

Die Oberfläche gibt es auf Deutsch, Englisch, Italienisch, Spanisch, Französisch und Russisch. Die Quelltexte sind deutsch und
bleiben es; die Übersetzung geschieht zur Laufzeit im Browser.

- `web/i18n-boot.js` wählt die Sprache (gemerkte Wahl in `localStorage` `lang`, sonst Browsersprache, sonst Englisch), noch bevor
  die Seite gezeichnet wird, und setzt `window.PS5_LANG` und `window.PS5_LOCALE` (Zahlen und Datum).
- `web/i18n.js` lädt `web/lang/<xx>.json` und ersetzt Texte im Dokument und alle später hinzukommenden (MutationObserver).
  Das Wörterbuch ist `{"v":1,"lang":"xx","t":{"deutscher Text":"Übersetzung"}}`. Ein Text mit Werten, die das Programm einsetzt,
  steht mit `{0}`, `{1}` … darin (aus `${…}` einer Vorlage in `app.js` oder `%d`/`%s` einer Meldung in `src/*.c`); Texte aus mehreren
  Stücken, die das Programm mit „ · “ verbindet, werden stückweise übersetzt. Ein Element mit `data-no-i18n` bleibt, wie es ist.
- Fehlt ein Text, bleibt er deutsch. Im Browser zeigt `PS5I18N.misses()` die Texte, die auf der Seite standen, aber nicht im
  Wörterbuch; so findet man Lücken (die Browsertests sammeln sie je Sprache).
- **Einen Text ergänzen oder verbessern:** in `web/lang/<xx>.json` den Schlüssel (der deutsche Text, genau wie im Quelltext) mit der
  Übersetzung eintragen. Die Platzhalter müssen in beiden gleich oft und genau als `{0}` vorkommen. Danach `tools/gen_assets.py` (läuft
  beim Bauen von selbst) – die Wörterbücher sind in die ELF eingebettet.
- **Eine neue Sprache:** `web/lang/<xx>.json` anlegen, in `web/i18n-boot.js` (`LOCALES`) und in der Sprachwahl in `web/index.html`
  eintragen. `tools/i18n/` hat die Hilfen: `extract.py` und `extract_c.py` ziehen alle deutschen Texte aus Oberfläche und Quelltext
  (`strings.json`, `strings_c.json`), `check_lang.py <xx>` meldet Lücken und Platzhalterfehler eines Wörterbuchs.
- **Mehrzahlformen:** Wo nach einer Zahl ein Hauptwort steht, schreiben fast alle Sprachen Einzahl und Mehrzahl als eigene Muster
  (`{0} file` / `{0} files`; im Deutschen sind es zwei Quelltexte, `{0} Datei` und `{0} Dateien`). Russisch braucht drei Formen: Dort steht in
  der Übersetzung eine Mehrzahlgruppe `{0|файл|файла|файлов}` (1, 21 … / 2–4, 22–24 … / sonst; ein Bruch nimmt die zweite Form), die die
  Laufzeit (`PS5I18N.plural` in `web/i18n.js`) durch **das eine Wort** ersetzt, das zum Wert von `{0}` passt; die Zahl steht getrennt als `{0}`
  davor. `check_lang.py` zählt eine Gruppe nicht als Platzhalter.
- **Texte, die der Extraktor nicht findet:** Einzelne kleingeschriebene Wörter, die das Programm in Sätze einsetzt (`kopiert`, `fertig` …), stehen
  von Hand in `tools/i18n/extra_sources.json`. Aneinandergefügte Zeichenketten (`"a" + "b"`) und `${bedingung ? "x" : "y"}` löst der Extraktor
  selbst auf.
- **Handbuch und FAQ** (`web/handbuch*.html`, `web/faq*.html` und die PDFs) entstehen aus `tools/i18n/docs/docs_src.<xx>.json` (Texte) und
  `docs_style.css`; `python tools/i18n/docs_build.py app` schreibt die Fassungen für die App, `... full --shots <Ordner>` die mit Bildschirmfotos
  für PDF und HTML (der Ordner hat je Sprache einen Unterordner mit den Bildern; PDF daraus mit dem Druckdialog des Browsers).
- Die Meldungen, die die Konsole selbst auf dem Fernseher einblendet, sind deutsch (sie entstehen in `src/notify.c`, nicht im Browser).

## Aufbau des Quellcodes

```text
src/main.c          Start, Ablösung einer älteren Instanz, Serverschleife
src/platform.c      Rechteausweitung, Temperatursensoren, ICC-Lüftersteuerung, Firmware
src/fan.c           Komfortregelung (Mittelwert, Totzone, Trend) und Servo auf die Lüfterschwelle
src/probe.c         Hintergrund-Thread für alle Abfragen an Systemdienste
src/http.c          HTTP/1.1-Server (POSIX-Sockets, ohne Fremdbibliothek), Host-/Origin-Prüfung
src/assets.c        liefert die eingebetteten Dateien der Oberfläche aus, gzip wie gespeichert oder entpackt (libdeflate), je nach Accept-Encoding
src/api.c           REST-Schnittstelle unter /api/v1
src/config.c        JSON-Konfiguration, atomares Speichern
src/log.c           Ereignis-Ringpuffer (gespiegelt ins Kernelprotokoll); Steuerzeichen einer Meldung werden zu Leerzeichen, damit aus einem Namen mit Zeilenumbruch keine zweite Zeile wird
src/klogown.h       erkennt Zeilen der App selbst im Kernel-Puffer; die Leser (Spiel, Takt, Controller, Mikrofon-Taste) überspringen sie
src/sysinfo.c       Modell, Laufzeit, Speicherplatz (zwischengespeichert)
src/netdisp.c       Netzwerkverbindung und angeschlossener Bildschirm
src/telemetry.c     Stromschienen, Takt live, Bildrate
src/clocks.c        Taktgrenzen aus der Energiezustandstabelle der Konsole
src/chanlog.c       Aufzeichnung aller SoC-Sensorkanäle (zum Zuordnen)
src/history.c       Temperaturverlauf über einen Tag
src/thermalog.c     Langzeitauswertung der Kühlleistung (thermal-health.csv)
src/gamestate.c     Welches Spiel läuft und ob es im Vordergrund ist
src/dualsense.c     Controller-Akku aus dem Kernelprotokoll
src/micbutton.c     Doppeldruck der Mikrofon-Taste -> Temperaturmeldung
src/notify.c        Bildschirmmeldungen, Adresse im Heimnetz
src/power.c         Ausschalten und Neustart
src/procmgr.c       Laufende Payloads auflisten und beenden
src/payloads.c      Payload-Dateien (interner Ordner, USB): starten, kopieren, löschen
src/profile.c       Anzeigename und Profilbild des Benutzers
src/regstats.c      Konsolenname aus der Registry (nur lesend)
src/library.c       Spiele des Startbildschirms, Start, Cover
src/sqlite_ro.c     Nur-lesender SQLite-Leser für die App-Datenbank der Konsole
src/smp.c           Schnittstelle zu ShadowMountPlus (Port 10101)
src/gamecopy.c      Spiele kopieren (jede Datei zurücklesen, Prüfsummen-Datei)
src/gameconvert.c   Spiele konvertieren (exFAT, ffpfsc, ffpkg), Ergebnis zurücklesen und prüfen
src/gamemove.c      Verschieben und Entpacken über ShadowMountPlus
src/conv_exfat.c    exFAT-Abbild schreiben (aus MkPFS portiert)
src/conv_pfs.c      PFS-Container mit PFSC-Kompression (aus MkPFS portiert), vollständige Prüfung
src/conv_ufs2.c     UFS2-Abbild (ffpkg) nach dem Vorbild von UFS2Tool, Prüfsumme je Datei
src/checkfile.c     Zurücklesen mit CRC-32/SHA-256, Prüfsummen-Datei im sha256sum-Format
src/sha256.c        SHA-256 (eigen, mit -O2 gebaut)
src/iopolicy.c      Lesen und Schreiben gleichzeitig nur zwischen zwei Laufwerken, Vorauslesen
src/gamedelete.c    Spiele und Sicherungen löschen: Plan mit Einmal-Kennwort, Deinstallation über die Konsole (Helfer), ShadowMountPlus abmelden + Abbild löschen, Sicherungen; löscht nie über Verknüpfungen, Laufwerksgrenzen oder außerhalb von /mnt/usb*, /mnt/ext*, /data
src/ioerr.h         die Worte für einen gescheiterten Zugriff: abgemeldetes Laufwerk (ENXIO/ENODEV), EIO, sonst strerror()
src/powerguard.c    Ruhemodus-Uhr bei langen Vorgängen zurücksetzen
src/klog.c          Kernel-Log live: Kopie von kern.msgbuf, neue Zeilen über einen Anker aus den letzten sechs Zeilen
src/klogfiles.c     Kernel-Log in Dateien: Auszüge von der Seite, Aufnahme (eigener Thread liest ps5tm_klog_fetch), Ordner klog-live-log leeren
src/playtime.c      Spielzeit: Sitzungen je Spiel mit Temperaturen (sessions.csv, session-open.csv), eigener Thread
src/libcache.c      Zwischenspeicher der Spieleliste (covers_and_more: meta.json + icon0.png je Titel), nur speichern und herausgeben; library.c prüft, was es davon glaubt
src/pkgparse.c      Paket-Dateien lesen (PS4 CNT, PS5 FIH, geteilte PS5MPKG1): Titel, Version, Art, Bild; jede Angabe der Datei wird gegen ihre Größe geprüft
src/pkgscan.c       Pakete suchen (Wurzel und Ordner pkg der Laufwerke, im Hintergrund), Liste mit Kennungen, geteilte Pakete als ein Eintrag
src/pkgsplit.c      ein Paket in Teile (PS5MPKG1) aufteilen: Zwischennamen, Zurücklesen mit SHA-256, Namen erst am Ende, Abbruch räumt auf
src/pkgstream.c     kleiner HTTP-Server nur auf 127.0.0.1:18851: liefert das Paket (ein geteiltes als ein Strom) mit Range an die Installation der Konsole, nur den einen gepinnten Namen
src/pkginstall.c    Paket installieren: Plan, ein Auftrag je Installation, Hilfsprogramm starten und befragen, Fertig erst bei Systemstatus + alle Bytes, danach Suche in der Datenbank (vier Enden: installiert, als fertig gemeldet, geliefert ohne Bestätigung, fehlgeschlagen); die Verbindung zum Hilfsprogramm liegt ab fd 200
src/pkginst.h       Aufteilung des Pakets in Stücke (Slices) und der Server; src/pkginst_ipc.h die Nachrichten zwischen App und Hilfsprogramm
src/helper/pkginst_helper.c  das Hilfsprogramm (eigene ELF, in die App eingebettet): ruft sceAppInstUtil* auf; höchstens ein Installationsaufruf je Prozess, fragt nach dem Fortschritt (auch als Ersatz für ein gestorbenes); beendet die Bibliothek nie, steigt mit _exit aus; gibt sceAppInstUtilGetInstallStatus einen eigenen 16-KiB-Puffer (die Funktion schreibt 712 Bytes, das bekannte Feld hat 600) und hält die Verbindung zur App ab fd 100
tools/gen_blob.py   macht aus der Hilfs-ELF ein C-Feld und prüft, dass die Kennungsstelle genau einmal vorkommt
src/savebackup.c    Spielstände: sichern, prüfen, zurückspielen (rohe Kopie mit Zurücklesen, Liste mit SHA-256, Stand vor dem Zurückspielen), ein Thread je Vorgang
src/tile.c          Kachel-Installation über AppInstUtil (Ordner aus eingebetteten Dateien, Paket als Rückfall)
src/dynsym.c        Symbolsuche in Sony-Modulen
src/sony_api_lock.c gemeinsame Sperre für empfindliche Systemaufrufe
src/third_party/    cJSON, libdeflate
web/                Web-Oberfläche (per tools/gen_assets.py eingebettet, gzip), Profilbilder in web/avatars/, Wörterbücher in web/lang/
tile/               Dateien der Kachel (sce_sys), eingebettet über tools/gen_tile_files.py
installer/          eigenständiger Kachel-Installer (ELF)
tools/              Build-, Release- und Diagnose-Skripte
test/               Stubs der früheren Host-Tests
docs/               Handbuch, API, Entwicklung, Versionshinweise, Entscheidungsprotokoll
```

## Vorgeschichte: Warum die alte Version nicht funktionierte

Das Projekt ist der Nachfolger von *PS5 Temperature Manager*; C-Backend und
Web-Oberfläche wurden neu geschrieben.

Per ELF-Analyse verifiziert:

1. **Keine JB-Rechteausweitung.** Die AuthID-Konstante `0x4801000000000013`
   fehlte in der Binärdatei vollständig. Ohne uid 0 + AuthID + Capabilities
   lehnt der Kernel `open("/dev/icc_fan")` ab — die Lüftersteuerung konnte
   nie greifen, der Adapter meldete dauerhaft „nicht verfügbar".
2. **`libSceAppInstUtil` war nicht gelinkt** (nur `libkernel_sys` +
   `libSceLibcInternal`) — die Kachel-Installation war unmöglich.
3. Mit fremder Toolchain gebaut (OS/ABI „System V", 8 Segmente) statt mit dem
   ps5-payload-sdk (FreeBSD-ABI, 4 Segmente).

Alle drei Punkte sind in dieser Version behoben.
