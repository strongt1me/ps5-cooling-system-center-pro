# PS5 Cooling & System Center - Pro (v1.49.0)

**Lüftersteuerung, Temperaturüberwachung und komplette Systemzentrale für die gejailbreakte PS5, mit deutscher Weboberfläche für Handy, Tablet und PC im Heimnetz.**

Aus dem kleinen Lüfterprogramm ist über die Versionen 1.43 bis 1.49 eine ganze Verwaltung geworden: Kühlung, Spiele (starten, kopieren, konvertieren, löschen), Spielzeit mit Temperaturen, Spielstände sichern, Pakete finden, aufteilen und installieren, Payload-Verwaltung, Dateimanager, Kernel-Log live, Profil und mehr. Alles läuft **auf der Konsole selbst**, ohne PC, ohne Internet. Die App sendet keine Daten nach außen.

---

## 1. Was ist das?

Ein Homebrew-Payload (ELF). Du sendest es an Port 9021, es startet einen eigenen kleinen Webserver (Port 8086) und regelt den Lüfter. Alles Weitere bedienst du im Browser unter `http://<PS5-IP>:8086`, am Handy genauso wie am PC. Eine Kachel im Startmenü öffnet dieselbe Oberfläche direkt im Browser der Konsole.

Es ist der Nachfolger des „PS5 Temperature Manager“. Backend (C) und Oberfläche sind komplett neu geschrieben.

---

## 2. Funktionen im Überblick

### Kühlung und Messwerte

- **Komfortregelung.** Hält eine Zieltemperatur (Standard 66 °C, einstellbar 60–78 °C) mit gleitendem Mittelwert, Totzone, Trend und Drehzahländerung in 1-%-Schritten. Ziel ist nicht der niedrigste Wert, sondern ein Lüfter, der ruhig und gleichmäßig klingt (Vorbild: die Temperaturregelung von Xbox 360 DashLaunch). Ein Servo liest die tatsächliche Drehzahl zurück und kalibriert sich selbst.
- **Betriebsarten:** Automatik (Komfortregelung) oder Beobachten (nur messen). **Schnellwahl** Kühl / Ausgewogen / Leise und **eigene Regeln je Spiel**.
- **Sicherheit zuerst.** Ab der Sicherheitstemperatur (Standard 78 °C) zählt nur noch die Hardware. Läuft die App nicht, regelt die Konsole mit ihrer eigenen Kennlinie. Beim Beenden gibt die App den Werkswert der Lüfterschwelle zurück.
- **Messwerte:** Prozessor, Hauptchip, **Grafiktemperatur (nur PS5 Pro)**, Lüfterdrehzahl, **Last aller 16 CPU-Threads**, **Takt live** (GPU, CPU, Fabric, Speicher), **Stromaufnahme der Spannungsschienen** (GPU, Speicher, CPU/SoC, gesamt), **Bildrate (FPS) im Spiel**, **Controller-Akku**, aktivste Threads.
- **Verläufe:** 2 Minuten, 24 Stunden und eine Wochenauswertung der Kühlleistung.
- **Läuft gerade:** erkennt Start, Ende und Titel-ID des laufenden Spiels, auch pausiert im Hintergrund.
- **Meldungen oben rechts auf dem Fernseher** mit eigenem Symbol: beim Start (mit Adresse), bei Warnungen und auf Wunsch regelmäßig. **Mikrofon-Taste des Controllers zweimal drücken** zeigt Prozessortemperatur und Lüfter, auch ohne geöffnete Weboberfläche. Das Mikrofon bleibt dabei, wie es war.
- **Warn-Countdown, Ampelstatus mit Klartext-Grund, Lüfterdiagnose (Soll/Ist), Rohsensoren und Expertenmodus** für alle, die tiefer hineinschauen wollen.

### Spiele

