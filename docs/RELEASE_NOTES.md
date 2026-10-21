# PS5 Cooling & System Center – Pro v1.73.0

## Highlights
- **Disc im Laufwerk** – neuer Reiter „Disc“: zeigt, welche Disc im Laufwerk liegt (PS2, PS3, PS4, PS5) mit Titelkennung, und legt von PS2-DVDs und PS3-Discs ein Abbild (ISO) mit CRC-32, MD5 und SHA-1 an, auf Wunsch geprüft gegen Redump (nach ps-disc-detect)
- **Warnung vor ffpfsc** – bei Spielen mit sehr vielen Daten oder Texture-Streaming fragt die App vor dem Packen, ob das Spiel trotzdem komprimiert werden soll
- **Spiel-Updates nur direkt** – kein SC-Link mehr: Die Adresse der Update-Liste liest die App aus der `param.json` des Spiels (Abbild oder Ordner, ohne Entpacken) oder aus der App-Datenbank der Konsole
- **Neues App-Symbol** und auf dem neuesten Stand: ShadowMountPlus 1.7beta6-fix2, PS4Mount 0.6.1, PatchDL 0.3.3

## Neu
### Disc
- Erkennung nach dem Einschalten (aus ab Werk, Abfrage alle 1,5 s): Art der Disc und Titelkennung wie bei der Konsole; PS4- und PS5-Discs werden nur erkannt
- „Abbild erstellen“ für PS2-DVD und PS3-Disc nach `/data/PS5-Cooling-Center/disc/`: Fortschritt, Geschwindigkeit, Restzeit, Abbrechen, Platzprüfung, Ruhemodus-Sperre, Eintrag im Verlauf; zweiter Schalter „Neue Disc automatisch sichern“ (aus ab Werk)
- Mit einer Redump-Liste unter `/data/redump.db` bekommt das Abbild den Namen aus der Liste und wird gegen sie geprüft
- PS1- und PS2-CDs kann das Laufwerk der PS5 nicht lesen; CD-Abbilder und PS3-IRD gibt es deshalb nicht

### Abbilder
- Der Abbild-Leser kennt jetzt auch `.ffpfs` (unkomprimierter PFS-Container)
- Bei Spielen über ShadowMountPlus zählt die Version aus dem Abbild selbst, wenn sie höher ist als die der App-Datenbank
- Backport-Ordner mit Namenszusatz (`backports/PPSA12345 Spielname`) werden erkannt, wie ShadowMountPlus 1.7beta6 sie wählt

### Warnung vor dem Packen
- Vor dem Konvertieren zu `.ffpfsc` prüft die App auf viele Daten (ab 40 GB), große Streaming-Container (`.ucas`, `.utoc`, `.pak`, `.forge` …, ab 4 GB zusammen) und Streaming-Ordner (ab 2 GB); trifft etwas zu, steht ein Hinweis im Fenster und beim Start die Frage „Trotzdem in das Format ffpfsc umwandeln?“ – „OK“ macht normal weiter, „Abbrechen“ beendet den Vorgang

## Verbesserungen
- PS4-Spiele von USB: ein Laufwerk, das nach einem Neustart unter anderem Namen wiederkommt (`usb1` statt `usb0`), behält seine angemeldeten Spiele (PS4Mount 0.6.1)
- Spiel-Updates laden schneller: Schreibblöcke von 2 MiB und größerer Empfangspuffer (PatchDL 0.3.3)
- Apps wie YouTube und Homebrew-Werkzeuge erscheinen nicht mehr im Reiter „Updates“
- Der Name des Herstellers kommt in Oberfläche, Handbuch, FAQ und Dokumentation nicht mehr vor
- Handbuch und FAQ in allen sechs Sprachen aktualisiert (neues Kapitel „Disc“, Abschnitt zur Warnung, PS4-USB-Hinweise)
- Fremdbausteine täglich prüfbar: `tools/upstream_check.py` und der Skill `upstream-update`

## Fehlerbehebungen
- Die Versionsauswahl für ältere Updates (SC-Link) ist entfernt, weil sie nicht wie gewünscht funktionierte; geladen wird nur, was auf dieser Firmware direkt angeboten wird

## Hinweise
- **Disc:** Zum Ausprobieren den Schalter im Reiter „Disc“ einschalten und eine PS2-DVD oder PS3-Disc einlegen; die Redump-Liste ist freiwillig
- **Starter-Kachel:** Ordner `PPSA86921` neu nach `/data/homebrew/` kopieren (enthält die aktuelle App und das neue Symbol); die Kachel in der Konsole zuerst löschen, wenn auch ihr Bild neu werden soll
- **Internet:** nur mit eingeschalteten Spiel-Updates, bei „Payloads herunterladen“ und mit der Redump-Liste, die du selbst ablegst; sonst nichts

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.73.0.elf` | die App – nach jedem Neustart der Konsole an Port 9021 senden |
| `PPSA86921.zip` | Starter-Kachel für `/data/homebrew/` (startet die App per Tipp) |
| `LIESMICH.txt` | Kurzanleitung |

**SHA-256**
```
034b801aba4d827a2c91682f3cca15a673eb757516fe8595e17883f52ba171c8  LIESMICH.txt
d14fbff0b05ab1325cae61046305b10e50998082b66b9aa5cf59fe9267e6ac0f  PPSA86921.zip
5f2e3d26d33981b2542f6a8b16d6cfee7989a4a3fe48d35d6d6c959f965a46de  PS5_Cooling_System_Center_v1.73.0.elf
```

## Danke
- **drakmor** – ShadowMountPlus, APR-EMU, PlayGo-Stub, DLC-Module
- **gasb07** – PS4Mount
- **Knutwurst** – PatchDL (Vorbild der Spiel-Updates)
- **PSBrew** – MkPFS
- **ps-disc-detect** – Vorbild für den Reiter „Disc“

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.72.0...v1.73.0
