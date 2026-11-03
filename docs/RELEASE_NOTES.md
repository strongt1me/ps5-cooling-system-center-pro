# PS5 Cooling & System Center – Pro v1.75.5

## Highlights
- **AMPR EMU NG in der Versionsauswahl** – die Weiterentwicklung von quer3q (liest LZ4- und zstd-Pakete) steht als „NG 0.2.0“ mit eigener Marke ganz oben und lässt sich auf Knopfdruck von GitHub laden
- **Handbuch und FAQ auf Schwedisch** – jetzt in allen sieben Sprachen

## Neu
- APR-EMU-Version wählen: Die App erkennt die Dateien von **ampr-emu-ng** (<https://github.com/quer3q/ampr-emu-ng>, GPL-3.0) an ihrer Kennung und zeigt sie als „NG 0.2.0“ bzw. „NG 0.2.0 debug“ mit grüner Marke „NG“. Eine eingebaute NG-Datei wird auch dann beim Namen genannt, wenn sie nicht im Ordner `AMPR_EMU` liegt.
- **„AMPR EMU NG 0.2.0 von GitHub laden“** holt die beiden Dateien der Veröffentlichung v0.2.0 in den Ordner `AMPR_EMU` – nur auf Knopfdruck, nur von GitHub, und erst nach passender Prüfsumme (SHA-256). Mitgeliefert wird NG nicht, damit die App klein bleibt.
- „Datei hinzufügen“ benennt Dateien einer neueren NG-Veröffentlichung (`libSceAmpr.sprx`, `libSceAmpr-debug.sprx`) selbst passend um.
- Nach dem Einbauen von NG erinnert eine Meldung daran, in ShadowMountPlus das Aktualisieren der fakelib abzuschalten (`update_emulators=0`) – sonst ersetzt ShadowMountPlus die Datei beim Spielstart. Im Browser als Warnkasten, in der Vollbild-App als Meldung und als Meldung der Konsole.
- Browser und Vollbild-App können das gleichermaßen (Vollbild: Extras → APR-EMU).
- Handbuch und FAQ gibt es jetzt auch auf Schwedisch; alle Fassungen nennen Svenska in der Sprachleiste.

## Fehlerbehebungen
- Bei schwedischer Oberfläche führte der Handbuch-Knopf ins Leere (die schwedische Fassung fehlte).
- Schwedisch: Vier Knöpfe lasen sich wie Zustandsmeldungen („Packar upp LZ4 …“); jetzt in Befehlsform („Packa upp LZ4 …“, „Packa med LZ4 …“, „Packa upp …“, „Dela upp …“).

## Hinweise
- Ob ein Spiel mit AMPR EMU NG läuft, hängt vom Spiel ab – vorher ungepackt testen, das Original bleibt als `libSceAmpr.sprx.orig` gesichert.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.5.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An quer3q für ampr-emu-ng, an drakmor für ampr_emu und an alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.4...v1.75.5
