# PS5 Cooling & System Center – Pro v1.89.0

## Highlights
- **Beim Konvertieren in LZ4- oder zstd-Packs packen:** Im Fenster „Konvertieren“ gibt es bei exFAT und ffpkg die Wahl „Packen (AMPR-Packs)“: *Nicht packen*, *LZ4-Packs* oder *zstd-Packs (nur AMPR EMU NG)*. Dump Forge packt das Spiel direkt beim Schreiben ins Abbild, ohne Zwischenordner; das Original bleibt unverändert. Browser-App und Vollbild-App bieten es beide an.
- **Firmware 1.00 bis 14.22:** Die App (ELF und beide Kacheln) wird jetzt mit dem PS5-Payload-SDK **v0.50** gebaut, dessen Startcode jede Firmware von 1.00 bis 14.22 kennt (v0.45 kannte 4.03 bis 14.10). Eine Firmware, die der Startcode nicht kennt, kommt nicht bis `main()`; das entscheidet also, auf welchen Konsolen die App überhaupt startet.
- **Lüfterdrehzahl als Ring (Browser-App):** Die Drehzahl in Prozent steht wie in der Vollbild-App als Ring, nicht mehr als Tacho.

## Neu
### Konvertieren
- **Packen beim Konvertieren (exFAT und ffpkg eines PS5-Spielordners):** LZ4 (Stufe 9, 3 oder 12; Laufzeit AMPR EMU NG oder „APR-EMU Original“) oder zstd (Stufe 5, 1, 9 oder 19; zstd liest nur AMPR EMU NG, die Dump Forge ins Spiel legt). Die Quelle wird zweimal gelesen (erst vermessen, dann geschrieben), es dauert also länger als ohne Packen; eine `.sha256`-Datei entsteht nicht; „Prüfung danach“ (gründlich oder schnell) gilt wie bisher.
- **Nicht möglich (ausgegraut, mit dem Grund daneben):** PS4-Spiele (sie haben keine AMPR-Packs), Spiele, die schon in Packs liegen (erst bei den Asset-Packs des Spiels entpacken), eine Quelle, die sich nicht lesen lässt, fehlendes Dump Forge (Knopf „Dump Forge laden“) oder eines unter 0.5.6 und Namen über 63 Bytes ohne Endung.
- **Die Auftragskarte sagt, was geschieht:** „Das Spiel wird für die Packs vermessen …“, dann „NN % · wird gepackt und geschrieben“, dann die Prüfung; am Ende „24 GB statt 38 GB (63 %)“. Warteschlange und Verlauf nennen die Wahl („zu exfat + LZ4“).

### Firmware und Bau
- **PS5-Payload-SDK v0.50 statt v0.45:** offizielle Release-Datei, SHA-256 geprüft. Nur sieben Dateien sind anders (der Startcode `crt1.o`, zwei Kopfdateien, zwei Bibliotheken und zwei Werkzeuge). Der Bau prüft jetzt 21 Firmware-Stände von 1.00 bis 14.22 am SDK und am fertigen Programm; die Systemseite nennt die Firmware-Gruppen über den ganzen Bereich (5.xx bis 8.xx hieß bisher „unbekannt“). Das SDK der Vollbild-App ist ebenfalls auf v0.50 festgelegt.

### Spiele
- **APR-EMU- und PlayGo-Datei: Ablage wählbar:** Bei einem Abbild (`.exfat`, `.ffpkg`) kann die Datei ins Abbild oder in den Backport-Ordner daneben (`backports/<Titel-ID>/fakelib`); ist das Abbild voll („No space left on device“), legt die App sie von selbst in den Backport-Ordner. Bei einem komprimierten `.ffpfsc` und bei einem vorhandenen Backport-Ordner steht die Ablage fest.
- **Das Versionsfenster der Vollbild-App nennt auch zstd,** nicht nur LZ4 (ein LZ4-gepacktes Spiel braucht eine LZ4-Fassung, ein zstd-gepacktes AMPR EMU NG).

### Übersetzungen
- **Zusammengesetzte Texte bleiben nicht mehr halb deutsch:** Ein Muster wie „{0} Min.“ nahm „6 Payloads · Stand vor 40“ als seine Zahl und ließ den Rest unübersetzt; ein Wert, der über ein „ · “ reicht, gilt nicht mehr. Elf fehlende Texte in sechs Sprachen ergänzt, drei Datumsangaben in der Sprache der Seite, die Vollbild-App schreibt „47.8 GB“ auf Englisch mit Punkt.
- **Lücken im Konvertieren-Fenster geschlossen:** der Titel, die Formatnamen, der UFS2-Satz und zwei Prüf-Meldungen in allen sechs Sprachen; die Beschreibung beim Knopf „Anleitung“ in der Vollbild-App.

## Hinweise
- **An der Konsole bestätigt (Slim, Firmware 12.00, 20.11.2026):** Packen beim Konvertieren mit einem kleinen Spiel (1,75 GB, ein Ordner): **LZ4 Stufe 3 → 1,21 GB (69,5 %)** in 133 s, **zstd Stufe 5 → 1,03 GB (59,2 %)** in 113 s, jeweils mit gründlicher Prüfung; das Spiel startete aus jedem der beiden Abbilder und lief (vom Nutzer bestätigt). Eine Messung mit einem kleinen Spiel und einem USB-Stick (beim Schreiben etwa 10 bis 15 MB/s), also keine Aussage über große Spiele. Nicht probiert: die Pro, die Laufzeit „APR-EMU Original“, ffpkg als Ziel.
- **ShadowMountPlus ignoriert ein Abbild, dessen Titel-ID schon als Ordner da ist** („Duplikat … ignoriert“): Um das Spiel aus so einem Abbild zu starten, muss das Original vorher aus den Suchordnern. Beim Start meldete ShadowMountPlus bei beiden Abbildern „sandbox fakelib unavailable (Device busy)“; das Spiel lief trotzdem.
- **An der Konsole nicht probiert:** die Wahl des Backport-Ordners bei vollem Abbild (nur die Abfrage wurde geprüft) und der Firmware-Bereich über 12.00 hinaus – der Startcode kennt 1.00 bis 14.22, getestet ist die App aber nur mit 12.00.
- 36 Host-Tests mit der Lüftersimulation und 382 Tests der Vollbild-App bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Alles andere ist wie in v1.88.1.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.89.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung (Deutsch) |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Dank
An alle, deren Arbeit in der App steckt oder sie möglich macht – die ganze Liste steht in der App unter „Credits“ oder Profil → Dank. Besonders **quer3q** für Dump Forge (es packt), AMPR EMU NG und Kraken Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager sowie **John Törnblom** und die Mitwirkenden am PS5-Payload-SDK (v0.50 mit jeder Firmware von 1.00 bis 14.22).

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.88.1...v1.89.0
