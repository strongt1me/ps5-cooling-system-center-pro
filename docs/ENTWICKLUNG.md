# Entwicklung

[README](../README.de.md) · [Handbuch](HANDBUCH.md) · [API](API.md) · [Entwicklung](ENTWICKLUNG.md)

> **This document is in German.** The [README](../README.md) is in English and has a short build guide.

Bauen, auf die Konsole senden, Fehlersuche, Tests, Aufbau des Quellcodes und Werkzeuge.

## Bauen

Voraussetzung: das [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk)
**v0.50** (seit 20.11.2026; es kennt jede Firmware von **1.00 bis 14.22**)
(`ps5-payload-sdk.zip`, 8 839 112 Bytes, SHA-256
`2993a69e52fea04a32bf1753318fc60b3f918ab635bdf946870e7d1849557bb2`, so auch
von GitHub als Prüfsumme der Datei angegeben). Ein älteres SDK lehnt der Bau ab,
weil ihm Firmware-Fälle fehlen (siehe unten; `make FW_NEED=…` schaltet das um).
`tools/build-windows.sh` erwartet eine Kopie im Projektordner unter
`PS5_PAYLOAD_SDK/` (Git ignoriert diesen Ordner); ein anderer Ort lässt sich mit
der Umgebungsvariablen `PS5_PAYLOAD_SDK_DIR` nennen. Gebaut wird unter Windows
mit Git Bash und **LLVM 21** (nächster Abschnitt).

### Warum das SDK die Firmware bestimmt

Jedes Payload beginnt im Startcode des SDK (`crt1.o`). Dessen `__kernel_init()`
liest die Firmware der Konsole und vergleicht sie, Fall für Fall, mit den
Versionen, für die es Kernel-Offsets hat. Eine Firmware ohne Fall endet vor
`main()`, ohne eine einzige Zeile im Protokoll. v0.41 kennt Firmware bis 13.40,
v0.42 brachte 13.60 dazu, v0.43 nur Kleinigkeiten (`kernel_iommu_copyin()`,
ein optionales `payload_args->payloadout`); beim Wechsel von v0.41 auf v0.43
blieb alles Übrige im SDK gleich, bis auf die Kopfdatei `ps5/kernel.h`, die nur
neue Deklarationen bekam. v0.44 (10.10.2026) ergänzt die Stub-Bibliotheken `libSceAgc` und
`libSceAgcDriver`, `killpg()` in der libc und Verbesserungen der Werkzeugkette; die App kommt davon nicht
zum Zug: mit v0.44 gebaut ist die ELF bis auf die zufällige 16-Byte-Build-ID dieselbe wie mit v0.43
(18.10.2026 verglichen). v0.45 (21.10.2026) bringt die Kernel-Offsets für Firmware 14.00 und 14.10
(geändert sind nur `target/lib/crt1.o` und `libkernel_sys.so`, gleiche Dateiliste mit 2926 Dateien).
v0.50 (19.11.2026) kennt **jede Firmware von 1.00 bis 14.22** (14.20 und 14.22 sind neu; `tools/check_sdk_firmware.py sdk`
findet alle Fälle von 01.00 bis 14.22, 14.30 fehlt). Gegenüber v0.45 sind nur sieben Dateien anders (gleiche Dateiliste mit
2926 Dateien): `target/lib/crt1.o`, `target/include/ps5/kernel.h` und `iommu.h`, `target/lib/libc++abi.a` und `libunwind.a`,
`bin/prospero-meson` und `win/prospero-lld.exe`. v0.45 liegt als Sicherung neben dem Projekt (`PS5_PAYLOAD_SDK-v0.45-Sicherung`).

Darum prüft der Bau selbst (`tools/check_sdk_firmware.py`, in `Makefile`):

- **vor dem Bauen** das `crt1.o` des SDK: Hat es keinen Fall für 1.00, 2.70, 3.21, 4.03,
  4.51, 5.50, 6.50, 7.61, 8.60, 9.60, 10.60, 11.60, 12.00, 12.70, 13.00, 13.20, 13.40,
  13.60, 14.00, 14.10 und 14.22, bricht `make` ab;
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

Ergebnis: `PS5_Cooling_Center.elf` (~25 MB — Web-UI samt den 30 Profilbildern und
Wörterbüchern, Konverter, Bibliotheken, die Hilfsprogramme, Kraken Mount, Dump Forge (als zstd-Rahmen),
die APR-EMU-, PlayGo- und DLC-Fassungen, die LZ4-Profile und die Dateien der Kachel `PSCC69690` eingebettet).

`make release` (bzw. `tools/build-windows.sh release`) legt zusätzlich einen Ordner
`PS5 Cooling and System Center v<Version>/` mit der ELF (`PS5_Cooling_System_Center_v<Version>.elf`) und der
LIESMICH an. Es löscht dabei nichts: Liegt in dem Ordner etwas anderes als diese beiden, `FAKE86921.zip`,
`FAKE86922.zip` und `payload-manager.json` (die legt `tools/release.sh` dazu), bricht es mit einer Liste ab.
Nach Änderungen an einem Header, an `src/*.inc` oder am Makefile selbst baut `make` neu.

### Kacheln bauen (`tools/make_starter.sh`)

```bash
tools/make_starter.sh [PS5_Cooling_Center.elf]
```

baut beide Kacheln nach `starter/out/`: **`FAKE86921`** (Vollbild-App aus `tvapp/`, über `tools/tvapp-build.sh`
im WSL-Baum `~/ps5cc-tvapp`) und **`FAKE86922`** (Browser-Kachel aus `starter/browser/`). Beide tragen die App-ELF
in ihrer `eboot.bin`; die Hintergründe (`pic0.dds`/`pic1.dds`, BC7, 3840 × 2160) macht `tools/png_to_bc7_dds.py`
aus `starter/sce_sys/pic0.png`. Braucht WSL (Ubuntu-26.04) mit clang, clang-18, ninja, Python und ps5-native-tool;
der erste Lauf lädt das festgelegte ps5-opengl-SDK (Prüfsumme geprüft).

### Veröffentlichen (`tools/release.sh`)

```bash
tools/release.sh <X.Y.Z> --dry-run     # nur prüfen und auflisten
tools/release.sh <X.Y.Z>               # mit einer Rückfrage vor Push und Release
```

