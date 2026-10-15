<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Lüftersteuerung, Temperaturüberwachung und Systemzentrale für die gejailbreakte PlayStation 5,<br>bedienbar im Browser der Konsole oder von jedem Gerät im Heimnetz, in sechs Sprachen.**

![Version](https://img.shields.io/badge/Version-1.68.1-1f6feb)
![Lizenz](https://img.shields.io/badge/Lizenz-GPL--3.0--or--later-blue)
![Plattform](https://img.shields.io/badge/Plattform-PS5%20Payload-003791)
![Sprachen](https://img.shields.io/badge/Sprachen-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Funktionen](#funktionen) · [Schnellstart](#schnellstart) · [Voraussetzungen](#voraussetzungen) · [Bedienung](#bedienung) · [Dokumentation](#dokumentation) · [Rechtliches](#rechtliches-und-haftungsausschluss) · [Credits](#credits-und-danksagung)

**Deutsch** · [English](README.en.md) · [Italiano](README.it.md) · [Español](README.es.md) · [Français](README.fr.md) · [Русский](README.ru.md)

<img src="docs/images/kuehlung.jpg" alt="Kühlung: Temperatur, Lüfter, laufendes Spiel, Zieltemperatur und Betriebsart" width="860">

</div>

Ein Homebrew-Payload (ELF), das die Temperatursensoren der PS5 liest, den Lüfter nach einer ruhigen, einstellbaren Komfortkurve regelt und eine Weboberfläche mitbringt. Dazu kommen Spiele-, Datei-, Payload- und Profilverwaltung. Alles läuft auf der Konsole selbst, ohne PC und ohne Internet.

## Funktionen

<table>
  <tr>
    <td width="33%" valign="top">
      <h3>🌡️ Kühlung</h3>
      Ruhige Lüfterregelung auf eine Zieltemperatur (60–91 °C), Betriebsarten, Profile pro Spiel, Sicherheitsgrenze in der Hardware.
    </td>
    <td width="33%" valign="top">
      <h3>📊 Messwerte</h3>
      Prozessor, Hauptchip, Grafik (Pro), Lüfter, Kernlast, Takt, Stromschienen, Bildrate und Controller-Akku, mit Verläufen.
    </td>
    <td width="33%" valign="top">
      <h3>🎮 Spiele</h3>
      Starten, beenden, kopieren, verschieben, in exFAT/ffpkg/ffpfsc umwandeln, LZ4-Asset-Packs, APR-EMU, PlayGo.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>💾 PS4 von USB</h3>
      PS4-Fake-PKGs direkt vom USB-Laufwerk spielen, ohne Installation (PS4Mount eingebaut), mit Updates und DLC.
    </td>
    <td valign="top">
      <h3>📦 Pakete & Spielstände</h3>
      PKG finden, aufteilen und installieren; Spielstände sichern und zurückspielen; Spielzeit mit Temperaturen.
    </td>
    <td valign="top">
      <h3>📁 Dateien & Payloads</h3>
      Dateimanager für Konsole und Laufwerke; Payloads starten, beenden und als Profile nacheinander laden.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>🖥️ System & Protokoll</h3>
      Modell, Firmware, Netzwerk, Bildschirm; Ereignisprotokoll und Kernel-Log live mit Aufnahme.
    </td>
    <td valign="top">
      <h3>🕹️ Bedienung am Controller</h3>
      L1/R1 wechseln die Seite, L2/R2 scrollen; Mute-Taste zweimal zeigt Temperatur und Lüfter auf dem Fernseher.
    </td>
    <td valign="top">
      <h3>🚀 Starter-Kachel</h3>
      Ein Ordner für <code>/data/homebrew</code>: Ein Tipp auf die Kachel startet die App und öffnet die Oberfläche.
    </td>
  </tr>
</table>

### Ansichten

<table>
  <tr>
    <td width="50%"><img src="docs/images/spiele.jpg" alt="Spiele-Bibliothek"></td>
    <td width="50%"><img src="docs/images/ps4-von-usb.jpg" alt="PS4-Spiele von USB"></td>
  </tr>
  <tr>
    <td align="center"><sub>Spiele: Bibliothek mit Starten, Kopieren, Konvertieren</sub></td>
    <td align="center"><sub>PS4 von USB: Fake-PKGs ohne Installation</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/spielzeit.jpg" alt="Spielzeit"></td>
    <td><img src="docs/images/dateimanager.jpg" alt="Dateimanager"></td>
  </tr>
  <tr>
    <td align="center"><sub>Spielzeit mit Tagesübersicht und Temperaturen</sub></td>
    <td align="center"><sub>Dateimanager für Konsole und USB-Laufwerke</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/payloads.jpg" alt="Payloads"></td>
    <td><img src="docs/images/system.jpg" alt="System"></td>
  </tr>
  <tr>
    <td align="center"><sub>Payloads: Profile, laufende und gespeicherte</sub></td>
    <td align="center"><sub>System: Konsole, Netzwerk, Bildschirm</sub></td>
  </tr>
</table>

<details>
<summary><b>Weitere Ansichten</b> (Kühlung im Detail, Spielstände, Pakete, Profil, Protokoll, Handy)</summary>
<br>
<table>
  <tr>
    <td width="50%"><img src="docs/images/kuehlung-diagramme.jpg" alt="Temperaturverlauf, Sensoren und Profile pro Spiel"></td>
    <td width="50%"><img src="docs/images/spielstaende.jpg" alt="Spielstände sichern"></td>
  </tr>
  <tr>
    <td><img src="docs/images/pakete.jpg" alt="Pakete installieren"></td>
    <td><img src="docs/images/profil.jpg" alt="Profil mit 30 eingebauten Profilbildern"></td>
  </tr>
  <tr>
    <td><img src="docs/images/protokoll.jpg" alt="Protokoll"></td>
    <td align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Kühlung auf dem Handy" width="240"></td>
  </tr>
</table>
<sub>Alle Bilder mit Beispieldaten und erfundenen Spielen.</sub>
</details>

<details>
<summary><b>Alle Funktionen im Detail</b></summary>

**Kühlung**
- Hält eine Zieltemperatur (Standard 66 °C, einstellbar 60–91 °C) mit gleitendem Mittelwert, Totzone und Trend und ändert die Drehzahl nur in kleinen Schritten.
- Betriebsarten leise, ausgewogen und kühl, Schnellwahl und eigene Regeln je Spiel.
- Ab der Sicherheitstemperatur (Standard 78 °C) gilt nur noch die Hardware; läuft die App nicht, regelt die Konsole selbst.

**Spiele**
- Starten (aus dem Konsolen-Browser schließt sich die Seite sofort), beenden, auf andere Laufwerke kopieren, mit ShadowMountPlus verschieben oder entpacken.
- Ohne PC in exFAT-, ffpkg- und ffpfsc-Abbilder umwandeln; alles wird nach dem Schreiben zurückgelesen und geprüft, mit `.sha256`-Datei.
- LZ4-Asset-Packs, AMPR-Index, APR-EMU, PlayGo-Stub und DLC-Module pro Spiel, Abbilder prüfen, entpacken und reparieren.
- PS4 von USB: PS4-Fake-PKGs vom USB-Laufwerk in die Bibliothek, ohne Installation (PS4Mount von gasb07).
- Spielzeit mit Temperaturen, Spielstände sichern und zurückspielen, Pakete finden, aufteilen und installieren, Verlauf aller Vorgänge.

**Verwaltung**
- Dateimanager: ansehen, herunter- und hochladen, Ordner anlegen, umbenennen, kopieren, verschieben, löschen (Ändern nur auf Laufwerken und in `/data`).
- Payloads: laufende ansehen und beenden, eigene `.elf` aus einem Ordner oder vom USB-Stick starten, Profile als Startfolge.
- Profil: Anzeigename und Profilbild (30 eingebaute Bilder oder eigene Datei).
- System: Modell, Firmware, Laufzeit, Speicher, Netzwerk; Rohsensoren und Diagnose im Expertenmodus.
- Kopfzeile auf jeder Seite: Ruhemodus, Neu starten, Ausschalten, abgesicherter Modus (je zwei Klicks).

**Technik**
- Eigener HTTP-Server ohne Fremdbibliothek, dokumentierte [JSON-Schnittstelle](docs/API.md), Einstellungen unter `/data/PS5-Cooling-Center/config.json`.
- Kein Zugriff aufs Internet: Die App spricht nur mit Programmen auf der Konsole und mit den Browsern im Heimnetz.

</details>

## Schnellstart

1. Aus den [Releases](../../releases) die Datei `PS5_Cooling_System_Center_v<Version>.elf` laden.
2. An die Konsole senden (ELF-Lader, Port **9021**), zum Beispiel:
   ```bash
   nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.68.1.elf
   ```
3. Im Browser öffnen: **`http://<PS5-IP>:8086`** (die Adresse zeigt auch die Meldung oben links auf dem Fernseher).

> [!TIP]
> **Ohne Senden starten:** `PPSA86921.zip` aus dem Release entpacken und den Ordner nach `/data/homebrew/` kopieren. ShadowMountPlus legt eine Kachel an; ein Tipp darauf startet die App und öffnet die Oberfläche.

> [!IMPORTANT]
> Es darf immer nur **eine** Instanz laufen, sonst regeln zwei Programme gegeneinander am Lüfter. Eine alte Instanz lässt sich auf der Seite „Payloads“ beenden. Mehr in der [LIESMICH](docs/LIESMICH.txt).

## Voraussetzungen

| | |
| --- | --- |
| **Konsole** | PS5 mit Jailbreak und ELF-Lader auf Port 9021 ([elfldr](https://github.com/ps5-payload-dev/elfldr) o. ä.) |
| **Getestet** | PS5 Pro (CFI-7021), Firmware 12.00; laut mehreren Nutzern auch 13.60 |
| **Lüfter** | [kstuff](https://github.com/EchoStretch/kstuff-lite) (ohne laufen Sensoren und Oberfläche, die Regelung nicht) |
| **Netzwerk** | Browser im selben Netz, oder der Browser der Konsole selbst |
| **Optional** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) für Verschieben, Entpacken und die Starter-Kachel |

<details>
<summary>Firmware und SDK im Detail</summary>

Die ELF ist mit dem PS5-Payload-SDK **v0.43** gebaut, dessen Startcode die Firmware **bis 13.60** kennt. Eine Firmware, die der Startcode nicht kennt, kommt nicht bis `main()`: Das Programm startet dann gar nicht und schreibt nichts ins Protokoll. Für 13.00 bis 13.40 gibt es noch keine Rückmeldung (der Startcode kennt sie). Die Grafiktemperatur gibt es nur auf der Pro.
</details>

## Bedienung

| Seite | Inhalt |
| --- | --- |
| **Kühlung** | Status, Verlauf, Sensoren, Zieltemperatur, Betriebsart, Profile pro Spiel |
| **Spiele** | Bibliothek, Spielzeit, Spielstände, Pakete, PS4 von USB, Sicherungen, Verlauf |
| **Dateimanager** | Ordner der Konsole und der Laufwerke |
| **Payloads** | laufende, gespeicherte und auf USB liegende Payloads, Profile |
| **System** | Konsole, Speicher, Netzwerk, Diagnose |
| **Profil** | Anzeigename und Profilbild |
| **Protokoll** | Ereignisse der App und Kernel-Log live |
| **Credits** | Dank an die Entwickler; dazu **Handbuch** und **FAQ** in sechs Sprachen |

**Am Controller (Browser der Konsole):** L1 / R1 vorherige / nächste Seite · L2 / R2 nach oben / unten scrollen · Mute-Taste zweimal: Temperatur und Lüfter auf dem Fernseher.

> [!WARNING]
> Die Oberfläche hat **keine Anmeldung**: Jedes Gerät im Heimnetz kann lesen und ändern. Keine Portweiterleitung auf Port 8086 einrichten. Wer nur den Browser der Konsole zulassen will, setzt `bind_address` auf `127.0.0.1`. Details im [Handbuch](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) und in der [Sicherheitsrichtlinie](SECURITY.md).

## Dokumentation

| Dokument | Inhalt |
| --- | --- |
| [Handbuch](docs/HANDBUCH.md) | Bedienung im Detail (auch in der App und als PDF in sechs Sprachen) |
| [API](docs/API.md) | Alle Endpunkte und Konfigurationsfelder |
| [Entwicklung](docs/ENTWICKLUNG.md) | Bauen, Senden, Fehlersuche, Quellcode-Aufbau |
| [Änderungen](CHANGELOG.md) | Was sich in welcher Version geändert hat |
| [Entscheidungsprotokoll](docs/ERWEITERUNGEN.md) | Warum etwas so gebaut ist, was gemessen wurde |
| [Fremde Bestandteile](THIRD_PARTY_NOTICES.md) | Übernommener Code und seine Lizenzen |
| [Mitwirken](CONTRIBUTING.md) · [Sicherheit](SECURITY.md) | Beiträge und Meldung von Sicherheitslücken |

<details>
<summary><b>Aus dem Quellcode bauen</b></summary>

Voraussetzung ist das [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk) **ab v0.42** (getestet mit v0.43); der Bau prüft das und bricht mit einem zu alten SDK ab. Unter Windows genügt Git Bash mit LLVM 21:

```bash
tools/build-windows.sh
```

Unter Linux oder WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

Ergebnis ist `PS5_Cooling_Center.elf`; die Starter-Kachel baut `tools/make_starter.sh`. Alles Weitere in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).
</details>

## Rechtliches und Haftungsausschluss

> [!CAUTION]
> **Kein Sony-Produkt, keine Piraterie, keine Haftung.** Dieses Projekt steht in keiner Verbindung zu Sony. Es enthält keine Spiele, keine Firmware, keine Schlüssel und keinen Code zum Umgehen von Kopierschutz, und es lädt nichts aus dem Internet. Das Modifizieren einer Konsole kann Garantie und Konto kosten; das Risiko trägt, wer es tut. Das Programm wird **ohne jede Gewährleistung** bereitgestellt.

<details>
<summary>Vollständiger Text</summary>

> Dieser Abschnitt ist eine allgemeine Information und keine Rechtsberatung. Wer das Projekt benutzt, ist selbst dafür verantwortlich, dass seine Nutzung in seinem Land und gegenüber seinen Vertragspartnern zulässig ist.

- **Kein Sony-Produkt.** „PlayStation“, „PS5“ und die zugehörigen Logos sind Marken der Sony Interactive Entertainment Inc. Dieses Projekt steht in keiner Verbindung zu Sony und wird von Sony weder unterstützt noch gebilligt. Alle weiteren Namen gehören ihren jeweiligen Inhabern.
- **Eigene Konsole, eigene Verantwortung.** Das Programm läuft nur auf einer Konsole, die der Besitzer selbst modifiziert hat. Das Modifizieren kann gegen Nutzungsbedingungen verstoßen und zum Verlust der Garantie, zur Sperrung des Kontos oder der Konsole führen, in manchen Ländern auch rechtliche Folgen haben. Das Risiko trägt, wer die Konsole modifiziert und dieses Programm einsetzt.
- **Keine Piraterie.** Dieses Projekt ist **nicht** dafür gedacht, Raubkopien zu beschaffen, zu verbreiten oder zu nutzen, und es unterstützt das nicht. Kopieren, Verschieben und Konvertieren wirken nur auf Spiele, die schon auf der eigenen Konsole liegen, und sind für Sicherungen rechtmäßig erworbener Spiele gedacht. Pakete finden, aufteilen, installieren und von USB einbinden wirken nur auf Pakete, die du selbst auf die Konsole oder ein Laufwerk gelegt hast; die App besorgt keine. Das Verbreiten urheberrechtlich geschützter Inhalte ist in den meisten Ländern strafbar. Hilfe dazu gibt es hier nicht, auch nicht in Issues.
- **Keine Dateien von Sony im Repository.** Systembibliotheken, Firmware und proprietäre SDK-Dateien werden weder mitgeliefert noch angenommen (siehe [CONTRIBUTING](CONTRIBUTING.md)). Die 30 Profilbilder hat der Autor selbst erzeugt.
- **Keine Gewährleistung, keine Haftung.** Das Programm greift in die Lüftersteuerung und in Systemfunktionen der Konsole ein. Es wird so bereitgestellt, wie es ist, **ohne jede Gewährleistung** (GNU GPL, Abschnitte 15 und 16). Der Autor haftet nicht für Schäden an Konsole, Daten oder Konten. Eine verstaubte Konsole kühlt schlechter, egal wie der Lüfter geregelt wird.
- **Datenschutz.** Verläufe, Protokoll und Einstellungen bleiben auf der Konsole unter `/data/PS5-Cooling-Center/`. Die App sendet keine Daten ins Internet. Einzige Ausnahme im Browser: Die Seite „Credits“ lädt einmal das kleine Symbolbild von github.com, um zu sehen, ob der Browser Internet hat; nur dann sind die Links anklickbar.
- **Drittprojekte** stehen unter ihren eigenen Lizenzen, siehe [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
</details>

## Mitwirken und Fehler melden

Fehler und Wünsche gern als [Issue](../../issues), mit Konsolenmodell, Firmware, Version und dem exportierten Protokoll (Seite „Protokoll“). Beiträge: [CONTRIBUTING.md](CONTRIBUTING.md). Sicherheitslücken bitte wie in [SECURITY.md](SECURITY.md) beschrieben melden.

## Credits und Danksagung

Dieses Projekt steht auf den Schultern der PS5-Homebrew-Gemeinschaft. **Vielen Dank!** Ein ganz besonderer Dank an **[Gezine](https://github.com/Gezine)**: Ohne Gezines Arbeit liefe auf vielen Konsolen kein Homebrew, und damit auch diese App nicht.

<details>
<summary><b>Plattform und Werkzeuge</b></summary>

- **John Törnblom und alle Mitwirkenden von [ps5-payload-dev](https://github.com/ps5-payload-dev)**: das [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk), der Payload-Lader [elfldr](https://github.com/ps5-payload-dev/elfldr), [klogsrv](https://github.com/ps5-payload-dev/klogsrv), [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) und [websrv](https://github.com/ps5-payload-dev/websrv).
- **Die Entwickler von kstuff**: [sleirsgoevy](https://github.com/sleirsgoevy), [EchoStretch](https://github.com/EchoStretch) und [drakmor](https://github.com/drakmor) ([kstuff-lite](https://github.com/EchoStretch/kstuff-lite)). Ohne kstuff gäbe es keinen Zugriff auf den Lüfter-Controller.
</details>

<details>
<summary><b>Code aus anderen Projekten (portiert oder eingebunden)</b></summary>

- **[gasb07](https://github.com/gasb07), [PS4Mount](https://github.com/gasb07/ps4mount)** (GPL-3.0): PS4-Fake-PKGs vom USB-Laufwerk ohne Installation; der Quelltext ist als Reiter „PS4 von USB“ eingebaut.
- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) und die Mitwirkenden von [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): exFAT-, PFS- und PFSC-Abbilder.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): Vorlage für den UFS2-/ffpkg-Schreiber.
- **[drakmor](https://github.com/drakmor)**: [ampr_emu](https://github.com/drakmor/ampr_emu) (LZ4-Asset-Packs, APR-EMU), [pgo_stub](https://github.com/drakmor/pgo_stub) und [dlc_emu](https://github.com/drakmor/dlc_emu) (eingebettete Fassungen, GPL-3.0).
- **[BlackBearReloaded](https://github.com/BlackBearReloaded), ps5-native-app-boilerplate** (GPL-3.0): Bauweise der Starter-Kachel als nativer Titel.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): Aufbau der Pakete und Ablauf des Installierens (eigener Code nach diesem Vorbild).
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): Profilfunktion und Format des Profilbilds.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): QR-Encoder.
- **[danthespal](https://github.com/danthespal), [MkPFS.CSharp](https://github.com/danthespal/MkPFS.CSharp)** (GPL-3.0): die vier getesteten LZ4-Profile (ASTRO BOT, EA Sports UFC 6, Insomniac-Engine, Unity-Engine), unverändert eingebettet.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT) · **[Dave Gamble](https://github.com/DaveGamble), [cJSON](https://github.com/DaveGamble/cJSON)** (MIT) · **[LZ4](https://github.com/lz4/lz4)** (BSD-2-Clause).
</details>

<details>
<summary><b>Zusammenspiel, Wissen und Vorbilder</b></summary>

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (Spiele einhängen, verschieben, entpacken) und [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (Stromschienen, Taktwerte).
- **V-10, [exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr)**: Liste für „Automatisch“, Mitschnitt zur Profil-Erzeugung, Pflege der Spiel-Plugins.
- **theghostonline (Husam Osman), [PS5 LZ4 Compression](https://github.com/theghostonline/PS5-LZ4-Compression)**: Dateien, die beim Packen lose bleiben müssen, Rechte 0777, Reparatur beim Prüfen der Pakete, Auswahl der getesteten Profile.
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: Parameter für `.ffpkg`-Abbilder.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) und [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): Erkennung des laufenden Spiels.
- **[etaHEN](https://github.com/etaHEN/etaHEN)** und **[onionHEN](https://github.com/aydencharles/onionHEN)**: Systemaufrufe, Messwerte, Bildrate.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)**: Vorbild für viele Funktionen (Code jeweils neu geschrieben).
- **BestPig, [BackPork](https://github.com/BestPig/BackPork)**: Prinzip der Ersatzbibliotheken.
- **Juma Sayeh und Osama Abualia, PS5 Game Compressor**: Ideen fürs Prüfen und Konvertieren (alles neu geschrieben, der Quelltext trägt keine Lizenz).
- **Linux-Treiber `hid-playstation`** (Akkustand des DualSense) · **Xbox 360 DashLaunch** (Gedanke der ruhigen Regelung).
</details>

<details>
<summary><b>Danke an die Community</b></summary>

Ihr Code steckt nicht in dieser App, aber ohne ihre geteilte Arbeit gäbe es die Szene so nicht: [owendswang](https://github.com/owendswang), [LightningMods](https://github.com/LightningMods), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW), [Specter](https://github.com/Cryptogenic), [ChendoChap](https://github.com/ChendoChap), [idlesauce](https://github.com/idlesauce), [flatz](https://github.com/flatz), [Al Azif](https://github.com/Al-Azif), [zecoxao](https://github.com/zecoxao), [ArkSama](https://github.com/ArkSama), [nexgen999](https://github.com/nexgen999), [matem6](https://github.com/matem6), [Phoenixx1202](https://github.com/Phoenixx1202), [SiSTR0](https://github.com/SiSTR0) – und allen anderen, die mit Code, Tests, Anleitungen oder Antworten beitragen. In der App stehen alle mit Bild auf der Seite „Credits“.
</details>

Wer sich hier vermisst oder falsch beschrieben findet: bitte melden, die Liste wird gern ergänzt.

## Lizenz

Copyright © 2026 strongt1me · **GNU General Public License v3.0 oder später**, siehe [LICENSE](LICENSE). Bis 1.45.1 galt MIT; ab 1.46.0 GPL-3.0-or-later, weil Teile aus GPL-Projekten übernommen sind (Einzelheiten in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

<sub>Diese README gibt es auch auf [English](README.en.md), [Italiano](README.it.md), [Español](README.es.md), [Français](README.fr.md) und [Русский](README.ru.md). Handbuch und FAQ liegen jeder Veröffentlichung in allen sechs Sprachen bei.</sub>