- **Bibliothek** wie auf dem Startbildschirm: Cover, Titel-ID, Content-ID, Version, Größe, installiert am, Spielzeit, zuletzt gespielt. Suchen, filtern (Alle / PS5 / PS4 / Backport-AMPR-PlayGo) und sortieren. Zeigt je Spiel das **Format** (Dump-Ordner, exFAT, ffpkg, ffpfs, ffpfsc, PS4 PKG, PS5 PKG), die benötigte Firmware (gelb, wenn höher als deine) und erkennt **Backport, AMPR EMU und PlayGo** (nur Anzeige).
- **Starten:** direkt, oder ein laufendes Spiel nach vorn holen. Der Browser der Konsole schließt sich dabei von selbst. Läuft schon ein anderes Spiel, kommt ein Hinweis; dann **„Spiel beenden“** (sofort, ohne Rückfrage) und danach mit eigenem Tipp starten. Ein Titel wird nie doppelt gestartet (das kann die Konsole abstürzen lassen).
- **Kopieren** auf andere Laufwerke (interne SSD, M.2, USB) nach `homebrew` (ShadowMountPlus findet es) oder in einen Sicherungsordner. Mit Fortschritt, Abbrechen-Knopf und Platzprüfung. Nichts wird überschrieben.
- **Verschieben und Entpacken** über die ShadowMountPlus-Schnittstelle.
- **Konvertieren auf der Konsole, ohne PC** (eigener C-Code):
  - **exFAT-Abbild** und **ffpfsc** (komprimiert, Portierung von MkPFS, libdeflate, rund **190–220 MB/s**)
  - **ffpkg / UFS2** im 64-KiB-Aufbau, den ShadowMountPlus empfiehlt (Dateien bis 4 TB)
  - Auch exFAT/ffpkg/ffpfs → ffpfsc.
- **Alles wird geprüft:** Kopien und Abbilder werden nach dem Schreiben ganz zurückgelesen und verifiziert (CRC, Blockprüfung, Dateisystem-Aufbau). Daneben liegt eine **`.sha256`-Datei**, die du später am PC mit `sha256sum -c` prüfen kannst.
- **Ruhemodus-Sperre** bei langen Vorgängen, paralleles Lesen/Schreiben zwischen zwei Laufwerken, 5-%-Regel bei der Kompression.
- **Spiele und Sicherungen löschen:** installierte Spiele (auch Fake-PKGs) über die Deinstallation der Konsole samt Updates und Zusatzinhalten, ShadowMountPlus-Spiele (abmelden, dann Abbild löschen), App-Sicherungen. Vorher zeigt ein Fenster genau, was weggeht; gelöscht wird erst nach Häkchen und zwei Klicks, mit Fortschrittsbalken. **Spielstände bleiben immer erhalten**, ein laufendes Spiel wird nie gelöscht.
- **Covers & Metadaten speichern:** optionaler Zwischenspeicher, damit die Liste schneller lädt (auch wenn ein Stick gerade nicht steckt).

### Reiter „Spielzeit“

Schreibt selbst mit, wann du gespielt hast, wie lange und **wie warm die Konsole dabei wurde**. Summen für heute, 7 Tage, 30 Tage und insgesamt, Tagesbalken der letzten 14 Tage, Rangfolge „Meistgespielt“ mit Höchsttemperatur, die letzten Sitzungen mit CPU/SoC-Höchstwert und mittlerer Lüfterdrehzahl, Marken für Warnung/Notfallmodus. Export als Tabelle (CSV für Excel/Calc).

### Reiter „Spielstände“

Sichert die Spielstände aller Benutzer (PS4 und PS5) auf USB, Platte oder in den Konsolenspeicher: **unverändert und verschlüsselt**, jede Datei zurückgelesen, SHA-256 in der Liste. **Zurückspielen** pro Titel: erst wird die Sicherung geprüft, dann der jetzige Stand eigens gesichert („Stand vor dem Zurückspielen“), dann wird atomar ersetzt und alles nochmal verglichen. Läuft ein Spiel, geht nichts.

### Reiter „Pakete“

- **Finden:** `.pkg` auf USB, Platten, Disc und im Konsolenspeicher, mit Bild, Titel, Version, Art (Spiel/Update/Zusatzinhalt), PS4 oder PS5.
- **Aufteilen** großer Pakete für **FAT32-Sticks (4095 MB)** oder Blu-ray (25/50 GB) oder eigene Größe. Zurückgelesen, geprüft, das Original bleibt unverändert. Kompatibel mit dem Teile-Format des PS5 PKG Managers.
- **Installieren** über die Installation der Konsole selbst. Vorher ein Plan: was im Weg stünde (schon installiert, läuft gerade, Datei verändert, zu wenig Platz) in Rot und Gelb. Fortschritt mit Prozent, Bytes und Restzeit. **Überschreibt und löscht nichts.** An der Konsole bestätigt mit LEGO Batman (31,8 GB, rund 37 MB/s) und Moorhuhn.

### Payloads