Ablauf: Prüfungen (sauberer Baum, Version über dem letzten Tag, Anmeldung, Zweig, alle Host-Tests über
`tools/run-host-tests.sh`), Version in `src/ps5tm.h`, den READMEs und `docs/LIESMICH.txt`, Handbuch und FAQ
(`tools/i18n/docs_build.py app`), Commit `release: vX.Y.Z`, Bau (`tools/build-windows.sh release`,
`tools/make_starter.sh`, `FAKE86921.zip` und `FAKE86922.zip` mit Python als ZIP ohne Kompression,
`payload-manager.json` mit `tools/make_payload_json.py`), Suche nach privaten Angaben, erst dann Push
`origin public:main` und `gh release create` mit fünf Dateien (ELF, zwei Kachel-Zips, LIESMICH,
`payload-manager.json`; Seite englisch aus `docs/RELEASE_NOTES.md`, die deutschen Notizen
`docs/RELEASE_NOTES.de.md` aufklappbar), zuletzt der Abgleich der Prüfsummen.

Ältere Prüfung ohne Veröffentlichung (Bau unter WSL, `make clean && make -j1 && make release`, dann
Prüfung der Dateien im Release-Ordner; die Kachel-Zips müssen dort schon liegen):

```powershell
.\tools\pre-release-check.ps1
```

Optional mit GitHub-Upload/Update der Release-Assets (nur ELF, `FAKE86921.zip` und LIESMICH; eine
vollständige Veröffentlichung macht `tools/release.sh`):

```powershell
.\tools\pre-release-check.ps1 -CreateOrUpdateRelease
```

Erwartete Erfolgsausgaben bei einem vollständigen Lauf:

```text
Local release assets: OK
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
tools/deploy.sh <IP>                 # ELF an Port 9021, danach beide Kacheln per FTP
tools/deploy.sh <IP> --nur-elf       # nur die ELF (--nur-kacheln: nur die Kacheln)
tools/deploy.sh <IP> --dry-run       # nur ausgeben, was geschähe
```

`tools/deploy.sh` (Git Bash, ohne WSL) schickt die ELF über `tools/lib/send_elf.ps1` in einem eigenen Prozess an
den ELF-Lader (Port 9021) – nicht, solange die App einen Auftrag bearbeitet oder an der Konsole gerade ein Titel
startet (`--trotzdem` übergeht das) – und wartet, bis `GET /api/v1/system` die neue `app_version` meldet. Danach
lädt es `starter/out/FAKE86921` und `FAKE86922` per FTP (Port 2121) nach `/data/homebrew/<Kachel>/`, die
`eboot.bin` zuletzt, und vergleicht deren Größen. Rückgabewert 3: Die Vollbild-App ist auf der Konsole offen
(ihre `eboot.bin` lässt sich dann nicht ersetzen).

Nur die ELF geht auch mit `make deploy PS5_HOST=<IP>` (der Sender des SDK) oder mit einem beliebigen
Payload-Sender an **Port 9021** (elfldr).

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
2. Expertenmodus schalten:
   Erwartung: Zusätzliche Karten für Locale- und Firmware-Rohwerte erscheinen.
3. Konfiguration exportieren:
   Erwartung: `ps5tm-config.json` wird heruntergeladen.
4. Konfiguration ändern und importieren:
   Erwartung: Werte werden übernommen, Seite zeigt nach Reload die alten Daten.
5. Spielprofile nach Config-Import prüfen:
   Erwartung: Regeln bleiben enthalten (Export/Import umfasst `game_rules`).

## Regelkreis-Simulation (Lüfter + Wächter)

`test/sim/fansim.c` bindet `src/fan.c` direkt ein und lässt die echte Regelschleife samt `src/fanguard.c` gegen ein
Modell aus Firmware (Drehzahl je Kelvin über der Schwelle, Rücksetzung auf 91 °C bei Spielwechsel), Lüfterträgheit und
Zwei-Knoten-Wärmemodell laufen, in virtueller Zeit (eine Stunde in unter einer Sekunde). Unter WSL/Linux mit gcc:

```bash
sh test/sim/run.sh             # alle Szenarien, eine Zeile Messwerte je Szenario
sh test/sim/run.sh step_t78    # eines; SIMV=1 zeigt das Protokoll, SIMTRACE=datei.csv schreibt den Verlauf
```

Das Modell ist nicht an einer Konsole vermessen (nur die Firmware-Kennlinie ist es); die Ergebnisse zeigen Tendenzen
und Wechselwirkungen, keine Grade.

## Systemfunktionen gegen die Firmwares prüfen

Die App bindet 222 Systemfunktionen beim Laden (die undefinierten Symbole der ELF) und sucht 27 weitere zur Laufzeit (`dlsym`,
`ps5tm_dynsym`). Die Systembibliotheken exportieren nach NID, dem Hash des Namens (SHA-1 über Name und 16 Byte Salz aus
`tvapp/tooling/native/sce_module_writer.cpp`, die ersten 8 Byte umgekehrt, Base64, 11 Zeichen, `/` als `-`).
`tools/check_fw_imports.py` rechnet die NID jedes Namens aus und sucht sie in den Exporten der entschlüsselten Bibliotheken
je Firmware (ein Ordner je Firmware, z. B. `12.70`):

```bash
python3 tools/check_fw_imports.py --fw-dir "/pfad/zu/den/firmwares"      # oder PS5_FW_DIR; -v zeigt jeden Namen
```

Es liest nur und kopiert nichts aus den Dumps (die Dateien gehören nicht ins Repository). Der Rückgabewert ist 1, sobald ein Name
unerwartet fehlt; bekannte Ausnahmen (`KNOWN_OPTIONAL` im Werkzeug) werden benannt. Nach einer Änderung an den Namen, die die App
per `dlsym` sucht, oder nach einem neuen SDK laufen lassen. „Vorhanden“ heißt nur: Die Firmware exportiert die Funktion; Argumente
und Verhalten sind damit nicht belegt, Firmwares ohne Dump (über 12.70) nicht abgedeckt, 7.00 ist verschlüsselt. Mit
Zwischenspeicher (`~/.cache/ps5cc-fw-imports`) dauert ein Lauf wenige Sekunden. Test des Werkzeugs ohne Dump: `sh test/fwimports/run.sh`.

## PS4Mount-Prüfungen

Die Prüfprogramme aus dem Quellprojekt (`third_party/ps4mount/test/`) prüfen den Port `src/ps4m_*.c`: `unit.c` PKG-Kopf,
Einträge, Schlüssel, `param.sfo`, Herkunftsliste; seit PS4Mount 0.8.0 dazu `pkgindex_test.c`, `klogdup_test.c` und
`accopy_test.c`; `test/ps4m/playgo_hostile_test.c` (von der App) prüft feindliche `playgo-chunk.dat`. Unter WSL/Linux mit gcc und python3:

