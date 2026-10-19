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
| **UFS2Tool** v4.1 (SvenGDK) — dessen Aufbau eines UFS2-Abbilds (Superblock, Zylindergruppen, Inodes, Verzeichniseinträge, Wiederherstellungsblock; `newfs -D` mit den Parametern, die ShadowMountPlus empfiehlt) als Vorlage für einen eigenen UFS2/ffpkg-Schreiber nach C übertragen; nichts davon wird mitgeliefert oder verteilt, nur die Formatkenntnis daraus portiert | `src/conv_ufs2.c` | BSD-2-Clause | <https://github.com/SvenGDK/UFS2Tool> |
| **ps5upload** (phantomptr) — Profilfunktion (lokalen Anzeigenamen lesen und ändern, Profilbild-Dateien einsetzen) nach C übertragen; Format, Größen und Vorlage der Profilbild-Dateien im Browser-Teil | `src/profile.c`, Profilbild-Teil von `web/app.js` | GPL-3.0 (README und LICENSE des Projekts; GitHub erkennt die Datei nicht automatisch) | <https://github.com/phantomptr/ps5upload> |
| **ps5-pkg-manager** 1.5.1 (itsPLK) — Aufbau der PS4-/PS5-Pakete (Kopf, Tabelle, `param.json`/`param.sfo`, Bild), seit 1.5.1 dazu die Erkennung der Rückportier-Patches (Kopf `LIH`, Inhaltsmarken `0x40100000`) und das Lesen der SDK-Version des Programms, das Teile-Format `PS5MPKG1` und der Ordnerplan der Suche; für das Installieren der Ablauf (die Konsole liest das Paket von einem kleinen Server auf der eigenen Adresse, ein eigener Prozess ruft die Installationsbibliothek AppInstUtil auf und fragt den Stand ab, die Wiederholung bei kurzzeitigen Fehlern) und der Aufbau der Strukturen, die diesem Aufruf übergeben werden. Die Programme `pkgparse.c`, `pkgscan.c`, `pkgsplit.c`, `pkgstream.c`, `pkginstall.c` und das Hilfsprogramm `helper/pkginst_helper.c` sind eigener Code nach diesem Wissen und nach dem öffentlich beschriebenen Paketformat; nichts davon ist eingebettet oder mitgeliefert (das Hilfsprogramm ist unser eigenes und steckt als Bytefolge in der App) | `src/pkgparse.c`, `src/pkgscan.c`, `src/pkgsplit.c`, `src/pkgstream.c`, `src/pkginstall.c`, `src/helper/pkginst_helper.c` | GPL-3.0 | <https://github.com/itsPLK/ps5-pkg-manager>, Version 1.5.1 |
| **webhb** 0.4.1 (slopmaster33, aus ps5-app-dumper von EchoStretch gelöst) — der QR-Encoder in `src/qr.c` (Byte-Modus, Stufe M, Version 1 bis 6) ist daraus übernommen, GPL-3.0-or-later wie diese App; geändert nur der Name der Funktion und der Kopf. | `src/qr.c` | GPL-3.0-or-later | <https://github.com/slopmaster33/webhb> (seit Oktober 2026 nicht mehr erreichbar) |
| **libdeflate** 1.26 (Eric Biggers, Google LLC) — zlib-Kompression und -Dekompression sowie CRC-32, unverändert bis auf eine ergänzte Datei: `lib/crc32.c` (der Einstieg `libdeflate_crc32()`, eigen geschrieben nach dem Muster von `adler32.c`; die Tabellen und Routinen darunter sind libdeflates; siehe `README.PS5TM` dort) | `src/third_party/libdeflate/` | MIT (`COPYING` dort) | <https://github.com/ebiggers/libdeflate>, Tag `v1.26` |
| **ampr_emu** (drakmor) — das Format des AMPR-Index (`AMPRIDX3`) und die Bauregeln aus `build_ampr_index.py`, nach C übertragen | `src/amprindex.c` | GPL-3.0 | <https://github.com/drakmor/ampr_emu> |
| **BearSSL** 0.6 (Thomas Pornin) — TLS-Client der Spiel-Updates, unverändert (nur `src/` und `inc/` des Archivs) | `src/third_party/bearssl/` | MIT (`LICENSE.txt` dort) | <https://bearssl.org/>, Archiv `bearssl-0.6.tar.gz` |
| **cJSON** 1.7.19 (Dave Gamble und Mitwirkende) | `src/third_party/cJSON*` | MIT | <https://github.com/DaveGamble/cJSON> |
| **PS5-Payload-SDK** v0.44 (John Törnblom und Mitwirkende) — Werkzeugkette und Laufzeit, mit der jedes ELF gebaut wird; mindestens v0.42 nötig (Firmware 13.60), der Bau prüft das | nicht im Quellbaum; `PS5_PAYLOAD_SDK/` beim Bauen | GPLv3+ (Kopfdateien unter `include/freebsd`: BSD) | <https://github.com/ps5-payload-dev/sdk> |