Laufende Payloads ansehen und beenden. Eigene `.elf` aus dem Ordner `/data/PS5-Cooling-Center/payloads` oder von einem USB-Stick (Hauptordner und `payloads`) **ohne PC starten** und in den internen Ordner kopieren. Es werden nur gültige 64-Bit-ELF-Dateien gestartet, alles andere bleibt markiert und gesperrt.

### Dateimanager (neu in 1.49.0)

Ordner der ganzen Konsole ansehen und Dateien herunterladen. Auf den Laufwerken und in `/data`: hochladen (mehrere Dateien, Fortschritt, am PC per Ziehen), neuer Ordner, umbenennen, **kopieren, ausschneiden, einfügen** mit Balken und Abbrechen, löschen mit Plan und zwei Klicks. Überall sonst „Nur ansehen“. Nichts wird überschrieben, Verknüpfungen werden nie verfolgt, der Ordner der App ist geschützt. An der Konsole benutzt: 2 GB kopiert mit rund 130 MB/s.

### Protokoll und Kernel-Log live

- Ereignisprotokoll der App, als `.log` exportierbar.
- **Kernel-Log live** direkt in der Oberfläche (kein `klogsrv` und kein `nc` am PC nötig): Filter mit Wörtern und `-Ausschluss`, Schnellwahlen (Ohne Rauschen, Diese App, Spiele, Fehler), Pause, Mitlaufen, **Speichern auf der Konsole**, Herunterladen und **Aufnahme**, bei der die Konsole selbst mitschreibt, auch bei geschlossener Seite und bis zu einem Absturz.

### Profil, System, Kopfzeile, Kachel

- **Profil:** Anzeigename ändern, Profilbild aus **30 eingebauten Bildern** oder eigener Datei, mit Sicherung des alten Bildes.
- **System:** Modell, Firmware, Laufzeit, Speicher, Netzwerk, Diagnose, Kachel installieren, Anzeige auf dem Fernseher.
- **Power-Optionen** in der Kopfzeile jeder Seite: Ruhemodus, Neu starten, Ausschalten, Abgesicherter Modus (jeweils zwei Klicks). Dazu Vollbild und Expertenmodus.
- **Startmenü-Kachel**, die die App beim ersten Start selbst anlegt (kein Installer nötig).
- **Credits-Seite** mit allen Entwicklern, deren Arbeit in der App steckt.

---

## 3. Was kam wann dazu?

| Version | Highlights |
| --- | --- |
| 1.43–1.44 | Spielerkennung, Anzeige per Taste, neue Kachel |
| 1.45 | Stromschienen, Last aller 16 CPUs, Takt live, FPS im Spiel, Meldungen oben rechts mit Symbol |
| 1.45.1 | Grafiktemperatur (PS5 Pro), falsche Sensorbeschriftungen entfernt |
| 1.46.0 | Seite „Spiele“ (Start, Kopieren, Verschieben, Konvertieren), Payload-Verwaltung, Profilbilder, Mikrofon-Taste, Lizenz GPL-3.0 |
| 1.47.0 | Spiel beenden, Kachel ohne Installer, Kernel-Log live, Firmware bis 13.60 (SDK v0.43), Sicherungen mit Zurücklesen und `.sha256` |
| 1.48.0 | Spielzeit, Spielstände, Pakete (finden, aufteilen, installieren), Covers & Metadaten, ffpkg im 64-KiB-Aufbau, Power-Optionen |
| **1.49.0** | **Dateimanager, Spiele und Sicherungen löschen, Credits-Seite, Installieren läuft bis zum Ende, klare Meldung bei abgemeldetem USB-Laufwerk** |

---

## 4. So wird es benutzt

1. Aus den Releases `PS5_Cooling_System_Center_v1.49.0.elf` laden.
2. An die Konsole senden, Port **9021**, mit einem beliebigen Payload-Sender oder:
   `nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.49.0.elf`
3. Auf dem Fernseher erscheint eine Meldung mit der Adresse.
4. Im Browser öffnen: **`http://<PS5-IP>:8086`**
5. Nach jedem Neustart der Konsole die ELF erneut senden (z. B. per Autoloader). **Nur eine Instanz** gleichzeitig, sonst regeln zwei den Lüfter gegeneinander. Eine alte Instanz beendest du auf der Seite „Payloads“.

Seiten der Oberfläche: **Profil · Spiele · Payloads · Kühlung · System · Protokoll · Dateien · Credits**.

