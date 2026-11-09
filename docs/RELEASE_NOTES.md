# PS5 Cooling & System Center – Pro v1.79.0

## Highlights
- **ShadowMountPlus 1.7beta12:** Die App liest und schreibt die neue `config.ini` (Abschnitte, `update_enabled`) genauso wie die alte und ändert immer die richtige Zeile – Standard ist „Automatisch erkennen“
- **ShadowMountPlus hat einen eigenen Platz:** im Browser als Karte ganz oben auf der Seite **System**, in der Vollbild-App als eigener Reiter unter **System**, wo man die Version in einem Auswahlfenster wählt
- **Neuer Schalter „Fakelib automatisch aktualisieren“:** Die Aktualisierung von ShadowMountPlus (sein Standard „an“) lässt sich wieder einschalten, nachdem man sie für AMPR EMU NG abgeschaltet hat
- **Firmware 4.03 bis 14.10** steht jetzt im README („FW 4.03 bis 14.10 Support“)

## Neu

### ShadowMountPlus 1.7beta12
- Ab 1.7beta12 benutzt ShadowMountPlus ein neues Format der `config.ini` (`[config]`, `version = 2`, Abschnitte; die Einstellung heißt `update_enabled` unter `[emulators]`) und wandelt die alte Datei beim ersten Start um. Die App versteht beide Formate und liest und schreibt sie so wie ShadowMountPlus selbst: Kommentare, Anführungszeichen, Leerzeichen, SMB-Zugänge und Zeilenenden bleiben, wie sie sind, geändert wird nur der eine Wert.
- Neue Einstellung **„Version“:** Automatisch erkennen (Standard), Vor 1.7beta12 oder Ab 1.7beta12. Die Datei bestimmt, welche Zeile geschrieben wird; die Angabe gilt für eine fehlende oder leere Datei und löst einen Hinweis aus, wenn sie nicht zur Datei passt (etwa wenn ShadowMountPlus nach dem Update noch nicht gestartet wurde).
- Alle Hinweise, Warnungen und Fragen nennen die Zeile, die die App wirklich ändert (`update_emulators` oder `update_enabled`).
- Wo man es findet: Browser auf der Seite **System** ganz oben in der Karte **ShadowMountPlus**; Vollbild-App unter **System** im Reiter **ShadowMountPlus** (die Version wählt man dort in einem Fenster mit den drei Antworten).

### Fakelib automatisch aktualisieren
- Die Frage „ShadowMountPlus umstellen?“ beim Einbauen von AMPR EMU NG schaltet die automatische Fakelib-Aktualisierung von ShadowMountPlus ab. Dafür gibt es jetzt auch den Weg zurück: ein Schalter in der Karte (Browser) und im Reiter ShadowMountPlus (Vollbild-App), der sie wieder einschaltet. Er ändert nur die eine Zeile in `/data/shadowmount/config.ini`.
- `GET /api/v1/config` meldet den Stand als `smp_update_emulators` (nur lesend).

### Fenster eines Spiels
- Das Fenster und sein Knopf heißen jetzt „LZ4-/zstd-Asset-Packs & APR-EMU“, damit man sieht, dass man dort auch zstd einstellt; in der Vollbild-App nennt die Zeile unter dem Titel der Extras zstd ebenfalls.

### Firmware
- Der Startcode der ELF (SDK v0.45) kennt die Firmware **4.03 bis 14.10**. Nutzer melden, dass die App auch auf 4.xx bis 10.xx und auf 13.60 läuft; selbst getestet ist die Firmware 12.00. README, FAQ und Handbuch sagen das in allen sieben Sprachen.

### Fremdbausteine
- **ShadowMountPlus 1.7beta12** (drakmor, Vorabfassung) geprüft: Die Schnittstelle (Port 10101) ist unverändert, neu ist das Dateiformat „v2“ der `config.ini` und die `profiles.ini`.
- **ps5upload bis v12.3.0** (phantomptr) nur gelesen; nichts davon betrifft Start oder Sicherheit der App.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- **Geprüft:** der Schreibweg gegen den echten Leser aus dem Quelltext von ShadowMountPlus 1.7beta12 am PC (11 Fälle); an der PS5 Pro (Firmware 12.00, `config.ini` im neuen Format) die Einstellung, der Hinweis, die Karte und der ganze Ablauf im Browser (Rückfrage, Speichern, „umstellen?“, NG-Einbau) an einer entpackten Testkopie von Double Dragon Revive – die echte `config.ini` blieb dabei byte-gleich.
- **Nicht geprüft:** die Vollbild-App auf dem Fernseher (Reiter und Auswahlfenster; am PC nur als Bild gesehen) und das Umschalten auf „an“ in der echten `config.ini`.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.79.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **drakmor** für ShadowMountPlus und die Hinweise zur neuen `config.ini`.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.78.0...v1.79.0
