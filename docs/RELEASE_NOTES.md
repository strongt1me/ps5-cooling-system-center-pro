# PS5 Cooling & System Center – Pro v1.75.3

## Highlights
- **Die Vollbild-App spricht jetzt 7 Sprachen** – Deutsch, Englisch, Spanisch, Französisch, Italienisch, Russisch und Schwedisch
- **Sprache direkt wählen** – Options-Taste → „Sprache / Language“ öffnet eine Liste aller Sprachen
- **Übersetzungen vervollständigt** – rund 1.600 fehlende Texte der Browser-Oberfläche ergänzt
- **Sicherer Dateimanager** – leere Einhängeorte wie `/mnt/usb2` oder `/mnt/disc` lassen sich nicht mehr versehentlich löschen

## Neu
### Vollbild-App mehrsprachig
- Ab Werk folgt die Vollbild-App der Systemsprache der Konsole (andere Sprachen: Englisch).
- Options-Taste → „Sprache / Language“: Liste aller Sprachen, die aktive mit Häkchen, ein Druck wählt. Die Wahl bleibt gespeichert und ist Teil von Export/Import der Einstellungen.
- Sie nutzt dieselben Wörterbücher wie die Browser-Oberfläche; was in keinem steht, bleibt deutsch.

## Verbesserungen
- Schwedisch (beigetragen von Daniel Nylander): die seither hinzugekommenen Texte der Browser-Oberfläche ergänzt.
- Englisch, Spanisch, Französisch, Italienisch und Russisch: je rund 200 fehlende Texte ergänzt – vor allem Meldungen zu Dumpen, PS4 von USB, ShadowMountPlus, Spielstart und Lüfterwächter.
- Dateimanager: Ändern geht jetzt überall, auch in Systemordnern wie `/user/download` und im Ordner der App. Gesperrt bleibt, was vor Unfällen schützt: `/`, `/dev`, `/proc`, Verknüpfungen, die Ordner direkt unter `/` und `/mnt` selbst, Ordner mit eingehängten Laufwerken darunter und Gerätedateisysteme. Überschrieben wird nie.

## Fehlerbehebungen
- Dateimanager: Leere Einhängeorte (`/mnt/usb2`–`usb7`, `/mnt/ext0`, `/mnt/disc`, `/mnt/sandbox`) ließen sich löschen oder umbenennen – danach hätte der nächste Stick, der erweiterte Speicher oder die nächste Disc keinen Platz mehr gefunden.
- Vollbild-App: Französische Texte zeigten vor jedem Doppelpunkt ein „?“ (geschütztes Leerzeichen); das Minuszeichen ebenso.
- Vollbild-App: Gekürzte Texte enden nicht mehr mit einem „?“, und eine Übersetzung wird nicht mehr ein zweites Mal übersetzt („Profiles“ statt „Profile“).
- Vollbild-App: Eine gewählte Sprache lädt auch dann, wenn die Systemangaben der Konsole nicht antworten.
- Ausgeschaltete Laufwerks-Telemetrie erzeugt keinen roten 409-Fehler mehr in der Browser-Konsole; die Systemseite sagt „deaktiviert“.
- Die Textvorschau im Dateimanager bricht nach 30 Sekunden mit „Die Konsole antwortet nicht.“ ab, statt endlos zu laden.

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren: Die Sprachen der Vollbild-App stecken in der Kachel.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.3.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An Daniel Nylander für die schwedische Übersetzung – und an alle, deren Arbeit in der App steckt oder sie möglich macht; die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.2...v1.75.3
