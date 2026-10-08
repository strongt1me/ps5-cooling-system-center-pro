# Fremde Bestandteile

PS5 Cooling & System Center - Pro steht unter der GNU GPL, Version 3 oder später
(siehe [LICENSE](LICENSE)). Diese Teile stammen aus anderen Projekten; ihre
Lizenzen sind mit der GPL-3.0 verträglich und gelten für die jeweiligen
Dateien weiter. Den Dank an die Urheber und eine verständliche Beschreibung
ihrer Beiträge gibt es in der [README](README.md#credits-und-danksagung).

## Übernommener oder portierter Code

| Bestandteil | Wo | Lizenz | Herkunft |
|---|---|---|---|
| **MkPFS** (RenanGBarreto, rdmrocha und Mitwirkende, PSBrew) — exFAT-Schreiber, PFS-/PFSC-Aufbau, exFAT-Großschreibtabelle, nach C übertragen | `src/conv_exfat.c`, `src/conv_pfs.c`, `src/conv_exfat_upcase.inc` | GPL-3.0 | <https://github.com/PSBrew/MkPFS>, Stand Commit `78eda0a` |
| **UFS2Tool** (SvenGDK) — dessen Aufbau eines UFS2-Abbilds (Superblock, Zylindergruppen, Inodes, Verzeichniseinträge, Wiederherstellungsblock; `newfs -D` mit den Parametern, die ShadowMountPlus empfiehlt) als Vorlage für einen eigenen UFS2/ffpkg-Schreiber nach C übertragen; nichts davon wird mitgeliefert oder verteilt, nur die Formatkenntnis daraus portiert | `src/conv_ufs2.c` | BSD-2-Clause | <https://github.com/SvenGDK/UFS2Tool> |
| **ps5upload** (phantomptr) — Profilfunktion (lokalen Anzeigenamen lesen und ändern, Profilbild-Dateien einsetzen) nach C übertragen; Format, Größen und Vorlage der Profilbild-Dateien im Browser-Teil | `src/profile.c`, Profilbild-Teil von `web/app.js` | GPL-3.0 (README und LICENSE des Projekts; GitHub erkennt die Datei nicht automatisch) | <https://github.com/phantomptr/ps5upload> |
| **ps5-pkg-manager** 1.4.1 (itsPLK) — Aufbau der PS4-/PS5-Pakete (Kopf, Tabelle, `param.json`/`param.sfo`, Bild), das Teile-Format `PS5MPKG1` und der Ordnerplan der Suche; für das Installieren der Ablauf (die Konsole liest das Paket von einem kleinen Server auf der eigenen Adresse, ein eigener Prozess ruft die Installationsbibliothek AppInstUtil auf und fragt den Stand ab, die Wiederholung bei kurzzeitigen Fehlern) und der Aufbau der Strukturen, die diesem Aufruf übergeben werden. Die Programme `pkgparse.c`, `pkgscan.c`, `pkgsplit.c`, `pkgstream.c`, `pkginstall.c` und das Hilfsprogramm `helper/pkginst_helper.c` sind eigener Code nach diesem Wissen und nach dem öffentlich beschriebenen Paketformat; nichts davon ist eingebettet oder mitgeliefert (das Hilfsprogramm ist unser eigenes und steckt als Bytefolge in der App) | `src/pkgparse.c`, `src/pkgscan.c`, `src/pkgsplit.c`, `src/pkgstream.c`, `src/pkginstall.c`, `src/helper/pkginst_helper.c` | GPL-3.0 | <https://github.com/itsPLK/ps5-pkg-manager>, Version 1.4.1 |
| **webhb** 0.4.1 (slopmaster33, aus ps5-app-dumper von EchoStretch gelöst) — der QR-Encoder in `src/qr.c` (Byte-Modus, Stufe M, Version 1 bis 6) ist daraus übernommen, GPL-3.0-or-later wie diese App; geändert nur der Name der Funktion und der Kopf. <https://github.com/slopmaster33/webhb> |
| **libdeflate** 1.26 (Eric Biggers, Google LLC) — zlib-Kompression und -Dekompression sowie CRC-32, unverändert bis auf eine ergänzte Datei: `lib/crc32.c` (der Einstieg `libdeflate_crc32()`, eigen geschrieben nach dem Muster von `adler32.c`; die Tabellen und Routinen darunter sind libdeflates; siehe `README.PS5TM` dort) | `src/third_party/libdeflate/` | MIT (`COPYING` dort) | <https://github.com/ebiggers/libdeflate>, Tag `v1.26` |
| **cJSON** (Dave Gamble und Mitwirkende) | `src/third_party/cJSON*` | MIT | <https://github.com/DaveGamble/cJSON> |
| **PS5-Payload-SDK** v0.43 (John Törnblom und Mitwirkende) — Werkzeugkette und Laufzeit, mit der jedes ELF gebaut wird; mindestens v0.42 nötig (Firmware 13.60), der Bau prüft das | nicht im Quellbaum; `PS5_PAYLOAD_SDK/` beim Bauen | GPLv3+ (Kopfdateien unter `include/freebsd`: BSD) | <https://github.com/ps5-payload-dev/sdk> |

## Ohne übernommenen Code, nur als Vorbild, Schnittstelle oder Wissensquelle genutzt

- **ShadowMountPlus** (drakmor, GPL-3.0): Seine Web-Schnittstelle liefert Format
  und Speicherort der Spiele und übernimmt Verschieben und Entpacken
  (`src/smp.c`, `src/gamemove.c`); seine README nennt die Parameter für
  `.ffpkg`-Abbilder (`src/conv_ufs2.c`). <https://github.com/drakmor/ShadowMountPlus>
- **exFAT Image Builder** (kerrdec97): zeigt, mit welchen UFS2Tool-Parametern ein
  `.ffpkg` für ShadowMountPlus gebaut wird (`-b 65536 -f 65536 -m 0 -S 512 -i 262144`);
  nur Wissen, nichts davon übernommen (`src/conv_ufs2.c`).
  <https://github.com/kerrdec97/ps5-exfat-builder>
- **ps5-unified-autoloader** (itsPLK): der Weg, den Browser der Konsole über
  `sceShellUIUtilLaunchByUri("pshomeui:navigateToHome…")` zu schließen
  (`src/library.c`). <https://github.com/itsPLK/ps5-unified-autoloader>
- **ps5-payload-dev/websrv** (John Törnblom): der Startweg über
  `sceSystemServiceLaunchApp` (`src/library.c`).
  <https://github.com/ps5-payload-dev/websrv>
- **ps5-payload-manager** (itsPLK): die Regel, welche Prozesse als Payload gelten
  und beendet werden dürfen (`src/procmgr.c`), und das Vorbild für den Aufbau der
  Seite „Payloads“. <https://github.com/itsPLK/ps5-payload-manager>
- **ps-game-state-lib** (StonedModder, MIT): die Muster im Kernelprotokoll, an
  denen sich das laufende Spiel erkennen lässt (`src/gamestate.c`). Der Code ist
  eigen; es wurden nur diese Muster übernommen.
  <https://github.com/StonedModder/ps-game-state-lib>
- **ps5-hwinfo** (drakmor, GPL-3.0): Fakten, keine Zeilen Code — Reihenfolge der
  Stromschienen, Bedeutung der Leerlauf-Schiene des Prozessors und die Art, die
  Taktwerte zu lesen (`src/telemetry.c`, `src/platform.c`).
  <https://github.com/drakmor/ps5-hwinfo>
- **etaHEN** und **onionHEN** (aydencharles, GPL-3.0): Wissen über Systemaufrufe,
  Messwerte und die Abfrage der Bildrate über `/dev/dce`, nachgelesen in deren
  Quelltext und Dokumentation (`src/platform.c`, `src/sysinfo.c`,
  `src/clocks.c`, `src/telemetry.c`, `src/chanlog.c`, `src/dualsense.c`).
  <https://github.com/etaHEN/etaHEN>, <https://github.com/aydencharles/onionHEN>
- **PS5 Game Compressor** (Juma Sayeh, getestet von Osama Abualia, Version 1.1.1;
  der Quelltext trägt keine Lizenz, deshalb ist **nichts davon übernommen**):
  Ideen, die hier neu geschrieben sind — die Prüfung des SDK auf die
  Firmware-13.60-Unterstützung beim Bau (`tools/check_sdk_firmware.py`), das
  vollständige Zurücklesen und Prüfen von Sicherungen mit Prüfsummen
  (`src/checkfile.c`, `src/gamecopy.c`, `src/gameconvert.c`,
  `src/conv_pfs.c`), das Zurücksetzen der Ruhemodus-Uhr bei langen Vorgängen
  (`src/powerguard.c`; die Aufrufe selbst stammen aus dem SDK und wurden vorher
  mit einem Testprogramm an der Konsole geprüft), gleichzeitiges Lesen und
  Schreiben nur zwischen verschiedenen Laufwerken (`src/iopolicy.c`), Nullen
  statt Dateilöcher im Container und die 5-%-Regel für komprimierte Blöcke
  (`src/conv_pfs.c`).
- **Elf Arsenal**: die Form der Aufrufe zur Kachel-Installation (Aufbau der
  Parameter von `sceAppInstUtilAppInstallPkg`), `src/tile.c`.
- **BackPork** (BestPig, GPL-3.0): das Prinzip der Ersatzbibliotheken, das die
  Spieleseite nur erkennt und anzeigt (`src/library.c`).
  <https://github.com/BestPig/BackPork>
- **Linux, Treiber `hid-playstation`** (GPL-2.0): Dokumentation des Statusbytes
  des DualSense für den Akkustand (`src/dualsense.c`).

## Zur Laufzeit gebraucht, nicht enthalten

- **elfldr** (ps5-payload-dev): der Payload-Lader auf Port 9021, über den das
  Programm und auch die Seite „Payloads“ Dateien starten.
  <https://github.com/ps5-payload-dev/elfldr>
- **kstuff**: Voraussetzung für den Zugriff auf den Lüfter-Controller.
- **ShadowMountPlus**: optional, siehe oben.
