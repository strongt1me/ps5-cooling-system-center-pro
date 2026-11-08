# PS5 Cooling & System Center – Pro v1.78.0

## Highlights
- **Spiele mit unsignierten Programmdateien starten:** Beim Konvertieren und beim Packen in einen neuen Ordner schreibt die App einfache ELF-Dateien als **FSELF** – an der Konsole bestätigt
- **zstd packt immer im Spielordner und kann die Programmdateien dabei signieren** – ohne Frage nach den Originalen
- **AMPR EMU NG 0.3.3** (quer3q) fest eingebaut, **LZ4 1.11.0** in den LZ4-Paketen

## Neu

### Unsignierte Programmdateien als FSELF
- Ein Spiel, dessen Programmdateien (`eboot.bin`, `.prx`, `.sprx`) einfache ELF-Dateien sind, startet an der Konsole nicht: Sie lehnt es mit `0x80020008` ab. Beim **Konvertieren** eines PS5-Ordners (exFAT, ffpkg, ffpfsc) und beim **LZ4-Packen in einen neuen Ordner** schreibt die App diese Dateien jetzt als **fake-signierte SELF-Dateien (FSELF)**. Der Spielordner wird dabei nur gelesen, ein `ampr_emu.index` wird auf die neuen Größen gebracht.
- Der Plan nennt vorab, wie viele Programmdateien betroffen sind – in der Browser-Oberfläche und in der Vollbild-App („Unsignierte Programmdateien“; im Packformular die Zeile „Programmdateien“). Dateien mit ungewöhnlichem Aufbau bleiben, wie sie sind, und die Notiz des Auftrags sagt es.
- Der Aufbau ist der von `make_fself.py` (ampr-emu-ng) und ps5-dump-forge 0.5.4 und stimmt Byte für Byte mit einer auf der Konsole laufenden `eboot.bin` überein.
- **An der Konsole bestätigt:** Ein Ordner mit einfacher ELF als `eboot.bin` startet nicht, dasselbe Spiel mit der App konvertiert startet und lief im Vordergrund.

### zstd: Spielordner und Signieren
- Mit zstd wird **immer im Spielordner** gepackt. Die Wahl „Im Ordner selbst oder als Kopie“ und der Rat, die Originale zu behalten, entfallen; die Originale gehen erst, wenn jede gepackte Datei Byte für Byte mit ihrem Original verglichen ist, und „zstd entpacken“ bringt alles zurück. Das Kästchen „ungepackt getestet“ bleibt.
- Neuer Schalter **„Programmdateien signieren“** (voreingestellt an, nur wählbar, wenn unsignierte Programmdateien da sind): Nach dem Packen ersetzt die App sie im Spielordner durch FSELF-Fassungen, jede gegen das Original zurückgelesen. Scheitert eine, bleiben die fertigen signiert und die übrigen, wie sie waren.

### Fremdbausteine
- **AMPR EMU NG v0.3.3** (quer3q, 07.11.2026) ersetzt die eingebettete 0.3.2: Korrekturen in der Laufzeit (vollständig installierte Spiele fragen nicht mehr nach Weiterladen oder Platz, Marvel's Wolverine startet wieder, weniger Hänger bei vielen gepackten Dateien, Ghost of Yōtei und Hollow Knight Silksong). Das Paketformat ist unverändert.
- **LZ4 1.11.0** (Yann Collet) statt 1.10.0 in den LZ4-Paketen. Das Blockformat ist gleich: Ein Test mit 884 Blöcken in beide Richtungen zeigte keinen Unterschied, die APR-EMU liest die Pakete weiter.
- **ps5-dump-forge 0.5.4** (quer3q): das zstd-Format ist unverändert; sein Signieren war die Vorlage für das oben.

## Hinweise
- Die neue ELF an Port 9021 senden (sie beendet die ältere Instanz selbst) und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Ab 1.77.1 ersetzen die Kacheln eine ältere laufende App beim Antippen selbst.
- Mit zstd gepackte Spiele laufen nur mit AMPR EMU NG. **An der Konsole geprüft:** Matchbox Driving Adventures (6 GB) lief nach dem Packen mit zstd und NG 0.3.3 mindestens zwei Minuten im Vordergrund und ließ sich vollständig entpacken. Längeres Spielen und andere Spiele sind nicht geprüft.
- Ältere NG-Dateien im Ordner `AMPR_EMU` der Konsole bleiben dort, bis du sie im Fenster „APR & LZ4“ entfernst.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.78.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG und ps5-dump-forge, deren Signieren hier die Vorlage war, und alle, die NG mit ihren Spielen testen.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.77.1...v1.78.0