```bash
sh test/ps4m/run.sh            # baut die Vorlagen-Kopfdatei, übersetzt; jedes Programm meldet „<n> checks, 0 failed“
```

Der Rückgabewert ist ungleich 0, sobald eine Prüfung scheitert. Bringt ein neues PS4Mount neue Prüfungen in seine Testdateien,
laufen sie hier mit.

## Host-Tests

Die Prüfungen ohne Konsole liegen unter `test/<Name>/`, je mit einem `run.sh` (Linux/WSL, gcc, die meisten mit ASan/UBSan;
ohne Konsole und ohne SDK). Welche zur festen Liste gehören, steht in `.github/workflows/tests.yml` (47 Tests);
`tools/run-host-tests.sh` arbeitet genau diese Liste ab und danach die Regelkreis-Simulation (`test/sim`). Unter Git Bash
läuft es in WSL (`HOST_TESTS_WSL_DISTRO`, Vorgabe `Ubuntu-26.04`); `tools/release.sh` ruft es vor jedem Release auf.

```bash
bash tools/run-host-tests.sh      # alle: Exit 0 bestanden, 1 ein Test scheiterte, 3 kein gcc bzw. WSL
sh test/krakenmig/run.sh          # ein einzelner
```

Nicht in der Liste, weil sie länger laufen oder eine Vorgabe brauchen: `test/fuzz` (libFuzzer, clang mit
`libclang-rt-dev`: `sh test/fuzz/run.sh [Sekunden je Ziel]`) und `test/lz4cross` (beim Wechsel der LZ4-Fassung:
`sh test/lz4cross/run.sh <Ordner der anderen Fassung>`). `test/stubs*.c` sind Reste eines früheren Host-Builds der ganzen
App; der baut mit dem heutigen Quellstand nicht mehr (der Schalter `-DPS5TM_HOST_TEST` dient heute den Einzeltests).

### Vollbild-App (`tvapp/`)

Die Vollbild-App wird nicht im Projektordner gebaut: `tools/tvapp-build.sh` (von `tools/make_starter.sh` gerufen) spiegelt
`tvapp/` in einen Baum im Linux-Dateisystem von WSL (`~/ps5cc-tvapp`, anders mit `PS5TM_TVAPP_TREE`) und baut dort. Dort
laufen auch ihre Tests:

```bash
cd ~/ps5cc-tvapp
USE_CCACHE=0 make test-unit       # GoogleTest-Einheitstests aus tvapp/tests/unit, mit ASan/UBSan
make test-integration             # Tests der Bauwerkzeuge (tvapp/tests/test_*.py)
make host-snapshots               # der Rundgang auf dem PC, Bilder nach build/snapshots
```

## Kachel-Dateien (PSCC69690)