---

## 5. Voraussetzungen

- PS5 mit Jailbreak und ELF-Lader auf **Port 9021** (z. B. elfldr)
- **kstuff** für die Lüftersteuerung. Ohne kstuff laufen Sensoren und Oberfläche weiter, die Regelung meldet „nicht verfügbar“.
- Browser im selben Netz
- optional: **ShadowMountPlus** (Verschieben/Entpacken, Format- und Speicherort-Erkennung, Löschen von Abbildern)
- Gebaut mit dem PS5-Payload-SDK **v0.43**, dessen Startcode die Firmware **bis 13.60** kennt.

**Getestet:** PS5 Pro (CFI-7021), Firmware **12.00**. Andere Modelle und Firmware-Stände sind **ungetestet**. Ob Lüftersteuerung und Sensoren auf 13.xx genauso arbeiten, ist nicht geprüft. Die Grafiktemperatur gibt es nur auf der Pro.

---

## 6. Wichtige Hinweise

- **Keine Anmeldung.** Jedes Gerät im Heimnetz kann lesen und ändern. **Keine Portweiterleitung auf Port 8086!** Wer das nicht will, setzt `bind_address` auf `127.0.0.1`. Der Server weist Anfragen ab, die erkennbar von fremden Webseiten kommen (Host-/Origin-Prüfung, Content-Security-Policy).
- **Löschen ist endgültig** (Spiele, Sicherungen, Dateien): kein Papierkorb.
- **„Spiel beenden“ fragt nicht nach.** Nicht Gespeichertes geht verloren.
- **Noch nicht an der Konsole geprüft:** Pakete aufteilen, und ob die Konsole ein neu erzeugtes `.ffpkg` einhängt. `.ffpkg` aus Fassungen vor 1.48.0 bitte neu erzeugen.
- **Abgebrochene Paket-Installation:** Die Konsole reserviert für große Spiele den ganzen Platz. Den gibst du in der Download-Liste der Konsole mit „Löschen“ wieder frei.
- **USB-Abbrüche („Device not configured“)** waren bei uns das Kabel. Die App sagt es jetzt in Klartext.
- **Spielzeit der App** beginnt mit dem ersten Start der Fassung, die sie zählt, und ist nicht die Spielzeit der Kartenanzeige der Konsole.
- **Keine Piraterie.** Keine Spiele, Firmware, Schlüssel oder Kopierschutz-Umgehung enthalten, nichts wird aus dem Internet geladen. Kopieren, Konvertieren, Aufteilen und Installieren wirken nur auf Dateien, die du selbst auf der Konsole oder an einem Laufwerk hast. Hilfe zu Raubkopien gibt es hier nicht.
- **Kein Sony-Produkt**, ohne jede Gewährleistung und Haftung. Das Modifizieren der Konsole geschieht auf eigenes Risiko (Garantie, Sperre, rechtliche Folgen je nach Land).

---

## 7. Lizenz und Dank

**GPL-3.0-or-later** (ab 1.46.0; der MkPFS-Port und das SDK verlangen es). Eigener HTTP-Server ohne Fremdbibliothek, dokumentierte JSON-Schnittstelle.

Ein riesiges Dankeschön an die PS5-Homebrew-Community, besonders an **Gezine** (ohne dessen Arbeit liefe auf vielen Konsolen kein Homebrew), **John Törnblom und ps5-payload-dev** (SDK, elfldr, klogsrv, ftpsrv, websrv), die **kstuff**-Entwickler (sleirsgoevy, EchoStretch, drakmor), **RenanGBarreto, rdmrocha und PSBrew** (MkPFS), **SvenGDK** (UFS2Tool), **itsPLK** (PS5 PKG Manager, Autoloader, Payload-Manager), **phantomptr** (ps5upload), **drakmor** (ShadowMountPlus, ps5-hwinfo), **Soniciso** (Elf Arsenal), **kerrdec97** (exFAT Image Builder), **StonedModder**, **BestPig**, Eric Biggers (libdeflate), Dave Gamble (cJSON) und viele mehr. Alle stehen mit Bild auf der Seite „Credits“ in der App.

---

## 8. Fehler melden

Bitte mit Konsolenmodell, Firmware, Programmversion und, wenn vorhanden, dem exportierten Protokoll (Seite „Protokoll“) und ggf. dem Kernel-Log. Wünsche sind willkommen.
