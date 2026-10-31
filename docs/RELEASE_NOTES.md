# PS5 Cooling & System Center – Pro v1.75.0

## Highlights
- **Die Vollbild-App ist da** – Kachel FAKE86921: die ganze App auf dem Fernseher mit dem Controller, von Kühlung bis Profil, ohne Browser
- **Zwei Kacheln, die die App selbst starten** – FAKE86921 (Vollbild) und FAKE86922 (Browser) tragen das Hauptprogramm in sich
- **Lüfterwächter** – ein unabhängiger zweiter Blick auf den Lüfter: hängt die Regelung oder wird es zu heiß, übernimmt er
- **Dumpen, Pakete, PS4 von USB, Dateimanager** – viele neue Werkzeuge, in beiden Oberflächen

## Neu
### Vollbild-App (Kachel FAKE86921)
- Alle Bereiche mit dem Controller: Kühlung (Übersicht, Einstellungen mit Warnungen, Lichtleiste, Spielprofilen, Nachtmodus, Selbsttest), Spiele (Bibliothek, Spielzeit, Spielstände, Pakete, PS4 von USB, Disc, Sicherungen, Aufträge), Dateimanager, Payloads (eigene, geladene, Store, Profile), System (Konsole, Speicher, Kühlleistung, Zusatzabfragen, Experte), Profil (Name, Profilbild, Danke) und Protokoll (App, Kernel-Log live, Gespeichert)
- Spiel-Werkzeuge im Aktionen-Menü: Kopieren, Konvertieren, Verschieben, Entpacken, Abbild prüfen, Löschen, Extras (AMPR, PlayGo, LZ4), Aus-/Einhängen über ShadowMountPlus, laufendes Spiel beenden
- Profilbilder: 30 mitgelieferte und eigene (JPG/PNG aus `/data/PS5-Cooling-Center/Profilbilder` oder vom USB-Stick), umgerechnet auf der Konsole
- Payload-Profile mit Editor, Pakete aufteilen, Bildschirmtastatur mit ä ö ü ß, eigene Hintergrundbilder je Bereich
- Die Seitenleiste öffnet die Touchpad-Taste

### Lüfter
- Unabhängiger Lüfterwächter: Regelung stumm → zurück an die Konsole (oder volle Kühlung, wenn es heiß ist); Übertemperatur → volle Kühlung, bis es wieder kühler ist; bei einem Absturz bekommt die Konsole den Lüfter zurück
- Kein Pendeln mehr bei Zielen um 72–82 °C, Regelung reagiert in Sekunden, Zieltemperatur ab 50 °C, Anzeige in Zehntelprozent

### Spiele und Laufwerke
- Dumpen im Reiter Disc: Entschlüsseln, FSELF, Backport (SDK herunterpatchen), Warteschlange für Disc-Spiele
- Pakete mit einem Klick installieren, PS4 von USB: übersprungene PKGs mit Grund, Updates und Zusatzinhalte wählen, Zusatzinhalte intern kopieren
- Einzelnes Spiel über ShadowMountPlus aus- und einhängen, USB-Laufwerk sicher auswerfen
- Dateimanager „Dateien & Ordner“: ganze Ordner hochladen, PS4-Spielordner konvertieren

## Verbesserungen
- USB-Laufwerke heißen wie auf der Konsole: `usb0`, `usb1` …
- Keine doppelten Bestätigungen mehr: Löschen, Power-Optionen, Beenden und Abbrechen wirken beim ersten Druck
- Apps wie YouTube bleiben unberührt (keine Updates, keine Spielstand-Aktionen)
- Fremdbausteine: PS4Mount 0.7.2 (Ausschalten hängt die Spiele aus), ShadowMountPlus 1.7beta11, PS5-Payload-SDK v0.45

## Fehlerbehebungen
- Zwei Durchsichten der ganzen App: u. a. Dump löscht keinen fertigen Dump mehr, Spielstand-Zurückspielen entspricht genau der Sicherung, Löschen gilt nur für die bestätigten Daten, Webserver antwortet sauber statt mit Verbindungsabbruch, Disc-Anzeige friert nicht mehr ein
- Fester Lüfterwert überschreibt den Wächter nicht mehr, der Verlauf bremst die Regelung nicht mehr

## Hinweise
- **Spiel-Updates entfernt:** Der Reiter „Updates“ und das Laden von Updates sind nicht mehr in der App
- Die Startmenü-Kachel PSCC69690 wird nicht mehr gebraucht und nicht mehr angelegt; die Kacheln FAKE86921 und FAKE86922 ersetzen sie
- **Nicht an der Konsole bestätigt:** PS4Mount 0.7.2 (Ausschalten mit laufendem Spiel), Pakete aufteilen, Payload-Profile ausführen und die Paket-Erkennung für Medien-Apps

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.74.0...v1.75.0