Seit 1.47.0 trägt die ELF die Dateien der früheren Homescreen-Kachel `PSCC69690` selbst: `tile/sce_sys/` (`param.json`,
`icon0.png`, `pic0.png`, `pic1.png`). `tools/gen_tile_files.py` prüft sie (JSON, PNG-Kennung,
Title-ID gleich `PS5TM_TITLE_ID` in `src/tile.c`, höchstens 4 MB), legt gleiche Dateien nur
einmal ab und schreibt `gen/tile_files.c`. `src/tile.c` legt daraus die Kachel nur für eine ELF an, die ohne die Kacheln
`FAKE86921`/`FAKE86922` läuft, und nur einmal (siehe das
[Handbuch](HANDBUCH.md#kacheln-fake86921-vollbild-und-fake86922-browser)). Ein Kachel-Paket und ein eigener Installer werden
seit 19.10.2026 weder gebaut noch ausgeliefert; der Paketweg in `src/tile.c` (`tile.pkg` im Datenordner,
`POST /api/v1/tile/upload`) und `make TILE_PKG=<Paket>` bleiben nur für ein Paket aus einer älteren Veröffentlichung.

## Kacheln FAKE86921/FAKE86922: ältere laufende App ersetzen (seit 1.77.1)

Beide Kacheln (`tools/make_starter.sh <elf>`) tragen eine Kopie der App-ELF und vergleichen beim Antippen die
Versionen:

- Die ELF enthält die Marke `PS5CC-VERSION:<Version>:` (`ps5tm_version_mark` in `src/main.c`), die laufende App meldet
  `app_version` in `GET /api/v1/system`.
- **Browser-Kachel** (`starter/browser/title/starter.c`): `maybe_update()` vergleicht (`compare_versions`), prüft über
  die Auftrags-Routen (`"active":true`), ob ein Auftrag läuft, schickt bei „älter und frei“ die ELF an Port 9021 und wartet
  höchstens 120 × 250 ms auf dieselbe Version. Gleiche, neuere oder unbekannte Versionen und Aufträge lassen die App stehen.
- **Vollbild-Kachel**: dieselbe Logik in `HttpSource::replace_older_app()` und der Verbindungsphase
  (`tvapp/src/platform/ps5/http_source.cpp`, Wartezeit höchstens 30 s); die reine Vergleichs- und Auftragslogik steht in
  `tvapp/src/ps5cc/app_update.cpp/.hpp`, ihr Einheitstest in `tvapp/tests/unit/ps5cc_app_update_test.cpp`
  (`USE_CCACHE=0 make test-unit` im WSL-Baum `~/ps5cc-tvapp`).
- Neue Auftragsarten, die eine Ersetzung abbrechen würde, gehören in die Liste der Auftrags-Routen beider Kacheln.

## Unsignierte Programmdateien: fsign (seit 07.11.2026)

`src/fsign.c` schreibt einfache ELF-Programmdateien (`e_type` `0xFE10` / `0xFE18`; Namen `eboot*.bin`, `.elf`, `.self`, `.prx`, `.sprx`) als fake-signierte SELF-Dateien, weil die Konsole einfache ELF-Dateien nicht startet. Der Aufbau ist byteweise der von `make_fself.py --ptype fake` (ampr-emu-ng) und `fself.rs` von ps5-dump-forge 0.5.4 (Kopf `4F 15 3D 1D`, je kopiertem Abschnitt ein Paar Einträge, Kopf mit SHA-256 der ELF, Nullblöcke, dann die Abschnitte, dann `PT_SCE_VERSION`); **nicht** der von `src/dump_fself.c` (anderer Magic, längerer Metabereich, an der Konsole ebenfalls nicht erprobt).

- **Konvertieren** (`gameconvert.c`, `cv_run`): `ps5tm_fsign_scan` listet die Programmdateien, `ps5tm_fsign_stage` schreibt sie (und einen angepassten `ampr_emu.index`) nach `<Ziel>.ps5cc-sign-teil`, die zweite Quelle der Abbild-Schreiber (`exfat_image_plan_roots`, `ufs2_write_tree_roots`: eine Datei in mehreren Quellen wird aus der letzten gelesen); `ps5tm_fsign_rmtree` räumt am Ende und bei jedem Abbruch auf. Nur PS5-Ordner (`nroots == 0`).
- **Packen in einen neuen Ordner** (`imgjob_lz4.inc`, `cp_sign` in `cp_walk`): die losen Programmdateien werden beim Kopieren als FSELF geschrieben und zurückgelesen, danach wird der kopierte `ampr_emu.index` angepasst. Die losen Einträge in `ampr_assets.index` behalten die ELF-Größen (ps5-dump-forge tut es ebenso; der Leser vergleicht lose Dateien nicht).
- **zstd im Spielordner** (`imgjob_lz4.inc`, nach dem Entfernen der Originale; `sign` im Start): `ps5tm_fsign_replace` schreibt jede Programmdatei als `<Name>.ps5cc-sign-tmp`, liest sie gegen das Original zurück und benennt sie darüber um (Rechte und Zeiten bleiben), dann wird der `ampr_emu.index` für die fertigen Dateien angepasst; scheitert eine, bleiben die fertigen signiert und die übrigen, wie sie waren.
- **Pläne:** `ps5tm_fsign_info` (in `gameconvert.c`) liefert `unsigned_exec` für den Konvertierungsplan und den Packplan; Browser (`cvUnsigned`, `aprUnsigned` in `web/app.js`) und Vollbild-App (`Plan::unsigned_exec`, `GameExtras::sign_text`) zeigen es.
- **Tests** (Linux/WSL, gcc, ASan/UBSan): `sh test/fsign/run.sh` – `test.c` prüft Erkennung, Index, Stage, Links, Abbruch und 3000 beschädigte ELF-Dateien; `e2e.c` baut exFAT- und UFS2-Abbilder aus Spielordner plus Stage und sucht die Bytes; mit `MAKE_FSELF=<make_fself.py>` (ampr-emu-ng, `tools/make_fself.py`, nicht im Projekt) wird das Ergebnis Byte für Byte mit dem des Skripts verglichen, mit dem Modul `blake3` auch mit dem Prüfwert aus dem Test von ps5-dump-forge.

## Übersetzungen

Die Oberfläche gibt es auf Deutsch, Englisch, Italienisch, Spanisch, Französisch, Russisch und Schwedisch. Die Quelltexte sind deutsch und
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
  für PDF und HTML (der Ordner hat je Sprache einen Unterordner mit den Bildern; PDF daraus mit dem Druckdialog des Browsers oder mit
  `msedge --headless=new --print-to-pdf=<Datei>.pdf <Datei>.html`).
- **Demo-Modus und Bildschirmfotos (seit 21.11.2026):** `tools/demo/` stellt die Weboberfläche ohne Konsole bereit, mit erfundenen Daten:
  `python tools/demo/demo_server.py --lang de` bedient `web/` und `/api/v1/…` aus `tools/demo/fixtures/*.json` (Spiele heißen „Spiel 1 …“, die Cover
  zeichnet der Server, Uhr, Verlauf und Spielzeit laufen bis „jetzt“); `python tools/demo/make_shots.py [Sprache …]` öffnet damit die 14 Seiten des Handbuchs in
  jeder Sprache und schreibt `docs/shots/<xx>/NN-name.png` (1500 × 937; braucht Python mit playwright und Microsoft Edge, es wird nichts geladen).
  Die Fixtures entstehen aus Antworten einer echten Konsole: `tools/demo/record.py http://<Konsole>:8086` (nur lesend) und
  `tools/demo/make_fixtures.py <Ordner>` ersetzen jeden persönlichen Wert (Titel, Kennungen, Konto, Netz, Pfade); am Ende sucht das Skript jeden echten
  Wert in seinem Ergebnis und bricht ab, wenn einer übrig ist. Die Aufnahmen selbst gehören nicht ins Repository (`tools/demo/raw/`, in `.gitignore`).
  Eine Route ohne Fixture meldet der Server auf stderr (404); `qr.json` macht `tools/demo/qr_demo.c` mit dem QR-Kodierer der App.
- **Klasse `ps5`:** `web/theme-boot.js` setzt sie an `<html>`, wenn der Browser „PlayStation“ meldet. Sie gilt nur für PS5-eigene Dinge
  (Titelbilder der kompakten Ansicht, Zeichenbereich je Spielkarte, Scroll-Messung). Eine größere Schrift nur für die PS5 gab es kurz am
  10.11.2026 (erzeugter Block `PS5-TEXT`); sie wurde auf Wunsch des Users zurückgenommen, die Schrift ist dort wie überall.
- Die Meldungen, die die Konsole selbst auf dem Fernseher einblendet, sind deutsch (sie entstehen in `src/notify.c`, nicht im Browser).

## Aufbau des Quellcodes

```text
src/main.c          Start, Ablösung einer älteren Instanz, Serverschleife; Versionsmarke PS5CC-VERSION für die Kacheln
src/instlock.c      genau eine laufende App: Schreibsperre auf eine Datei im Datenordner, eine ältere Kopie wird beendet (ohne HTTP)
src/platform.c      Rechteausweitung, Temperatursensoren, ICC-Lüftersteuerung, Firmware
src/fan.c           Komfortregelung (Mittelwert, Totzone, Trend) und Servo auf die Lüfterschwelle
src/fanguard.c      Lüfterwächter: eigener Thread, liest die Sensoren selbst; hängt die Regelung oder wird es zu heiß, Lüfter zurück an die Konsole bzw. volle Kühlung
src/fantrace.c      Regelaufzeichnung der letzten 30 Minuten im Speicher (GET /api/v1/fan/trace, POST /api/v1/fan/trace/save)
src/probe.c         Hintergrund-Thread für alle Abfragen an Systemdienste
src/http.c          HTTP/1.1-Server (POSIX-Sockets, ohne Fremdbibliothek), Host-/Origin-Prüfung, Uploads direkt auf die Platte
src/assets.c        liefert die eingebetteten Dateien der Oberfläche aus, gzip wie gespeichert oder entpackt (libdeflate), je nach Accept-Encoding
src/api.c           REST-Schnittstelle unter /api/v1 (die Routen des Dateimanagers stehen in filemgr.c)
src/config.c        JSON-Konfiguration, atomares Speichern
src/log.c           Ereignis-Ringpuffer (gespiegelt ins Kernelprotokoll); Steuerzeichen einer Meldung werden zu Leerzeichen, damit aus einem Namen mit Zeilenumbruch keine zweite Zeile wird
src/klogown.h       erkennt Zeilen der App selbst im Kernel-Puffer; die Leser (Spiel, Takt, Controller, Mikrofon-Taste) überspringen sie
src/sysinfo.c       Modell, Laufzeit, Speicherplatz (zwischengespeichert)
src/netdisp.c       Netzwerkverbindung und angeschlossener Bildschirm
src/qr.c            QR-Code für die Adresse der Oberfläche (System-Seite); der Encoder stammt aus webhb von slopmaster33 (GPL-3.0)
src/telemetry.c     Stromschienen, Takt live, Bildrate
src/clocks.c        Taktgrenzen aus der Energiezustandstabelle der Konsole
src/chanlog.c       Aufzeichnung aller SoC-Sensorkanäle (zum Zuordnen)
src/history.c       Temperaturverlauf über einen Tag
src/thermalog.c     Langzeitauswertung der Kühlleistung (thermal-health.csv)
src/gamestate.c     Welches Spiel läuft und ob es im Vordergrund ist
src/dualsense.c     Controller-Akku aus dem Kernelprotokoll
src/micbutton.c     Doppeldruck der Mikrofon-Taste -> Temperaturmeldung
src/notify.c        Bildschirmmeldungen, Adresse im Heimnetz
src/power.c         Ausschalten, Neustart, Ruhemodus, abgesicherter Modus
src/powerguard.c    Ruhemodus-Uhr bei langen Vorgängen zurücksetzen
src/procmgr.c       Laufende Payloads auflisten und beenden
src/payloads.c      Payload-Dateien (interner Ordner, USB): starten, kopieren, löschen
src/payprofiles.c   Payload-Profile: benannte Folgen von Payloads mit Pausen, ein Startprofil, Abbruch zwischen zwei Einträgen
src/paystore.c      Payloads herunterladen: die Listen des Payload Managers von itsPLK (itsplk.github.io) und von strongt1me, Download mit SHA-256-Prüfung in den Payload-Ordner, installed.json, Aktualisieren/Löschen
src/upd_http.c      HTTP/1.1 über TCP oder TLS 1.2 (BearSSL, Vertrauensanker aus src/upd_ca.h, erzeugt mit tools/mk_upd_ta.py), Umleitungen nur nach Regel; upd_dns.c eigene DNS-Anfragen (UDP, TCP, DNS über HTTPS, gemerkte Adressen); beides aus PatchDL
src/profile.c       Anzeigename und Profilbild des Benutzers
src/account.c       Offline-Konto aktivieren (wie OffAct) und Fake-Anmeldung (np-fake-signin senden, Einträge leeren)
src/regstats.c      Konsolenname aus der Registry (nur lesend)
src/backdrops.c     Hintergrundbilder der Vollbild-App je Bereich (eigene JPEGs im Datenordner)
src/library.c       Spiele des Startbildschirms, Start, Cover; Quelle und Format je Spiel (ShadowMountPlus, PS4Mount, Kraken Mount)
src/libcache.c      Zwischenspeicher der Spieleliste (covers_and_more: meta.json + icon0.png je Titel), nur speichern und herausgeben; library.c prüft, was es davon glaubt
src/sqlite_ro.c     Nur-lesender SQLite-Leser für die App-Datenbank der Konsole
src/smp.c           Schnittstelle zu ShadowMountPlus (Port 10101; erkennt, wenn dort Kraken Mount antwortet)
src/smpcfg.c        config.ini von ShadowMountPlus: update_emulators bzw. update_enabled lesen und ändern (flaches und v2-Format)
src/smpremount.c    ein Spiel über ShadowMountPlus aus-, ein- oder neu einhängen
src/smphold.c       ein über ShadowMountPlus eingehängtes, nicht gestartetes Spiel wieder freigeben (smp-mounted.txt)
src/bgpause.c       Pausieren des Hintergrund-Spiels vor dem Antippen einer Kachel (17.11.2026): `ps5tm_bgpause_decide` (reine Entscheidung, Host-Test `test/bgpause`), `ps5tm_bgpause_tick` (aus der Probe-Schleife, zweimal pro Sekunde solange ein Spiel läuft), startet das Hilfsprogramm über `ps5tm_loader_inject_mem`; liest dessen Antwort aus `bgpause.res`
src/mounters.c      Titel, die zwei Einbinder zugleich führen (Kraken Mount, ShadowMountPlus, PS4Mount): die Warnliste conflicts
src/fpkgdup.c       ein Spiel, das Kraken Mount und ShadowMountPlus beide führen, nach dem Beenden bei ShadowMountPlus aushängen
src/kraken.c        KrakenMount: Kraken Mount (quer3q) eingebettet, starten und beenden, Seite, Suchordner (Kraken-Games; ohne ShadowMountPlus auch dessen Ordner), Löschen über Kraken Mount
src/krakenboot.h    wann Kraken Mount mit der App startet (Host-Test test/krakenboot)
src/krakenmig.h     Umzug FPKG-Games → Kraken-Games und die Ordnerlisten (Host-Test test/krakenmig)
src/dumpforge.c     Dump Forge (quer3q) als Motor von „FPKG (Debug-.pkg)“ und des Packens beim Konvertieren: eingebettet (zstd), beauftragt über 127.0.0.1:8095
src/lhttp.c         kleiner HTTP-Client für die Programme auf 127.0.0.1 (Kraken Mount 8097, Dump Forge 8095)
src/tilekeep.c      hält die eigene Kachel von Kraken Mount und Dump Forge fern (leere Datei an /user/app/<Titel-ID>)
src/gamecopy.c      Spiele kopieren (Ziele homebrew, Kraken-Games, PS5-Sicherung/Spiele; jede Datei zurücklesen, Prüfsummen-Datei)
src/gameconvert.c   Spiele konvertieren (exFAT, ffpfsc, ffpkg, FPKG über Dump Forge), Ergebnis zurücklesen und prüfen
src/convqueue.c     Warteschlange der Konvertierungen (eigener Thread, höchstens 24 Einträge, nur im Speicher)
src/cvname.c        Name des Ergebnisses aus einer Vorlage ({titel}, {id}, {version}, {quelle}); reine Funktionen, Host-Test test/cvname
src/cvpack.c        prüft die Wahl, ein Spiel beim Schreiben des Abbilds zu packen (pack, pack_level, pack_runtime)
src/origafter.c     „Original danach“: das Original nach Konvertieren, Kopieren oder Entpacken behalten, löschen oder verschieben
src/fsign.c         Unsignierte PS5-Programmdateien als fake-signierte SELF-Dateien (FSELF) schreiben: beim Konvertieren und beim Packen in einen neuen Ordner
src/gamemove.c      Verschieben und Entpacken über ShadowMountPlus; scheitert dessen Entpacken an EACCES, entpackt die App selbst
src/gamedelete.c    Spiele und Sicherungen löschen: Plan mit Einmal-Kennwort, Deinstallation über die Konsole (Helfer), ShadowMountPlus abmelden + Abbild löschen, Spiele von Kraken Mount über Kraken Mount, von PS4Mount über PS4Mount, Sicherungen; löscht nie über Verknüpfungen, Laufwerksgrenzen oder außerhalb von /mnt/usb*, /mnt/ext*, /data
src/filemgr.c       Dateimanager: ansehen, laden, hochladen, kopieren, verschieben, umbenennen, löschen; ändern nur unter den Laufwerken und /data, nie über Verknüpfungen
src/fm_ntfs.inc     NTFS-Laufwerke im Dateimanager (in filemgr.c eingebunden): Knoten Konsole/NTFS, Liste, Ansicht, Hochladen, Kopieren/Verschieben/Löschen, Auswerfen; nie ersetzen oder umbenennen
src/fm_search.inc   Suche des Dateimanagers (eigener Thread, auch auf NTFS)
src/fm_title.inc    Spieltitel zu Einträgen, die nach einer Titel-ID heißen (Ordnerliste und Suche)
src/ntfsvol.c       NTFS-Laufwerke finden (ganze Platten daN ohne Einhängung, höchstens alle 3 s, nie beim Auswerfen), ntfs0..7, nur-lesbar-Gründe, Auswerfen
src/third_party/ntfsb/  NTFS-Treiber aus PS5-NTFS-BROWSER v1.4.0 (abcde6543, GPL-3.0-or-later); die Änderungen (Prüfungen, Schreib-Cache, Schreibreihenfolge) nennt ORIGIN.md dort
src/usbeject.c      USB-Laufwerk sicher auswerfen über ShadowMountPlus: nur ohne laufenden Auftrag, ohne PS4Mount-Spiel und ohne Abbild von Kraken Mount auf dem Laufwerk
src/drivegate.c     Laufwerks-Tor: Lesezugriffe der App melden sich an, ein Auswerfen wartet auf sie (bis 4 s) und lässt keine neuen zu
src/driveholders.c  wer Dateien, Ordner oder sein Arbeitsverzeichnis auf einem Laufwerk offen hat (sysctl kern.proc.filedesc, nur lesend)
src/ejectmark.c     Marke eines Auswerfens, das nicht zurückkam: danach bleibt das Auswerfen aus, bis es wieder erlaubt wird (rearm)
src/jobreg.c        die eine Liste der Aufträge, die nicht nebeneinander laufen (ps5tm_jobs_busy_except)
src/jobhist.c       Verlauf der Aufträge (history.jsonl): eine Zeile je beendetem Kopieren, Konvertieren, Installieren, Löschen, Dateimanager-Auftrag usw.
src/imgread.c       nur-lesender Leser für exFAT-, PFS/PFSC- (.ffpfsc) und UFS2-Abbilder (.ffpkg); alles gegen die Größen der Köpfe geprüft, Fehler statt wilder Lesezugriffe
src/imgjob.c        Abbild prüfen, in einen Ordner entpacken und reparieren (mit imgjob_lz4.inc für die LZ4- und zstd-Pack-Aufträge)
src/amprindex.c     AMPR-Index (ampr_emu.index, AMPRIDX3) eines Spiels bauen: C-Portierung von build_ampr_index.py aus drakmors ampr_emu
src/indexlimit.c    passen die Tabellen eines zstd- oder LZ4-Packs in den Indexspeicher von AMPR EMU NG (index_check)
src/amprlib.c       APR-EMU- und PlayGo-Datei eines Spiels: Fassung wählen, einsetzen, entfernen, das Original zurücklegen; die Fassungen sind eingebettet (gen/*_blob.c)
src/amprng.c        AMPR EMU NG (quer3q): die zwei angebotenen Fassungen, eine neuere stabile Veröffentlichung von GitHub laden
src/blobz.c         entpackt die als zlib-Strom eingebetteten Programme (Hilfsprogramme, np-fake-signin, Kraken Mount)
src/lz4pack.c       LZ4-Asset-Packs eines Spiels (AMPRPAK4/AMPRDAT3): packen, prüfen, entpacken (mit lz4pack_write.inc und lz4pack_read.inc)
src/zstdpack.c      zstd-Asset-Packs eines Spiels: packen, prüfen, entpacken
src/lz4cfg.c        Packprofil der LZ4-Packs: eingebaute Regeln und ein Leser für die TOML-Profile von drakmors ampr_pack.py
src/plugins.c       Spiel-Plugins (PlayGo-Stub, DLC-Module von drakmor) im fakelib eines Spiels aktuell halten
src/trace.c         Mitschnitt: ein Profil für die LZ4-Packs aus dem echten Leseverhalten eines Spiels (Debug-APR-EMU, Journal AMPRCMD1)
src/conv_exfat.c    exFAT-Abbild schreiben (aus MkPFS portiert)
src/conv_pfs.c      PFS-Container mit PFSC-Kompression (aus MkPFS portiert), vollständige Prüfung
src/conv_ufs2.c     UFS2-Abbild (ffpkg) nach dem Vorbild von UFS2Tool, Prüfsumme je Datei
src/checkfile.c     Zurücklesen mit CRC-32/SHA-256, Prüfsummen-Datei im sha256sum-Format
src/sha256.c        SHA-256 (eigen): reines C, dazu die SHA-Befehle der CPU (CPUID + Selbsttest, Test: test/sha256/run.sh)
src/iopolicy.c      Lesen und Schreiben gleichzeitig nur zwischen zwei Laufwerken, Vorauslesen
src/ioerr.h         die Worte für einen gescheiterten Zugriff: abgemeldetes Laufwerk (ENXIO/ENODEV), EIO, sonst strerror()
src/klog.c          Kernel-Log live: Kopie von kern.msgbuf, neue Zeilen über einen Anker aus den letzten sechs Zeilen
src/klogfiles.c     Kernel-Log in Dateien: Auszüge von der Seite, Aufnahme (eigener Thread liest ps5tm_klog_fetch), Ordner klog-live-log leeren
src/playtime.c      Spielzeit: Sitzungen je Spiel mit Temperaturen (sessions.csv, session-open.csv), eigener Thread
src/pkgparse.c      Paket-Dateien lesen (PS4 CNT, PS5 FIH, geteilte PS5MPKG1): Titel, Version, Art, Bild; jede Angabe der Datei wird gegen ihre Größe geprüft
src/pkgscan.c       Pakete suchen (Wurzel und Ordner pkg der Laufwerke, im Hintergrund), Liste mit Kennungen, geteilte Pakete als ein Eintrag
src/pkgsplit.c      ein Paket in Teile (PS5MPKG1) aufteilen: Zwischennamen, Zurücklesen mit SHA-256, Namen erst am Ende, Abbruch räumt auf
src/pkglive.c       Paket direkt vom PC: der Browser schickt Stücke zu 1 MB, die Konsole installiert, ohne dass vorher etwas auf ein Laufwerk geschrieben wird
src/pkgqueue.c      Warteschlange: mehrere Pakete nacheinander installieren; jedes wird erst an seiner Reihe befragt
src/pkgstream.c     kleiner HTTP-Server nur auf 127.0.0.1:18851: liefert das Paket (ein geteiltes als ein Strom) mit Range an die Installation der Konsole, nur den einen gepinnten Namen
src/pkginstall.c    Paket installieren: Plan, ein Auftrag je Installation, Hilfsprogramm starten und befragen, Fertig erst bei Systemstatus + alle Bytes, danach Suche in der Datenbank (vier Enden: installiert, als fertig gemeldet, geliefert ohne Bestätigung, fehlgeschlagen); die Verbindung zum Hilfsprogramm liegt ab fd 200
src/pkginst.h       Aufteilung des Pakets in Stücke (Slices) und der Server; src/pkginst_ipc.h die Nachrichten zwischen App und Hilfsprogramm
src/helper/bgpause_helper.c  das Hilfsprogramm zu bgpause.c (eigene ELF, eingebettet; `gen/bgpause_blob.c`): liest die Anwendungs-ID aus `bgpause.req`, ruft `sceLncUtilSuspendApp` nur für die laufende große Anwendung gleicher ID; baut ohne `-fno-asynchronous-unwind-tables`, `tools/check_elf_pages.py` prüft die Seitenlage der Segmente
src/helper/pkginst_helper.c  das Hilfsprogramm (eigene ELF, in die App eingebettet): ruft sceAppInstUtil* auf; höchstens ein Installationsaufruf je Prozess, fragt nach dem Fortschritt (auch als Ersatz für ein gestorbenes); beendet die Bibliothek nie, steigt mit _exit aus; gibt sceAppInstUtilGetInstallStatus einen eigenen 16-KiB-Puffer (die Funktion schreibt 712 Bytes, das bekannte Feld hat 600) und hält die Verbindung zur App ab fd 100
src/savebackup.c    Spielstände: sichern, prüfen, zurückspielen (rohe Kopie mit Zurücklesen, Liste mit SHA-256, Stand vor dem Zurückspielen), ein Thread je Vorgang
src/savemnt.c       einen Spielstand der Konsole öffnen (über die Funktionen der Konsole; die App entschlüsselt nichts selbst)
src/savexport.c     einen Spielstand als ZIP aus Klartext-Dateien auslesen
src/saveimport.c    einen Spielstand aus einer solchen ZIP einspielen (neues Abbild, Stand davor gesichert)
src/savezip.c       die ZIP-Dateien der Spielstände lesen und schreiben
src/chives.c        chives.json: wie das Abbild eines ausgelesenen Spielstands gebaut war (liegt in der ZIP neben den Dateien)
src/savesfo.c       die param.sfo eines Spielstands
src/savedb.c        der Eintrag eines eingespielten Spielstands in der Spielstand-Datenbank der Konsole (SQLite)
src/ps4m_core.c     PS4Mount 0.8.0 (gasb07, GPL-3.0) in der App: PS4-Fake-PKGs auf USB erkennen, ohne Installation einbinden, zurückverschieben; ps4m_pkg.c, ps4m_build.c, ps4m_crypto.c, ps4m_fw.c, ps4m_origin.c, ps4m_pkgindex.c (PKG-Liste je Laufwerk), ps4m_accopy.c (Zusatzinhalte kopieren), ps4m_klogdup.c (wiederholte Kernel-Zeilen zusammenfassen) und ps4m_keys.h gehören dazu (Quelle: third_party/ps4mount/)
src/dupimg.c        doppelte Abbilder eines Spiels finden und beiseitelegen (`….off`), nach exFAT2AMPR 1.104 (V-10)
src/disc.c          Das Laufwerk (nach ps-disc-detect, GPL-3.0): CAM-Durchreichgerät, Disc-Profil, Titelkennung, Abbild einer PS2-DVD/PS3-Disc mit CRC-32/MD5/SHA-1, optionale Redump-Liste (sqlite_ro.c liest ihre WITHOUT-ROWID-Tabellen)
src/dump_lib.c      Reiter Disc: Marken der Spieleliste und Suche nach vorhandenen Dumps (die dump_*.c nach ps5-app-dumper 2.10, GPL-3.0-or-later)
src/dump_job.c      ein laufendes PS5-Spiel samt eingehängter DLC dumpen
src/dump_queue.c    Warteschlange der Dumps: bis zu 64 Titel nacheinander starten und dumpen
src/dump_self.c     SELF/SPRX-Dateien eines Dumps entschlüsseln
src/dump_fself.c    eine einfache ELF als FSELF schreiben (Einstellung „als FSELF neu signieren“)
src/dump_backport.c SDK-Version der Dateien eines Dumps senken (Backport)
src/dump_unpkg.c    die sce_sys-Dateien aus einem installierten app.pkg oder ac.pkg holen
src/dump_ps4.c      PFS-Abbild eines PS4-Spiels entpacken
src/dump_image.c    was ein PS4-Spielordner für ein Abbild braucht (nach PS4FFPFSC 0.2.9); heute vom Konvertieren der PS4-Ordner benutzt
src/tile.c          Kachel PSCC69690 für eine ELF ohne die Kacheln FAKE86921/FAKE86922: Ordner aus eingebetteten Dateien über AppInstUtil, ein vorhandenes Paket als Rückfall
src/dynsym.c        Symbolsuche in Systemmodulen
src/sony_api_lock.c gemeinsame Sperre für empfindliche Systemaufrufe
src/third_party/    cJSON, libdeflate, lz4, zstd, BearSSL, SQLite, ntfsb (siehe THIRD_PARTY_NOTICES.md)
third_party/        eingebettete Fassungen fremder Programme: ampr_emu (drakmor, AMPR EMU NG von quer3q), dlc_emu und playgo (drakmor), ps4mount (gasb07: Vorlagen und Tests), kraken_mount und dump_forge (quer3q), np_fake_signin (earthonion), lz4_profiles; patchdl nur als Herkunftsnachweis
web/                Web-Oberfläche (per tools/gen_assets.py eingebettet, gzip), Profilbilder in web/avatars/, Wörterbücher in web/lang/
tile/               Dateien der Kachel PSCC69690 (sce_sys), eingebettet über tools/gen_tile_files.py
starter/            Browser-Kachel FAKE86922 (starter/browser/) und die Hintergründe beider Kacheln (starter/sce_sys/)
tvapp/              Vollbild-App, Kachel FAKE86921 (auf ps5-homebrew-ui und ps5-opengl), mit eigenen Tests
tools/              Bau-, Release-, Sende- und Prüfwerkzeuge (nächster Abschnitt)
test/               Host-Tests (je Ordner ein run.sh) und die Regelkreis-Simulation test/sim
docs/               Handbuch, API, Entwicklung, Versionshinweise, Entscheidungsprotokoll
```

## Werkzeuge (`tools/`)

```text
tools/build-windows.sh         Bau unter Windows ohne WSL (Git Bash, GNU Make, LLVM 21); „release“ legt den Release-Ordner an, „clean“ räumt auf
tools/win/                     prospero-clang, llvm-config und rev für Git Bash (von build-windows.sh benutzt)
tools/check_sdk_firmware.py    prüft das crt1.o des SDK bzw. eine fertige ELF auf die Firmware-Fälle (FW_NEED im Makefile)
tools/check_elf_pages.py       prüft, dass jedes ladbare Segment einer Payload-ELF auf einer 16-KiB-Seite beginnt (Hilfsprogramm bgpause)
tools/check_fw_imports.py      sucht die Systemfunktionen der App in den Exporten entschlüsselter Firmware-Bibliotheken (siehe oben)
tools/build_date.py            Baudatum für das Makefile; tools/read_version.py die Version aus src/ps5tm.h
tools/gen_assets.py            bettet web/ gzip-komprimiert ein (gen/assets.c)
tools/gen_tile_files.py        bettet tile/sce_sys ein (gen/tile_files.c)
tools/gen_pkg_blob.py          bettet ein Kachel-Paket ein, nur mit make TILE_PKG=…; sonst ein leeres Feld
tools/gen_blob.py              macht aus einer ELF ein C-Feld (zlib-Strom): beide Hilfsprogramme, np-fake-signin, Kraken Mount
tools/gen_zstd_blob.py         bettet einen schon gepackten zstd-Rahmen ein (Dump Forge)
tools/gen_emu_blob.py          bettet die APR-EMU-Fassungen, die PlayGo-Stubs und die DLC-Module aus third_party/ ein
tools/gen_lz4_profiles.py      bettet die erprobten LZ4-Profile aus third_party/lz4_profiles ein
tools/gen_p4m_templates.py     die PlayGo-Vorlagen von PS4Mount als C-Felder (gen/pdb_template.h)
tools/patch_kraken.py          die drei Änderungen an Kraken Mount: kein Warten auf ShadowMountPlus, keine eigene Update-Abfrage, Port 18097 statt 10101
tools/fetch-dumpforge.sh       lädt die festgelegte Fassung von Dump Forge und prüft die SHA-256; tools/pack-dumpforge.sh legt sie als zstd-Rahmen nach third_party/dump_forge
tools/mk_upd_ta.py             schreibt src/upd_ca.h (BearSSL-Vertrauensanker)
tools/gen_theme_hell.py        Textfarben des hellen Designs in web/style.css
tools/build_avatars.py         die 30 eingebauten Profilbilder (web/avatars); tools/build_avatar_thumbs.py ihre Vorschauen
tools/build_tile_assets.py     Bilder der Kachel PSCC69690 aus den Vorlagen in pkg/ (der Ordner gehört nicht zum Repository)
tools/png_to_bc7_dds.py        Hintergrund einer Kachel als BC7-DDS, 3840 × 2160 (pic0.dds, pic1.dds)
tools/optimize_png.py          PNG-Dateien kleiner, ohne ein Pixel zu ändern (zopfli)
tools/make_starter.sh          baut beide Kacheln FAKE86921 und FAKE86922; tools/tvapp-build.sh baut darin die Vollbild-App unter WSL
tools/release.sh               die Veröffentlichung (siehe oben)
tools/make_payload_json.py     payload-manager.json: die Liste dieser App für den Payload Manager von itsPLK
tools/pre-release-check.ps1    ältere Prüfung vor einem Release (Bau unter WSL, Dateien im Release-Ordner)
tools/check-release-liesmich-sync.ps1  vergleicht den Abschnitt KURZDIAGNOSE der LIESMICH in den Release-Ordnern mit docs/LIESMICH.txt
tools/run-host-tests.sh        alle Host-Tests der CI-Liste und die Simulation (siehe Host-Tests)
tools/deploy.sh                ELF und Kacheln an eine Konsole; tools/lib/send_elf.ps1 schickt die ELF an Port 9021
tools/diagnose.ps1             von Windows aus: läuft die App, antwortet Port 8086, wirkt der Lüfterpfad
tools/upstream.json            die Fremdbausteine und ihre eingebauten Fassungen (Feld current); tools/upstream_check.py fragt GitHub nach neueren (Skill upstream-update)
tools/i18n/                    Wörterbücher prüfen, Texte ziehen, Handbuch und FAQ bauen (siehe Übersetzungen)
tools/demo/                    Vorführung ohne Konsole und Bildschirmfotos fürs Handbuch (siehe Übersetzungen)
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