## Eingebettete Fassungen, einzelne übernommene Dateien, Vorbilder und Schnittstellen (bei jedem Eintrag steht, was genau übernommen ist)

- **ampr_emu** (drakmor, GPL-3.0): Fünf Bauten der APR-EMU-Dateien (`libSceAmpr.sprx`, Fassungen 0.3.6.6 und
  0.4.2.1, GPL-3.0, siehe `third_party/ampr_emu/ORIGIN.md`) sind gepackt in die App eingebettet; sie
  kommen nur auf Wunsch in den Ordner `/data/PS5-Cooling-Center/AMPR_EMU` oder in ein Spiel
  (`src/amprlib.c`). Quelltext der Programme: das Projekt unten. Fassungen und Bauarten (`test-pack`,
  `test-nopack`, `test-debug-pack`, `test`, `test-debug`) nach <https://github.com/drakmor/ampr_emu>.
- **pgo_stub** (drakmor, GPL-3.0): Zwei Bauten des PlayGo-Stubs (`libScePlayGo.sprx`, Fassung 0.6, mit und ohne
  Protokoll, siehe `third_party/playgo/ORIGIN.md`) sind gepackt in die App eingebettet; sie kommen nur auf Wunsch in den
  Ordner `/data/PS5-Cooling-Center/PlayGo` oder in ein Spiel (`src/amprlib.c`). Quelltext der Programme:
  <https://github.com/drakmor/pgo_stub>.
- **dlc_emu** (drakmor, GPL-3.0): Drei Bauten der DLC-Module (`libSceAppContent.sprx`, `libSceGameUpdate.sprx`,
  `libSceNpEntitlementAccess.sprx`, Fassung 0.3, „log“, siehe `third_party/dlc_emu/ORIGIN.md`) sind gepackt in die App
  eingebettet; sie kommen nur in ein Spiel, dessen Ordner eine `dlc_emu.ini` enthält (`src/plugins.c`). Quelltext der Programme:
  <https://github.com/drakmor/dlc_emu>.
- **ps5-native-app-boilerplate** (BlackBearReloaded, GPL-3.0-or-later): Für die Starter-Kachel (`starter/`) sind das Linker-Skript
  (`starter/title/title.ld`), die Symboltabelle (`starter/title/symbols.map`) und `starter/sce_module/libc.prx` (Kompatibilitäts-Stubs
  und Startcode, kein Sony-Code, siehe `starter/sce_module/NOTICE.txt`) übernommen; der Bauweg (freistehendes Programm, `ps5-native-tool`,
  Signatur) folgt dem Projekt. <https://github.com/BlackBearReloaded/ps5-native-app-boilerplate>
- **PS4Mount** (gasb07, GPL-3.0-or-later, Version 0.6.0): Für den Reiter „PS4 von USB“ (PS4-Fake-PKGs vom USB-Laufwerk ohne Installation) ist der
  Quelltext übernommen: Paket lesen und Schlüssel (`src/ps4m_pkg.c`, `src/ps4m_keys.h`), Erzeugen der Installationsdateien
  (`src/ps4m_build.c`), Kryptographie (`src/ps4m_crypto.c`), Firmware-Weiche (`src/ps4m_fw.c`), Herkunft verschobener Pakete (`src/ps4m_origin.c`) und der Kern mit Suche, Anmeldung, nullfs-Einbindung und Löschfenster
  (`src/ps4m_core.c`, angepasst: als Faden der App statt als eigenes Payload). Die Funktionen heißen mit dem Vorsatz `p4m_`; Lizenztext und
  Vorlagen unter `third_party/ps4mount/`. Der Schlüssel in `src/ps4m_keys.h` stammt aus LibOrbisPkg (maxton, LGPL-3.0). Quelltext der
  Vorlage: <https://github.com/gasb07/ps4mount>.
- **PatchDL** 0.3.0 (Knutwurst, GPL-3.0): Vorbild für die Spiel-Updates (`src/updates.c`, `src/upd_*.c`): die Server
  und Adressen von Sonys Versionslisten und Paketlisten, der Schlüssel der PS4-Versionsadresse, die Prüfung, ob ein Spiel
  von Sony signiert ist, der DNS-Weg (UDP, TCP, DNS über HTTPS, gemerkte Adressen) und das Holen älterer Patches über
  prosperopatches.com/orbispatches.com und SC-Links. Der Code ist eigener (andere TLS-Bibliothek, eigene Warteschlange);
  übernommen sind nur diese Fakten und die öffentlichen Wurzelzertifikate (Sony, DNS-Anbieter), aus denen
  `tools/mk_upd_ta.py` die Vertrauensanker in `src/upd_ca.h` erzeugt. <https://github.com/knutwurst/ps5-patchdl>.
