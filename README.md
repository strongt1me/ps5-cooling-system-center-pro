<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Lüftersteuerung, Temperaturüberwachung und Systemzentrale für die gejailbreakte PlayStation 5, mit Weboberfläche im Heimnetz in sechs Sprachen.**

![Version](https://img.shields.io/badge/Version-1.54.1-1f6feb)
![Lizenz](https://img.shields.io/badge/Lizenz-GPL--3.0--or--later-blue)
![Plattform](https://img.shields.io/badge/Plattform-PS5%20Payload-003791)
![Sprachen](https://img.shields.io/badge/Sprachen-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Funktionen](#funktionen) · [Installation](#installation) · [Bedienung](#bedienung) · [Dokumentation](#dokumentation) · [Rechtliches](#rechtliches-und-haftungsausschluss) · [Credits](#credits-und-danksagung)

**Deutsch** · [English](README.en.md) · [Italiano](README.it.md) · [Español](README.es.md) · [Français](README.fr.md) · [Русский](README.ru.md)

</div>

---

PS5 Cooling & System Center - Pro ist ein Homebrew-Payload (ELF) für eine gejailbreakte PlayStation 5. Es liest die Temperatursensoren der Konsole, regelt den Lüfter nach einer ruhigen, frei einstellbaren Komfortkurve und bringt eine Weboberfläche mit, die du von jedem Gerät im Heimnetz öffnest: Handy, Tablet oder PC. Dazu kommen eine Spielverwaltung, eine Payload-Verwaltung und die Profilverwaltung der Konsole. Alles läuft auf der PS5 selbst, ohne PC und ohne Internet.

Das Projekt ist der Nachfolger des *PS5 Temperature Manager*; Backend (C) und Oberfläche sind neu geschrieben.

<p align="center">
  <img src="docs/images/kuehlung.jpg" alt="Kühlung: Temperatur, Lüfter, Verlauf und Sensoren" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/payloads.jpg" alt="Payload-Verwaltung"></td>
    <td width="50%"><img src="docs/images/profil.jpg" alt="Profil mit 30 eingebauten Profilbildern"></td>
  </tr>
  <tr>
    <td align="center"><sub>Payloads starten, kopieren und beenden (Beispieldaten)</sub></td>
    <td align="center"><sub>Profilbild aus 30 eingebauten Bildern wählen</sub></td>
  </tr>
</table>

<details>
<summary>Auch am Handy bedienbar</summary>
<p align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Kühlung auf dem Handy" width="300"></p>
</details>

## Funktionen

**Kühlung**

- **Komfortregelung des Lüfters.** Hält eine Zieltemperatur (Standard 66 °C, einstellbar von 60 bis 91 °C; 91 °C ist der Wert der Konsole selbst) mit gleitendem Mittelwert, Totzone und Trend und ändert die Drehzahl nur in kleinen Schritten. Das Ziel ist nicht die niedrigste Temperatur, sondern ein Lüfter, der ruhig und gleichmäßig klingt. Betriebsarten (leise, ausgewogen, kühl), Schnellwahl und eigene Regeln je Spiel.
- **Sicherheit zuerst.** Ab der Sicherheitstemperatur (Standard 78 °C) gilt nur noch die Hardware. Läuft die App nicht, regelt die Konsole mit ihrer eigenen Kennlinie.
- **Messwerte.** Prozessor, Hauptchip, Grafik (nur PS5 Pro), Lüfterdrehzahl, Last aller CPU-Kerne, Takt live, Stromaufnahme der Spannungsschienen, Bildrate im Spiel und Controller-Akku. Verläufe über 2 Minuten, 24 Stunden und als Wochenauswertung der Kühlleistung.
- **Meldungen auf dem Fernseher.** Beim Start, bei Warnungen und auf Wunsch regelmäßig. Zwei kurze Drücke auf die Mikrofon-Taste des Controllers zeigen Prozessortemperatur und Lüfter.

**Verwaltung**

- **Spiele.** Die Spiele des Startbildschirms mit Cover, Spielzeit, Format und Speicherort. Direkt starten (läuft ein anderes Spiel, weist ein Hinweis darauf hin; „Spiel beenden“ schließt es sofort, danach startet das nächste mit einem Tipp), auf andere Laufwerke kopieren, mit ShadowMountPlus verschieben oder entpacken und ohne PC in exFAT-, ffpkg- und ffpfsc-Abbilder umwandeln. Ein zweiter Reiter schreibt die Spielzeit mit: wann gespielt wurde, wie lange und wie warm die Konsole dabei wurde, mit Summen, Tagesbalken und Rangfolge. Ein dritter sichert die Spielstände auf einen Stick, eine Platte oder den Konsolenspeicher und spielt einen Titel auf Wunsch zurück: unverändert und verschlüsselt, jede Datei zurückgelesen und geprüft, und vor dem Zurückspielen wird der jetzige Stand eigens gesichert. Kopien und Abbilder werden nach dem Schreiben ganz zurückgelesen und geprüft; daneben liegt eine `.sha256`-Datei, die am PC mit `sha256sum -c` später wieder zu prüfen ist. Ein Schalter „Covers & Metadaten speichern“ legt Titelbilder und die langsam zu ermittelnden Angaben der Spiele auf der Konsole ab (Ordner `covers_and_more`), damit die Liste schneller lädt. Ein vierter Reiter, „Pakete“, findet die Spiel-Pakete (`.pkg`) auf Sticks, Platten und im Konsolenspeicher, zeigt sie mit Bild, Version und Art und teilt große Pakete in Teile für FAT32-Sticks oder Discs auf (zurückgelesen und geprüft; das Paket bleibt unverändert) und installiert ein Paket auf Wunsch über die Installation der Konsole selbst: Die App stellt es ihr nur bereit, prüft vorher, was im Weg stehen würde, zeigt den Fortschritt und löscht oder überschreibt nichts.
- **Dateien.** Ein Dateimanager für die Ordner der Konsole: ansehen, herunterladen, hochladen, neuen Ordner anlegen, umbenennen, kopieren, verschieben und löschen (Ändern nur auf den Laufwerken und in `/data`; Löschen fragt zweimal nach). Die Liste lässt sich nach Name, Größe oder Datum ordnen, alle Einträge lassen sich auf einmal wählen, die Größe eines Ordners wird auf Wunsch nachgezählt, und Bilder und Textdateien zeigt „Ansehen“ gleich im Browser.
- **Payloads.** Laufende Payloads ansehen und beenden. Eigene `.elf`-Dateien aus einem Ordner der Konsole oder von einem USB-Stick starten oder in den Ordner kopieren, ganz ohne PC.
- **Profil.** Anzeigename ändern; Profilbild aus 30 eingebauten Bildern oder aus einer eigenen Datei, mit Sicherung des bisherigen Bildes.
- **System.** Modell, Firmware, Laufzeit, Speicher und Netzwerk. Rohsensoren und Diagnose im Expertenmodus. Ereignisprotokoll zum Exportieren.
- **Kopfzeile.** Auf jeder Seite: Ruhemodus, Neu starten, Ausschalten und abgesicherter Modus (jeder nach zwei Klicks, als Gruppe in der Mitte), Vollbild und Expertenmodus.
- **Sprachen.** Die Oberfläche gibt es auf Deutsch, Englisch, Italienisch, Spanisch, Französisch und Russisch; oben rechts wählst du die Sprache (beim ersten Besuch gilt die des Browsers). Handbuch und FAQ stehen in allen sechs Sprachen in der App und als PDF zum Download. Meldungen, die die Konsole selbst auf dem Fernseher einblendet, sind deutsch.
- **Startmenü-Kachel.** Öffnet die Oberfläche direkt im Browser der Konsole.

**Technik.** Eigener HTTP-Server ohne Fremdbibliothek (die Dateien der Oberfläche gehen komprimiert über das Netz), dokumentierte [JSON-Schnittstelle](docs/API.md), Einstellungen unter `/data/PS5-Cooling-Center/config.json`, kein Zugriff aufs Internet (die App verbindet sich nur mit Programmen auf der Konsole selbst).

## Voraussetzungen

| | |
| --- | --- |
| **Konsole** | PS5 mit Jailbreak und einem ELF-Lader auf **Port 9021** ([elfldr](https://github.com/ps5-payload-dev/elfldr) oder gleichwertig). Getestet auf einer **PS5 Pro (CFI-7021) mit Firmware 12.00**. Auf einer PS5 mit **Firmware 13.60** läuft die App laut Rückmeldung mehrerer Nutzer ebenfalls. Andere Modelle und Firmware-Stände sind ungetestet. Die Grafiktemperatur gibt es nur auf der Pro. |
| **Firmware** | Die ELF ist mit dem PS5-Payload-SDK **v0.43** gebaut, dessen Startcode die Firmware **bis 13.60** kennt. Eine Firmware, die der Startcode nicht kennt, kommt nicht bis `main()`: Das Programm startet dann gar nicht und schreibt nichts ins Protokoll. Auf **13.60** melden mehrere Nutzer, dass die App läuft; für 13.00 bis 13.40 ist das nicht gemeldet (der Startcode kennt sie). |
| **kstuff** | Für die Lüftersteuerung nötig (`/dev/icc_fan`). Ohne kstuff laufen Sensoren und Oberfläche weiter, die Regelung meldet „nicht verfügbar“. |
| **Netzwerk** | Ein Browser im selben Netz wie die Konsole. |
| **optional** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) für Verschieben und Entpacken von Spielen sowie die Erkennung von Format und Speicherort. |

## Installation

1. Aus den **Releases** die Datei `PS5_Cooling_System_Center_v<Version>.elf` laden.
2. Die ELF an die Konsole senden, Port **9021**, mit einem beliebigen Payload-Sender oder per Kommandozeile:

   ```bash
   nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.54.1.elf
   ```

3. Auf dem Fernseher erscheint eine Meldung mit der Adresse. Im Browser öffnen: **`http://<PS5-IP>:8086`**
4. Die Startmenü-Kachel (Bereich „Medien“) legt das Programm beim ersten Start selbst an, ein Installer ist nicht nötig. Die Kachel überlebt Neustarts, startet das Programm aber nicht, sie öffnet nur die Oberfläche. Fehlt sie später, holt „Kachel installieren“ auf der Systemseite sie zurück; als Rückfallweg liegt `cooling-center-launcher-installer_v<Version>.elf` bei.

Nach jedem Neustart der Konsole muss die ELF erneut gesendet werden, zum Beispiel per Autoloader. Es darf immer nur **eine** Instanz laufen: Zwei Instanzen würden den Lüfter gegeneinander regeln. Eine alte Instanz lässt sich auf der Seite „Payloads“ beenden. Die ausführliche Anleitung liegt der Veröffentlichung als [LIESMICH](docs/LIESMICH.txt) bei.

## Bedienung

| Seite | Inhalt |
| --- | --- |
| **Profil** | Anzeigename und Profilbild der Konsole |
| **Spiele** | Spiele starten, kopieren, verschieben, konvertieren; Spielzeit mit Temperaturen; Spielstände sichern und zurückspielen |
| **Dateien** | Dateimanager: Ordner ansehen, Dateien hoch- und herunterladen, kopieren, verschieben, löschen |
| **Payloads** | laufende, gespeicherte und auf USB liegende Payloads |
| **Kühlung** | Status, Verlauf, Sensoren, Zieltemperatur, Betriebsart, Spielprofile |
| **System** | Konsole, Speicher, Netzwerk, Diagnose, Ausschalten und Neustart |
| **Protokoll** | Ereignisse der App, als `.log` exportierbar, und das Kernel-Log der Konsole live mit Filter, Pause, Speichern auf der Konsole und Aufnahme |
| **Credits** | Dank an die Entwickler, deren Arbeit in der App steckt; dazu **Handbuch** und **FAQ** in deiner Sprache |

Alles Weitere steht im [Handbuch](docs/HANDBUCH.md): Komfortregelung und ihre Parameter, Payloads starten, Profilbilder, Kachel, Bildschirmanzeige und Einstellungen.

**Zugriff im Heimnetz.** Die Oberfläche hat keine Anmeldung: Jedes Gerät im Heimnetz kann lesen und ändern. Die Konsole gehört nicht ins offene Internet, also **keine Portweiterleitung** auf Port 8086 einrichten. Wer das nicht will, setzt `bind_address` auf `127.0.0.1`. Der Server weist Anfragen ab, die erkennbar von fremden Webseiten kommen. Einzelheiten stehen im [Handbuch](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) und in der [Sicherheitsrichtlinie](SECURITY.md).

## Aus dem Quellcode bauen

Voraussetzung ist das [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk) **ab v0.42** (gebaut und getestet mit **v0.43**): Sein Startcode entscheidet, auf welcher Firmware die ELF überhaupt anläuft, und v0.41 endet bei 13.40. Der Bau prüft das (`tools/check_sdk_firmware.py`) und bricht mit einem zu alten SDK ab, ebenso nach dem Binden, wenn die fertige ELF den Fall für 13.60 nicht enthält. Unter Windows genügt Git Bash mit LLVM 21 (nicht 18):

```bash
tools/build-windows.sh
```

Unter Linux oder WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

Ergebnis ist `PS5_Cooling_Center.elf`. Alle Schritte, Fehlersuche, Release-Ablauf und Aufbau des Quellcodes stehen in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).

## Dokumentation

| Dokument | Inhalt |
| --- | --- |
| [Handbuch](docs/HANDBUCH.md) | Bedienung im Detail (auch in der App und als PDF in sechs Sprachen, siehe Releases) |
| [API](docs/API.md) | Alle Endpunkte und Konfigurationsfelder |
| [Entwicklung](docs/ENTWICKLUNG.md) | Bauen, Senden, Fehlersuche, Quellcode-Aufbau |
| [Versionshinweise](docs/RELEASE_NOTES.md) | Was sich in welcher Version geändert hat |
| [Entscheidungsprotokoll](docs/ERWEITERUNGEN.md) | Warum etwas so gebaut ist, was gemessen wurde, was offen ist |
| [LIESMICH](docs/LIESMICH.txt) | Kurzanleitung, die jeder Veröffentlichung beiliegt |
| [Fremde Bestandteile](THIRD_PARTY_NOTICES.md) | Übernommener Code und seine Lizenzen |
| [Mitwirken](CONTRIBUTING.md) · [Sicherheit](SECURITY.md) | Beiträge und Meldung von Sicherheitslücken |

## Rechtliches und Haftungsausschluss

> Dieser Abschnitt ist eine allgemeine Information und keine Rechtsberatung. Wer das Projekt benutzt, ist selbst dafür verantwortlich, dass seine Nutzung in seinem Land und gegenüber seinen Vertragspartnern zulässig ist.

- **Kein Sony-Produkt.** „PlayStation“, „PS5“ und die zugehörigen Logos sind Marken der Sony Interactive Entertainment Inc. Dieses Projekt steht in keiner Verbindung zu Sony und wird von Sony weder unterstützt noch gebilligt. Alle weiteren Namen gehören ihren jeweiligen Inhabern.
- **Eigene Konsole, eigene Verantwortung.** Das Programm läuft nur auf einer Konsole, die der Besitzer selbst modifiziert hat. Das Modifizieren kann gegen Nutzungsbedingungen verstoßen und zum Verlust der Garantie, zur Sperrung des Kontos oder der Konsole führen, in manchen Ländern auch rechtliche Folgen haben. Das Risiko trägt, wer die Konsole modifiziert und dieses Programm einsetzt.
- **Keine Piraterie.** Dieses Projekt ist **nicht** dafür gedacht, Raubkopien zu beschaffen, zu verbreiten oder zu nutzen, und es unterstützt das nicht. Es enthält keine Spiele, keine Firmware, keine Schlüssel und keinen Code zum Umgehen von Kopierschutz oder DRM. Es lädt nichts aus dem Internet. Kopieren, Verschieben und Konvertieren wirken nur auf Spiele, die schon als Ordner oder Abbild auf der eigenen Konsole liegen, und sind für Sicherungen rechtmäßig erworbener Spiele gedacht. Pakete finden, aufteilen und installieren wirken nur auf Pakete, die du selbst auf die Konsole oder ein angestecktes Laufwerk gelegt hast; die App besorgt keine. Das Verbreiten urheberrechtlich geschützter Inhalte ist in den meisten Ländern strafbar. Hilfe dazu gibt es hier nicht, auch nicht in Issues.
- **Keine Dateien von Sony im Repository.** Systembibliotheken, Firmware und proprietäre SDK-Dateien werden weder mitgeliefert noch angenommen (siehe [CONTRIBUTING](CONTRIBUTING.md)). Die 30 Profilbilder hat der Autor selbst erzeugt.
- **Keine Gewährleistung, keine Haftung.** Das Programm greift in die Lüftersteuerung und in Systemfunktionen der Konsole ein. Es wird so bereitgestellt, wie es ist, **ohne jede Gewährleistung** (GNU GPL, Abschnitte 15 und 16). Der Autor haftet nicht für Schäden an Konsole, Daten oder Konten. Das Programm ersetzt keine Wartung: Eine verstaubte Konsole kühlt schlechter, egal wie der Lüfter geregelt wird.
- **Datenschutz.** Verläufe, Protokoll und Einstellungen bleiben auf der Konsole unter `/data/PS5-Cooling-Center/`. Die App sendet keine Daten ins Internet und verbindet sich nur mit Programmen auf der Konsole selbst (Payload-Lader, ShadowMountPlus) und mit den Browsern im Heimnetz, die die Oberfläche öffnen. Einzige Ausnahme im Browser, nicht in der App: Die Seite „Credits“ lädt beim Öffnen einmal das kleine Symbolbild von github.com, um zu sehen, ob der Browser Internet hat; nur dann sind die Links zu GitHub anklickbar. Die Profilbilder der Entwickler sind in der App eingebaut und werden nicht aus dem Internet geladen.
- **Drittprojekte.** Sie stehen unter ihren eigenen Lizenzen, siehe [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) und die Credits unten.

## Mitwirken und Fehler melden

Fehler und Wünsche gern als [Issue](../../issues). Bitte Konsolenmodell, Firmware, Version des Programms und, wenn vorhanden, das exportierte Protokoll (Seite „Protokoll“) angeben. Beiträge: [CONTRIBUTING.md](CONTRIBUTING.md). Sicherheitslücken bitte nicht öffentlich melden, sondern wie in [SECURITY.md](SECURITY.md) beschrieben.

## Credits und Danksagung

Dieses Projekt steht auf den Schultern der PS5-Homebrew-Gemeinschaft. Ohne die Arbeit dieser Entwicklerinnen und Entwickler gäbe es weder die Plattform noch Teile der Funktionen. **Vielen Dank!**

**Plattform und Werkzeuge**

- **John Törnblom und alle Mitwirkenden von [ps5-payload-dev](https://github.com/ps5-payload-dev)**: das [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk), mit dem jede ELF dieses Projekts gebaut wird, der Payload-Lader [elfldr](https://github.com/ps5-payload-dev/elfldr) auf Port 9021, [klogsrv](https://github.com/ps5-payload-dev/klogsrv) und [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) für die Entwicklung und [websrv](https://github.com/ps5-payload-dev/websrv), dessen Startweg für Spiele hier als Vorbild diente.
- **Die Entwickler von kstuff**: [sleirsgoevy](https://github.com/sleirsgoevy) (Hauptautor), [EchoStretch](https://github.com/EchoStretch) und [drakmor](https://github.com/drakmor) (größte Mitwirkende an [kstuff-lite](https://github.com/EchoStretch/kstuff-lite)); Repositories u. a. bei [ps5-payload-dev](https://github.com/ps5-payload-dev/kstuff) und [EchoStretch](https://github.com/EchoStretch/kstuff). Ohne kstuff gäbe es keinen Zugriff auf den Lüfter-Controller.

**Code aus anderen Projekten (portiert oder eingebunden)**

- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) und die Mitwirkenden von [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): Aufbau von exFAT-, PFS- und PFSC-Abbildern. Die Konvertierung auf der Konsole ist eine Übertragung nach C.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): Vorlage für den UFS2-/ffpkg-Schreiber.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): Aufbau der PS4-/PS5-Pakete, das Teile-Format (`PS5MPKG1`), der Ordnerplan der Paketsuche und der Ablauf des Installierens (wie die Systembibliothek der Konsole aufgerufen wird und woher sie das Paket liest). Die Pakete-Funktionen dieser App sind eigener Code nach diesem Vorbild, nichts davon ist eingebettet.
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): Aufbau der Profilfunktion und Format des Profilbilds.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT): schnelle Kompression für die Abbilder.
- **[Dave Gamble](https://github.com/DaveGamble) und Mitwirkende, [cJSON](https://github.com/DaveGamble/cJSON)** (MIT): JSON.

**Zusammenspiel, Wissen und Vorbilder**

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (Spiele einhängen, verschieben und entpacken; die Spieleseite baut darauf auf; seine README nennt den empfohlenen Aufbau für `.ffpkg`-Abbilder: 64-KiB-Blöcke) und [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (Reihenfolge der Stromschienen, Taktwerte).
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: zeigte, mit welchen Parametern ein `.ffpkg` für ShadowMountPlus gebaut wird (64-KiB-Blöcke und -Fragmente, kein reservierter Platz, Inode-Dichte 262144, Sektorgröße 512) und dass die Sektorgröße 4096 dort (unter Windows) fehlerhafte Abbilder ergibt. Nur Wissen, kein Code.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) (Browser der Konsole schließen) und [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager), Vorbild der Payload-Verwaltung und der Regel, welche Prozesse beendet werden dürfen.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): der QR-Encoder, der die Adresse der Weboberfläche als Code zeigt (`src/qr.c`, übernommen und mit einem Lesegerät geprüft).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): Muster im Kernelprotokoll, an denen sich das laufende Spiel erkennen lässt.
- **Das Projekt [etaHEN](https://github.com/etaHEN/etaHEN)** und **[onionHEN](https://github.com/aydencharles/onionHEN)** (aydencharles): Quelltext und Dokumentation zu Systemaufrufen, Messwerten und der Bildraten-Abfrage.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)** (Nachfolger von [Sonic Loader](https://git.etawen.dev/soniciso/sonicloader)): Form der Aufrufe für die Kachel-Installation und Vorbild für viele Funktionen (Spiel beenden, Kernel-Log live, Spielzeit, Spielstände, Dateimanager, Spiele löschen); der Code ist jeweils neu geschrieben.
- **BestPig und [BackPork](https://github.com/BestPig/BackPork)**: das Prinzip der Ersatzbibliotheken (Fakelibs), das die Spieleseite erkennt und anzeigt.
- **Juma Sayeh (Entwickler) und Osama Abualia (Tests), PS5 Game Compressor**: Vorbild für die Prüfung des SDK auf Firmware 13.60 beim Bau und für fünf Ideen beim Kopieren und Konvertieren: Sicherungen nach dem Schreiben ganz zurücklesen und prüfen, den Ruhemodus bei langen Vorgängen verhindern, nur zwischen zwei verschiedenen Laufwerken gleichzeitig lesen und schreiben, Lücken in Abbildern ausdrücklich mit Nullen füllen und Blöcke unkomprimiert lassen, die weniger als 5 % sparen. Der Quelltext des Game Compressors trägt keine Lizenz, deshalb ist nichts davon übernommen: Alles ist neu geschrieben.
- **Linux-Kernel, Treiber `hid-playstation`**: Dokumentation des DualSense-Statusbytes für den Akkustand.
- **Xbox 360 DashLaunch**: Vorbild für den Gedanken der ruhigen, trägen Temperaturregelung.

**Ein ganz besonderer Dank an [Gezine](https://github.com/Gezine)**: Seine Arbeit ist für die PS5-Homebrew-Community grundlegend – ohne sie liefe auf vielen Konsolen kein Homebrew, und damit auch diese App nicht.

**Danke an die Community.** Ihr Code steckt nicht in dieser App, aber ohne ihre geteilte Arbeit gäbe es die Szene so nicht: [owendswang](https://github.com/owendswang) ([ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager), [ps5-fan-control](https://github.com/owendswang/ps5-fan-control)), [LightningMods](https://github.com/LightningMods) (etaHEN, [Itemzflow](https://github.com/LightningMods/Itemzflow)), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW) ([PPPwn](https://github.com/TheOfficialFloW/PPPwn)), [Specter](https://github.com/Cryptogenic) ([PS5-IPV6-Kernel-Exploit](https://github.com/Cryptogenic/PS5-IPV6-Kernel-Exploit)), [ChendoChap](https://github.com/ChendoChap) ([pOOBs4](https://github.com/ChendoChap/pOOBs4)), [idlesauce](https://github.com/idlesauce) ([umtx2](https://github.com/idlesauce/umtx2)), [flatz](https://github.com/flatz) ([pkg_pfs_tool](https://github.com/flatz/pkg_pfs_tool)), [Al Azif](https://github.com/Al-Azif) ([ps4-exploit-host](https://github.com/Al-Azif/ps4-exploit-host)), [zecoxao](https://github.com/zecoxao) – und allen anderen, die mit Code, Tests, Anleitungen oder Antworten beitragen. In der App stehen alle mit Bild auf der Seite „Credits“.

Die 30 Profilbilder hat der Autor selbst erzeugt. Wer sich hier vermisst oder falsch beschrieben findet: bitte melden, die Liste wird gern ergänzt.

## Lizenz

Copyright © 2026 strongt1me

Dieses Programm ist freie Software: Es darf unter den Bedingungen der **GNU General Public License**, Version 3 oder (nach Wahl) jeder späteren Version, weitergegeben und verändert werden, siehe [LICENSE](LICENSE). Es wird ohne jede Gewährleistung bereitgestellt.

Bis einschließlich 1.45.1 stand das Projekt unter der MIT-Lizenz. Ab 1.46.0 gilt GPL-3.0-or-later: Das Konvertieren von Spielen ist aus [MkPFS](https://github.com/PSBrew/MkPFS) (GPL-3.0) übertragen, und das PS5-Payload-SDK, mit dem jedes ELF gebaut wird, steht selbst unter GPLv3+. Welche fremden Teile enthalten sind und unter welcher Lizenz, steht in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Sprachen

Diese README gibt es auch auf [English](README.en.md), [Italiano](README.it.md), [Español](README.es.md), [Français](README.fr.md) und [Русский](README.ru.md). Das Entscheidungsprotokoll, die Entwicklerdokumente und die Schnittstellenbeschreibung sind deutsch; Handbuch und FAQ liegen jeder Veröffentlichung in allen sechs Sprachen als PDF und HTML bei und sind in der App eingebaut. Fehlende oder holprige Übersetzungen gern als Issue („Translation“) melden; Näheres zum Ergänzen steht in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md#übersetzungen).
