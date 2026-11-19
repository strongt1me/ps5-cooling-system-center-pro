# PS5 Cooling & System Center – Pro v1.86.7

## Highlights
- **„Original danach“ klappt jetzt auch bei einem Spiel, das ShadowMountPlus eingehängt hat:** Beim Löschen oder Verschieben des Originals hängt die App den Titel bei ShadowMountPlus zuerst aus und meldet ihn dann ab. Vorher lehnte ShadowMountPlus das Abmelden eines eingehängten Titels ab („title runtime could not be released; its mount or sandbox is still busy“), und das Original blieb neben der geprüften Kopie liegen.

## Fehlerbehebungen
- **Prüfrunde zu „Original danach“ (23 Funde, 19 bestätigt):** Solange ein Original kopiert oder gelöscht wird, startet kein anderer Auftrag der App mehr dazwischen (vorher konnte die Warteschlange den nächsten Eintrag starten, und Original und Kopie blieben beide liegen). Das Abbild eines Entpackens, das ShadowMountPlus ohne Ergebniswert beendet, wird bei „Löschen“ nicht gelöscht. Beide Oberflächen schicken den Pfad des Originals mit; liegt der Titel beim Start woanders, lehnt die App ab. Gelöscht wird nur, was ein Spiel ist (Ordner mit `sce_sys/param.json`, Abbild mit bekannter Endung). „Verschieben“ kopiert den Pfad, nicht den Titel. Ein Neustart der App mitten in der Wahl wird beim nächsten Start gemeldet. Teilweise Gelöschtes wird als solches gemeldet.
- **„Verschieben“ auf demselben Laufwerk geht jetzt sofort:** Liegt der Zielordner auf dem USB- oder M.2-Laufwerk des Originals, wird es nur umbenannt (keine Kopie, kein freier Platz nötig). Auf der internen SSD und wenn das Dateisystem es verweigert, wird wie bisher kopiert, geprüft und gelöscht.
- **Weitere kleine Korrekturen:** FAT32-Ziele mit Dateien über 4 GB werden gleich bei der Prüfung abgelehnt; ein unfertiger Rest einer früheren Kopie am Ziel wird ersetzt; lange Zielpfade mit Umlauten gehen bei der Prüfung nicht mehr verloren; die Zeitgrenze der Kopie sind 24 Stunden.
- **Werkzeuge:** `deploy.sh` sendet nicht, solange die App arbeitet; `release.sh` führt vorher die Host-Tests aus; `make release` kennt alle Release-Dateien.
- **Original verschieben oder löschen bei einem eingehängten Titel:** Führt ShadowMountPlus den Titel als eingehängt (das Spiel läuft nicht), hängt die App ihn über dessen eigene Schnittstelle aus, bevor sie ihn abmeldet; ein Titel, der läuft, wird nie ausgehängt. Lehnt ShadowMountPlus das Abmelden mit „busy“ ab, versucht die App es nach drei Sekunden noch einmal, insgesamt dreimal. Jede andere Ablehnung beendet es sofort.
- Dasselbe gilt für „Spiel löschen“ bei einem Titel von ShadowMountPlus.
- **Unverändert sicher:** Wird das Abmelden am Ende doch abgelehnt, löscht die App nichts, sagt es und lässt das Original und die Kopie liegen.

## Hinweise
- **An der Konsole geprüft** (Spielordner mit 132 Dateien, 1,6 GB, auf einem USB-Laufwerk, Titel eingehängt): Konvertieren zu `.ffpfsc` mit „Original verschieben“ auf die M.2-Erweiterung. Vor der Korrektur lehnte ShadowMountPlus das Abmelden ab; mit ihr wurde der Titel ausgehängt, abgemeldet und das Original nach der geprüften Kopie gelöscht. Damit ist auch ein USB-Laufwerk als Quelle und die M.2-Erweiterung als Ziel geprüft.
- **An der Konsole geprüft (19.11.2026, Testkopie mit eigener Titel-ID):** „Verschieben“ auf dasselbe USB- und auf dasselbe M.2-Laufwerk (nur umbenannt, in Sekunden, 132 Dateien unverändert); Entpacken eines `.ffpfsc` und eines `.exfat` über ShadowMountPlus mit „Löschen“ (ShadowMountPlus meldet einen Ergebniswert, das Abbild wurde gelöscht); Neustart der App mitten in einer Wahl (keine Daten verloren, die Meldung kommt beim nächsten Start); `deploy.sh` sendet nicht, solange die App arbeitet; die Vollbild-App am Controller: Konvertieren mit „Original danach: Löschen“.
- **Noch nicht geprüft:** „Verschieben“ und „Entpacken“ in der Vollbild-App am Controller (sie nutzen dieselbe Anfrage wie „Löschen“, getestet nur am PC); das Umbenennen auf einem Laufwerk mit FAT32.
- Ein **PFS-Abbild** (`.ffpfs`) lässt sich weiterhin nicht entpacken (ShadowMountPlus meldet „Permission denied“, das eigene Entpacken der App kennt das Format nicht); das Original bleibt dann liegen.
- Ob der Payload Manager von itsPLK der Weiterleitung von `latest/download` bei einer Quellliste folgt, ist nicht belegt.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Alles andere ist wie in v1.86.6.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.7.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: als Quelle `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge, **drakmor** für ShadowMountPlus sowie **itsPLK** für den Payload Manager.