- **exFAT2AMPR** (V-10, „Made by V-10“, GPL-3.0 mit Zusatzbedingungen): Anregung und Wissen für die Liste, die „Automatisch“
  lose lässt, für den Mitschnitt zur Profil-Erzeugung (`src/trace.c`, ein C-Nachbau von `ampr_pack_profile.py`, Gegenprobe mit
  dem Python-Werkzeug) und für die Prüfsummen-Tabelle der Spiel-Plugins; seit 1.104 (gelesen am 18.10.2026) auch für das Beiseitelegen
  doppelter Abbilder (`src/dupimg.c`), die Startprüfung gepackter Spiele, das Ablehnen von Verknüpfungen vor dem Packen und die Zeitschätzung je
  LZ4-Stufe (deren Startwerte sind die von exFAT2AMPR). Kein Quelltext von V-10 ist übernommen.
  <https://github.com/exFAT2AMPR/exfat2ampr>.
- **ampr_emu, LZ4-Pakete** (drakmor, GPL-3.0): Format und Regeln der LZ4-Pakete (AMPRPAK4, AMPRDAT3, AMPRCRC1, die TOML-Profile) sind ein
  C-Nachbau von `tools/ampr_pack.py` und `ampr_pack_format.py` (`src/lz4pack.c`, `src/lz4cfg.c`); gelesen am 12.10.2026 in
  <https://github.com/drakmor/ampr_emu> (`docs/ASSET_PACKS.md`). Gegenprobe in beide Richtungen mit dessen Python-Werkzeug.
- **Getestete LZ4-Profile** (danthespal, GPL-3.0, veröffentlicht mit MkPFS.CSharp): `astrobot.toml`, `ufc6-PPSA23566.toml`,
  `insomniac.toml` und `unity.toml` sind unverändert eingebettet (`third_party/lz4_profiles/`, `gen/lz4prof_blob.c`), übernommen aus
  PS5 LZ4 Compression 1.5.0 samt dessen Liste `index.json` (zwei Anzeigenamen eingedeutscht). Herkunft und Prüfsummen:
  `third_party/lz4_profiles/ORIGIN.md`. <https://github.com/danthespal/MkPFS.CSharp>
- **PS5 LZ4 Compression** 1.4.3–1.5.0 (theghostonline / Husam Osman, GPL-3.0-or-later): Vorbild für die Dateien, die immer lose
  bleiben (PlayGo-Marker, leere Dateien, die Startdateien von Ghost of Yōtei, gefunden von edu-ruiz-), für die Rechte 0777 aller
  geschriebenen Spieldateien (SIGBUS/CE-108255-1, gemeldet von OmgWhatIsThisss), für die Reparatur beim Prüfen der Pakete und für
  die Auswahl eines getesteten Profils nach Titel-ID, Name oder Engine (`src/lz4cfg.c`, `src/lz4pack_read.inc`, `src/amprlib.c`,
  `src/imgjob_lz4.inc`); nachgebaut, kein Quelltext übernommen. <https://github.com/theghostonline/PS5-LZ4-Compression> (seit Oktober 2026 nicht mehr erreichbar; Konto und Projekt wurden gelöscht oder umbenannt)
- **LZ4** 1.10.0 (Yann Collet, BSD-2-Clause): `src/third_party/lz4/` (`lz4.c`, `lz4hc.c`), unverändert; die Fassung, die auch die APR-EMU
  zum Entpacken benutzt. Lizenztext: `src/third_party/lz4/LICENSE`.
- **PS5 Dump & Image Converter** (eigenes Projekt des Autors, Modul `shadowmount_generation.py` und der
  fakelib-Manager): Vorbild für die Ablage der APR-EMU-Datei (`fakelib` statt `fakelib2`, Sicherung der
  Spieldatei als `.orig`, Erkennung eines früheren Emulators an `[AMPR_EMU]`) und für die Suchreihenfolge von
  ShadowMountPlus 1.7 (Backport-Ordner vor Spiel).
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
- **ps5-payload-manager** (itsPLK, GPL-3.0): die Regel, welche Prozesse als Payload gelten
  und beendet werden dürfen (`src/procmgr.c`), und das Vorbild für den Aufbau der
  Seite „Payloads“; seit 19.10.2026 (Fassung 0.6.3) auch für „Payloads herunterladen“
  (`src/paystore.c`, eigener Code): die Adresse der Liste, ihr Format, die Regeln für
  Versionsvergleich, Kennung und Prüfsumme. <https://github.com/itsPLK/ps5-payload-manager>
- **ps5-payloads-mirror** (itsPLK): die Liste `payloads_v2.json`, die „Payloads
  herunterladen“ zur Laufzeit von <https://itsplk.github.io/ps5-payloads-mirror/> lädt,
  samt den dort gespiegelten Dateien. Nichts davon ist Teil der App; die Payloads gehören
  ihren jeweiligen Autoren und stehen unter deren Lizenzen.
- **Wurzelzertifikate für GitHub** (ISRG Root X1/X2, USERTrust ECC/RSA; öffentlich, aus
  dem CA-Paket von Debian): `src/upd_ca.h`, Satz `UPD_WEB_*`, erzeugt von `tools/mk_upd_ta.py`.
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
