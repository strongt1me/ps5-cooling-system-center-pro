# PS5 Cooling & System Center – Pro v1.93.0

## Highlights
- **NTFS-Laufwerke im Dateimanager:** Ein Stick oder eine Festplatte mit NTFS (dem Dateisystem von Windows), die die Konsole selbst nicht einhängt, erscheint als `ntfs0` und lässt sich lesen und beschreiben: ansehen, herunterladen, hochladen, neuer Ordner, kopieren und verschieben in beide Richtungen, löschen, sicher auswerfen. Im Browser und in der Vollbild-App, mit dem Treiber von PS5-NTFS-BROWSER (abcde6543).
- **Sicher bei kaputten oder präparierten Sticks:** Der NTFS-Treiber ist gegen beschädigte Datenträger gehärtet (Fuzzing mit über 22 Millionen Durchläufen, Abbilder, die den Original-Treiber hängen oder abstürzen lassen). Ein Stick, den Windows nicht ganz freigegeben hat oder der mitten im Schreiben abgezogen wurde, bleibt nur lesbar, mit Grund und Anleitung.
- **Firmware-Marke nach dem SDK des Spiels:** Verlangt die `param.json` eine neuere Firmware, die `eboot.bin` ist aber mit einem passenden SDK gebaut, bleibt die Marke grau und sagt, warum das Spiel trotzdem startet. FPKGMount-Spiele (Kraken Mount) werden jetzt auch auf einen Backport geprüft.

## Neu
### Dateimanager: NTFS
- **Lesen und Schreiben:** Jede NTFS-Partition (MBR oder GPT) wird ein eigener Ort (`ntfs0`, `ntfs1` …). Umbenennen, Bearbeiten und Ersetzen gehen nicht – der Treiber ändert vorhandene Dateien nie; bei vergebenen Namen gibt es „Beide behalten“ und „Überspringen“. Namen, die Windows nicht erlaubt, werden mit `_` angepasst. Spiele lassen sich von NTFS nicht starten.
- **Nur lesbar, mit Grund:** Ohne „Sicher entfernen“ abgezogen, Schnellstart, chkdsk nicht fertig, Ruhezustand einer Windows-Systemplatte, an der PS5 mitten im Schreiben abgezogen – der Hinweis sagt jeweils, was am Windows-PC zu tun ist.
- **Sicher auswerfen:** Knopf am NTFS-Ort (Vollbild: Quadrat-Taste). Schreibt Ausstehendes aus, leert den Schreib-Cache des Sticks und gibt ihn frei („ntfs0 kann jetzt abgezogen werden.“).
- **Vorsicht beim Einstecken:** Die App öffnet nur ganze Platten, eine frisch eingesteckte erst nach 10 s, nie während eines Auswerfens, und versucht eine Platte, die sich nicht öffnen lässt, nur noch in wachsenden Abständen.
- **Hochladen in ganzen Blöcken:** auf einem langsamen Stick 2,8 MB/s statt vorher 17 KB/s mit Abbruch.

### Spiele
- **Firmware-Marke und Backport bei FPKGMount-Spielen:** Siehe Highlights. Die Vollbild-App zeigt in den Details die Zeile „Gebaut mit SDK“.
- **PS4Mount:** PS4-Spiele auf der M.2-Erweiterung werden nicht mehr angemeldet (die Konsole bricht ihren Start ab); sie stehen unter „Gefunden, nicht eingebunden“ mit Grund. Direkt nach dem Anmelden startet ein Spiel sofort (vorher kam „nicht in der Spieleliste“).
- **Start bei „Device busy“:** Lehnt ShadowMountPlus einen Start kurz ab, versucht die App es noch zweimal im Abstand von 3 s.

### Gesamtprüfungen
- Zwei Durchsichten der ganzen App mit mehreren Prüfern, Gegenprüfungen und Belastungstests. Behoben unter anderem: Auswerfen eines USB-Laufwerks, während PS4Mount sucht; Abbilder mit Ordnern, die auf sich selbst zeigen; exFAT-Ordner anderer Werkzeuge („NoFatChain“), die als beschädigt galten; doppelter Spielstart; der Payload-Store ersetzt eine fremde Datei nur nach Rückfrage; zstd-gepackte Spiele heißen im AMPR-Fenster richtig.
- **Vollbild-App:** Eine Spielstand-ZIP auf einem Laufwerk lässt sich löschen (fragt zuerst); vor dem Installieren eines Pakets fragt sie den Plan; Konfliktfrage, Hinweiskarten und über 30 Übersetzungen berichtigt.

## Hinweise
- **An der Konsole erprobt** (Slim, Firmware 12.00): NTFS mit zwei Sticks (250 GB und 60 GB) – Hochladen, Lesen mit Prüfsumme, Kopieren in alle Richtungen, Verschieben, Löschen, Auswerfen, Wiedereinstecken nach 10 s; ein Stick, der am Windows-PC und einer, der an der PS5 mitten im Schreiben abgezogen wurde, erscheint nur lesbar mit dem passenden Grund. Alle 80 lesenden Routen und 31 Fehl-Anfragen ohne Problem, Kernel-Log ohne Absturz.
- **Bekannt:** Sehr große Ordner auf NTFS (etwa 100 000 Dateien) sind langsam; auf einem langsamen Stick kostet jede neue kleine Datei ein bis drei Sekunden, und der erste Schreibvorgang nach jedem Start der App leert das Windows-Journal des Sticks (einige Sekunden).
- 391 Tests der Vollbild-App, 46 Host-Tests und die Simulation bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.93.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung (Deutsch) |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Dank
An alle, deren Arbeit in der App steckt oder sie möglich macht – die ganze Liste steht in der App unter „Credits“ oder Profil → Dank. Besonders **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **quer3q** für Dump Forge, AMPR EMU NG und Kraken Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und den Mitwirkenden am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.92.0...v1.93.0
